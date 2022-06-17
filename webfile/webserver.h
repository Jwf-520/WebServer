#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "../threadpool/threadpool.h"
#include "../http/http_conn.h"

const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5;             //最小超时单位

class WebServer
{
public:
    WebServer();
    ~WebServer();

    void init(int port , string user, string passWord, string databaseName,
              int log_write , int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);
    //线程池函数
    void thread_pool();
    //数据库池函数
    void sql_pool();
    void log_write();
    //更改模式  
    void trig_mode();
    //创建lfd 
    void eventListen();
    //当服务器非关闭状态 用于处理事件
    void eventLoop();
    //定时器的操作
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int sockfd);
    //处理用户数据
    bool dealclinetdata();
    //信号
    bool dealwithsignal(bool& timeout, bool& stop_server);
    //读事件
    void dealwithread(int sockfd);
    //写事件
    void dealwithwrite(int sockfd);

public:
    //基础
    //监听端口
    int m_port;
    char *m_root;
    //日志
    int m_log_write;
    int m_close_log;
    //触发模式
    int m_actormodel;
    //进程通信模块
    int m_pipefd[2];
    //epoll根
    int m_epollfd;
    //用于接受用户连接
    http_conn *users;

    //数据库相关
    connection_pool *m_connPool;
    string m_user;         //登陆数据库用户名
    string m_passWord;     //登陆数据库密码
    string m_databaseName; //使用数据库名
    int m_sql_num;

    //线程池相关
    threadpool<http_conn> *m_pool;
    int m_thread_num;

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd; //监听fd 申请一次
    int m_OPT_LINGER;
    int m_TRIGMode; //触发模式 ET+LT LT+LT LT+ET  ET+ET 
    int m_LISTENTrigmode;  // 监听 ET/LT
    int m_CONNTrigmode;    // 连接 ET/LT

    //定时器相关
    client_data *users_timer;
    Utils utils;
};
#endif
