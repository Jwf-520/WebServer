#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../log/log.h"

//连接资源结构体成员需要用到定时器类
//需要前向声明
class util_timer;
//连接资源
struct client_data {
    //客户端socket地址
    sockaddr_in address;
    //socket文件描述符
    int sockfd;
    //定时器
    util_timer *timer;
};
//定时器类
class util_timer {
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    //超时时间
    time_t expire;
    //回调函数
    void (* cb_func)(client_data *);
    //连接资源
    client_data *user_data;
    //前向定时器
    util_timer *prev;
    //后向定时器
    util_timer *next;
};

//项目中的定时器容器为带头尾结点的升序双向链表，具体的为每个连接创建一个定时器，
//将其添加到链表中，并按照超时时间升序排列。执行定时任务时，将到期的定时器从链表中删除。
//从实现上看，主要涉及双向链表的插入，删除操作，其中添加定时器的事件复杂度是O(n),
//删除定时器的事件复杂度是O(1)。

class sort_timer_lst {
public:
    sort_timer_lst();
    ~sort_timer_lst();
    //添加定时器，内部调用私有成员add_timer
    //若当前链表中只有头尾节点，直接插入
    //否则，将定时器按升序插入
    void add_timer(util_timer *timer);

    //adjust_timer函数，当定时任务发生变化,调整对应定时器在链表中的位置
    //客户端在设定时间内有数据收发,则当前时刻对该定时器重新设定时间，这里只是往后延长超时时间
    //被调整的目标定时器在尾部，或定时器新的超时值仍然小于下一个定时器的超时，不用调整
    //否则先将定时器从链表取出，重新插入链表
    void adjust_timer(util_timer *timer);
    //del_timer函数将超时的定时器从链表中删除
    //常规双向链表删除结点
    void del_timer(util_timer *timer);
    //定时任务处理函数
    void tick();

private:
    void add_timer(util_timer *timer, util_timer *lst_head);

    util_timer *head;
    util_timer *tail;
};

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    sort_timer_lst m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};
//回调函数处理不活跃的连接
void cb_func(client_data *user_data);

#endif
