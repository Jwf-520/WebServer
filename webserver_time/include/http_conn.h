#ifndef HTTP_CONN_H
#define HTTP_CONN_H
#include <sys/epoll.h>
#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h> //内存映射
#include <stdarg.h>
#include <errno.h>
#include "locker.h"
#include <sys/uio.h>
#include <string.h>
#include <stdio.h>
#include "lst_timer.h"
/*浏览器发出http连接请求，主线程创建http对象接受请求并将所有数据读入对应的buffer，将该对象插入任务队列，工作线程从任务队列中取出一个任务进行处理
工作线程取出任务后，调用process_read函数，通过主、从状态机对请求报文进行解析。
解析完成后，跳转do_request函数生成响应报文，通过process_write写入buffer，返回给浏览器。

*/

class http_conn1 {
public:
    static int m_epollfd; //所有的socket上的事件都被注册到同一个epoll对象中,所以将epoll文件描述符设置为静态
    static int m_user_count; //统计用户的数量
    static const int READ_BUFFER_SIZE = 2048;    //读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024;    //写缓冲区的大小
    static const int FILENAME_LEN = 200;        // 文件名的最大长度
public:
    int m_sockfd;          //该HTTP连接的socket
    sockaddr_in m_address; //通信对方的socket地址
    util_timer* timer;     //定时器
    char m_read_buf[READ_BUFFER_SIZE]; //读缓冲区


     // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH};
    
    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    
    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };

    http_conn1() {}
    ~http_conn1() {}
public:
    void init(int sockfd, const sockaddr_in & addr); //初始化新接受的连接
    void process(); //处理客户端的请求
    void close_conn(); //关闭连接
    bool read(http_conn1* ); //非阻塞的读
    bool write(); //非阻塞的写
private:

    void init();  //初始化连接
    HTTP_CODE process_read(); //解析HTTP请求
    bool process_write(HTTP_CODE ret);    // 填充HTTP应答

    //下面这组函数被process_read调用以分析HTTP请求
    HTTP_CODE parse_request_line(char* text);  //解析请求首行
    HTTP_CODE parse_headers(char* text);  //解析请求头
    HTTP_CODE parse_content(char* text);  //解析请求体
    LINE_STATUS parse_line();    //具体解析每一行到\r\n
    HTTP_CODE do_request();   //生成响应报文
    char* get_line();


    // 这一组函数被process_write调用以填充HTTP应答。
    void unmap();
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_content_type();
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();



private:
    int m_read_idx;            //标识读缓冲区中已经读入的客户端数据的最后一个字节的下一个位
    int m_checked_idx;     //当前正在分析的字符在读缓冲区的位置
    int m_start_line;      //当前正在解析行的起始位置
    char m_write_buf[ WRITE_BUFFER_SIZE ];  // 写缓冲区
    int m_write_idx;                        // 写缓冲区中待发送的字节数

    CHECK_STATE m_check_state; //主状态机当前所处的状态
    METHOD m_method;    //请求方法
    char m_real_file[FILENAME_LEN];       // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录
    char * m_url;       //请求目标文件的文件名
    char * m_version;   //协议版本，只支持HTTP1.1
    char* m_host;       //主机名
    bool m_linger;      //HTTP请求是否要求保持连接
    int m_content_length; //HTTP请求的消息体长度
    struct stat m_file_stat;                // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    char* m_file_address;                   // 客户请求的目标文件被mmap到内存中的起始位置
    struct iovec m_iv[2];                   // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。一个是写缓冲区，还有一个是请求体映射到内存中的目标文件m_file_address
    int m_iv_count;
};
#endif