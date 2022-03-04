#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <exception>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h> //跟错误号相关的头文件
#include <fcntl.h> //主要操作文件描述符
#include <sys/epoll.h>
#include "locker.h"  
#include "threadpool.h" //一开始在程序就要把线程池创建出来，去运行起来，当有任务的时候，追加到队列当中，有个线程找个合适的时间去处理
#include <signal.h> //与信号相关的头文件
#include "http_conn.h"
#include "lst_timer.h"
#define MAX_FD 65535 //最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000
#define TIMESLOT 5

static int pipefd[2];

extern sort_timer_lst timer_lst;
/*
定时器的机制:设置SIGALRM信号处理函数, 设置一个定时器, 到期处理函数就往pipefd[1]里面写, 
epoll_wait就会由于pipefd[0]可以读而触发, 再设置timeout为true, 调用tick()函数删除非活跃连接, 
然后再设置一个定时器,以此重复。
*/



//1、argc为整数
//2、argv为指针的指针，argv是一个指针数组，元素个数为argc，存放指向每一个参数的指针

/*
当输入prog para_1 para_2 有2个参数，则由操作系统传来的参数为：
   argc = 3，表示除了程序名外还有2个参数。
   argv[0]指向输入的程序路径及名称。
   argv[1]指向参数para_1字符串。
   argv[2]指向参数para_2字符串。
*/
using namespace std;

void sig_handler(int sig) {
    int save_errno = errno;
    int msg = sig;
    //将SIGALARM信号发送到管道当中，将信号的值发送到管道里面
    send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}
//添加信号捕捉
void addsig(int sig, void(*handler)(int)) {
    struct sigaction sa; //注册信号的参数
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask); //设置临时阻塞的信号集
    assert(sigaction(sig, &sa, NULL) != -1);

}

void timer_handler() {
    //定时处理任务，实际上就是调用tick()函数
    timer_lst.tick();
    //因为一次alarm调用只会引起一次SIGALARM信号，所以我们要重新定时，以不断触发SIGALARM信号。
    alarm(TIMESLOT);
}





//添加文件描述符到epoll中
//如果这样函数的声明中带有关键字extern，仅仅是暗示这个函数可能在别的源文件里定义。
//这样一来，就是在程序中取代include “*.h”来声明函数，在一些复杂的项目中，比较习惯在所有的函数声明前添加extern修饰，以防止遗漏包含头文件而导致的编译错误。
extern void addfd(int epollfd, int fd, bool one_shot);
//从epoll中删除文件描述符
extern void removefd(int epollfd, int fd);
//修改文件描述符
extern void modfd(int epollfd, int fd, int ev);


extern void setnonblocking(int fd);
extern void cb_func(http_conn1* user_data);
extern 

int main(int argc, char* argv[]) {
    
    if (argc <= 1) {
        cout << "按照如下格式运行： " << basename(argv[0]) << " post_name" << endl;
        exit(-1);
    }
    //获取端口号
    int port = atoi(argv[1]);

    //对SIGPIPE信号进行忽略处理,知道服务器往已经关闭了的sockfd中写两次时，会产生SIGPIPE信号，如果不处理，默认会挂掉服务器，所以要忽视这个信号。
    addsig(SIGPIPE, SIG_IGN);

    //创建线程池，初始化线程池
    threadpool<http_conn1> * pool = NULL;
    try {
        pool = new threadpool<http_conn1>;
    } catch(...) {
        //如果线程池没有创建出来，直接退出
        exit(-1); 
    }
    //创建一个数组用于保存所有的客户端信息
    http_conn1* users = new http_conn1[MAX_FD];
    
    int ret = 0;
    //编写错误处理函数
    //创建监听的套接字
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    //设置端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //绑定
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret != -1);
    //监听
    //backlog：等待连接队列的最大长度。
    ret = listen(listenfd, 5);
    assert(ret != -1);


    //建立epoll对象，事件数组，添加
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);

    //将监听的文件描述符添加到epoll对象中
    addfd(epollfd, listenfd, false);
    //创建管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    /*
        阻塞模式下的两个特征：
        1、当读管道时，如果管道中没有数据，则会阻塞，直到管道另一端写入数据。
        2、当写管道时，如果管道中已经满了，则会阻塞，直到管道另一端读出数据(读出的数据会从管道中清除)。\
    */
    setnonblocking(pipefd[1]); //非阻塞写端，若管道里面的数据写满了，直接返回，过一会再来写
    addfd(epollfd, pipefd[0], false); //将管道的读端加入到epoll中进行监听，当有数据的时候，说明有信号产生了，发送过来了。

    //设置信号处理函数，添加定时器。
    addsig(SIGALRM, sig_handler);
    addsig(SIGTERM, sig_handler);
    bool stop_server = false;
    http_conn1::m_epollfd = epollfd;
    bool timeout = false;
    alarm(TIMESLOT); //定时，5秒后产生SIGALARM信号

    while (!stop_server) {
        //num代表检测到了几个事件
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);

        if ((num < 0) && (errno != EINTR)) {
            cout << "epoll failure" << endl;
            break;
        }

        //循环遍历事件数组
        for (int i = 0; i < num; i++) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {
                //有客户端连接进来
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                //
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlen);

                if (http_conn1::m_user_count >= MAX_FD) {
                    //目前连接数满了
                    //给客户端写一个信息：服务器内部正忙
                    close(connfd);
                    continue;
                }
                //将新客户的数据初始化，放到数组中
                users[connfd].init(connfd, client_address); 
                //创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，最后将定时器添加到链表timer_lst中
                util_timer* timer = new util_timer();
                timer->user_data = &users[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                users[connfd].timer = timer;
                timer_lst.add_timer(timer);
            } else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)) {
                //处理信号
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1) {
                    continue;
                } else if (ret == 0) {
                    continue;
                } else {
                    for (int i = 0; i < ret; i++) {
                        switch (signals[i]) {
                            case SIGALRM: {
                                /*用timeout变量标记有定时任务需要处理，但不立即处理定时任务
                                 这是因为定时任务的优先级不是很高，我们优先处理其他更重要的任务。*/
                                 timeout = true;
                                 break;
                            }
                            case SIGTERM: { //终止信号
                                stop_server = true;

                            }
                        }   
                    }
                }

            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                //对方异常断开或者错误的事件
                users[sockfd].close_conn();
            } else if (events[i].events & EPOLLIN) {

                if (users[sockfd].read(&users[sockfd])) {
                    //一次性把所有数据读完
                    //将数据放到线程池的工作队列中，给线程处理
                    pool->append(users + sockfd);
                } else {
                    users[sockfd].close_conn();
                }
            } else if (events[i].events & EPOLLOUT) {
                //一次性写完所有数据
                if (!users[sockfd].write()) {
                    users[sockfd].close_conn();
                }

            }
        }
        // 最后处理定时事件，因为I/O事件有更高的优先级。当然，这样做将导致定时任务不能精准的按照预定的时间执行。
       if (timeout) {
           timer_handler();
           timeout = false;
       } 

    }

    close(epollfd);
    close(listenfd);
    delete[] users;
    delete pool;


    return 0;
}