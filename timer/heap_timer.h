#ifndef MIN_HEAP
#define MIN_HEAP

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
#include <iostream>
#include <algorithm>
#include <time.h>
#include "../log/log.h"
using std::exception;
#define BUFFER_SIZE 64


class heap_timer;

struct client_data{
    sockaddr_in address;
    int sockfd;
    char buff[BUFFER_SIZE];
    heap_timer* timer;
};

class heap_timer {
public:
    heap_timer(int delay) {
        expire = time(nullptr) + delay;
    }
public:
    time_t expire;
    void (*cb_func)(client_data*);
    client_data* user_data;
};

void cb_func(client_data *user_data);

class time_heap {
public:
    time_heap(int cap);
    time_heap(heap_timer** init_array, int size, int capacity);
    ~time_heap();

    void add_timer(heap_timer* timer);
    void del_timer(heap_timer* timer);
    heap_timer* top() const;
    void pop_timer();
    void tick();
    bool empty() const;
private:
    void heapify(int hole);
    void resize();

private:
    heap_timer** array;
    //时间堆的大小
    int capacity; 
    //当前有多少个定时器在时间堆中
    int cur_size;
};

class Utils {
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

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);
public:
    static int *u_pipefd;
    time_heap m_timer_heap(int cap);
    static int u_epollfd;
    int m_TIMESLOT;
};

#endif