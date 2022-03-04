#include "http_conn.h"
#include <assert.h>


#define TIMESLOT 5
//所有socket上的事件都被注册到同一个epoll内核事件中，所以设置为静态的
int http_conn1::m_epollfd = -1;
//所有的客户数
int http_conn1::m_user_count = 0; 
sort_timer_lst timer_lst;

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 网站的根目录
const char* doc_root = "/home/jwf/Desktop/webserver/resources";

//设置文件描述符非阻塞
void setnonblocking(int fd) {
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

//向epoll中添加需要监听的文件描述符,并且判断是否触发EPOLL的EPOLLONESHOT事件
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    //event.events = EPOLLIN | EPOLLRDHUP;//水平触发，如果不使用EPOLLRDHUP事件，我们也可以单纯的使用EPOLLIN事件然后根据recv函数的返回值来判断socket上收到的是有效数据还是对方关闭连接的请求。
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLET; //边沿触发，有数据请求才会触发一次读数据。
    /*
    即使可以使用 ET 模式,一个socket 上的某个事件还是可能被触发多次。这在并发程序中就会引起一个
    问题。比如一个线程在读取完某个 socket 上的数据后开始处理这些数据,而在数据的处理过程中该
    socket 上又有新数据可读(EPOLLIN 再次被触发),此时另外一个线程被唤醒来读取这些新的数据。于
    是就出现了两个线程同时操作一个 socket 的局面。一个socket连接在任一时刻都只被一个线程处理,可
    以使用 epoll 的 EPOLLONESHOT 事件实现。
    对于注册了 EPOLLONESHOT 事件的文件描述符,操作系统最多触发其上注册的一个可读、可写或者异
    常事件,且只触发一次,除非我们使用 epoll_ctl 函数重置该文件描述符上注册的 EPOLLONESHOT 事
    件。这样,当一个线程在处理某个 socket 时,其他线程是不可能有机会操作该 socket 的。但反过来思
    考,注册了 EPOLLONESHOT 事件的 socket 一旦被某个线程处理完毕, 该线程就应该立即重置这个
    socket 上的 EPOLLONESHOT 事件,以确保这个 socket 下一次可读时,其 EPOLLIN 事件能被触发,进
    而让其他工作线程有机会继续处理这个 socket。
    */
    if (one_shot) {
        event.events | EPOLLONESHOT;
    }

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    //设置文件描述符非阻塞主要是为了ET模式
    setnonblocking(fd);
}




//从epoll中移除需要监听的文件描述符
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//修改文件描述符，重置socket上EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}


//定时器回调函数，它删除非活动连接socket上的注册事件，并关闭之。
void cb_func(http_conn1* user_data) {
    epoll_ctl(http_conn1::m_epollfd, EPOLL_CTL_DEL, user_data->m_sockfd, 0);
    assert(user_data);
    close(user_data->m_sockfd);
    std::cout << "close fd " << user_data->m_sockfd << std::endl;
}


//初始化连接
void http_conn1::init(int sockfd, const sockaddr_in& addr) {
    m_sockfd = sockfd;
    m_address = addr;

    //端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //添加到epoll对象中
    addfd(m_epollfd, m_sockfd, true);
    m_user_count++;  //总用户数+1
    init();
}


void http_conn1::init() {
    m_check_state = CHECK_STATE_REQUESTLINE;    //初始化状态为解析请求首行
    m_checked_idx = 0;                          //当前正在分析的字符在读缓冲区的位置，默认为0
    m_start_line = 0;                           //当前解析行的起始位置默认为0
    m_read_idx = 0;                             //标识读缓冲区中已经读入的客户数据的最后一个字节的下一个位置，默认为0                
    m_method = GET;                             //HTTP请求方式，默认请求方式为GET
    m_url = 0;                                  //客户请求的目标文件的文件名，默认为0
    m_version = 0;                              //HTTP协议的版本号，默认为0
    m_host = 0;                                 //主机名，默认为0
    m_content_length = 0;                       //HTTP请求的消息体长度，默认为0
    m_linger = false;                           //HTTP请求是否要求保持连接，默认不保持连接
    m_write_idx = 0;                            //写缓冲区中待发送的字节数，默认为0
    //初始化读缓冲区，写缓冲区，目标文件的完整路径
    bzero(m_read_buf, READ_BUFFER_SIZE);        
    bzero(m_write_buf, READ_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}


//关闭连接
void http_conn1::close_conn() {
    if (m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--; //关闭一个连接，客户总数量-1
    }
}

//循环读取客户数据，直到无数据可读或者对方关闭连接
bool http_conn1::read(http_conn1* user_data) { 
    
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }
    
    //读取到的字节
    int bytes_read = 0;
    while (true) {
        //循环读取发送过来的数据，将已经读取的数据放入读缓冲区，读缓冲区的大小一步步减少
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                //没有数据
                break;
            }
            //如果发生读错误，则关闭连接，并移除其对应的定时器
            cb_func(user_data);
            if (user_data->timer) {
                timer_lst.del_timer(timer);
            }
            //出错
            return false;
        } else if (bytes_read == 0) {
            //如果对方已经关闭连接，则我们也关闭连接，并移除对应的定时器。
            cb_func(user_data);
            if (user_data->timer) {
                timer_lst.del_timer(timer);
            }
            //对方关闭了连接
            return false;
        } else {
            //如果客户端上有数据可读，则我们要调整该连接对应的定时器，以延迟该连接被关闭的时间。
            if (user_data->timer) {
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                std::cout << "adjust timer once" << std::endl;
                timer_lst.adjust_timer(user_data->timer);
            }
            m_read_idx += bytes_read;
        }
        
    }
    std::cout << "读取到了数据： " << m_read_buf << std::endl;
    return true;
}



//解析HTTP请求首行,获得请求方法，目标URL，HTTP版本号
http_conn1::HTTP_CODE http_conn1::parse_request_line(char * text) {
    //GET /index.html HTTP/1.1
    //请求行中最先含有空格和\t任意字符的位置并返回
    /*
    返回值：该函数返回 str中第一个匹配字符串 strCharSet中字符的字符位置，如果未找到字符则返回 NULL。
    函数说明： strpbrk函数用来检索字符串 str中第一个匹配字符串 strCharSet 中字符的字符，不包含空结束字符。
    也就是说，依次检验字符串 str 中的字符，当被检验字符在字符串 strCharSet 中也包含时，则停止检验，并返回该字符位置。
    */
    m_url = strpbrk(text, " \t");
    if (!m_url) {
        return BAD_REQUEST;
    }

    //GET\0/index.html HTTP/1.1
    *m_url++ = '\0';
    char * method = text;
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }
    //m_url此时跳过了第一个空格或\t字符，但不知道之后是否还有
    //将m_url向后偏移，通过查找，继续跳过空格和\t字符，指向请求资源的第一个字符
    // /index.html HTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    // /index.html\0HTTP/1.1
    *m_version++ = '\0';
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }
    //http://192.168.1.1:10000/index.html
    //目前m_url为/index.html\0HTTP/1.1
    //"http://"长度为7
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7; //192.168.1.1:10000/index.html
        m_url = strchr(m_url, '/'); // 找/第一次出现的位置，现在m_url为/index.html
    }
    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER; //主状态机检查状态变成检查请求头
    return NO_REQUEST; //还要继续解析，需要继续读取客户数据
}

//解析HTTP请求头部信息
http_conn1::HTTP_CODE http_conn1::parse_headers(char * text) {
    // 遇到空行，表示头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        //HTTP请求的消息体长度m_content_length，默认为0
        if ( m_content_length != 0 ) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
        //处理Connection头部字段
        //Connection:keep-alive
    } else if (strncasecmp( text, "Connection:", 11) == 0) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn(text, " \t"); //""keep-alive" 与 " \t" 比较,第一个字符k都没对上，返回0
        if (strcasecmp(text, "keep-alive") == 0) {
            //如果是长连接，则将linger标志设置为true
            m_linger = true;
        }
    } else if (strncasecmp(text, "Content-Length:", 15) == 0 ) {
        // 处理Content-Length头部字段(解析请求头部内容长度字段)
        text += 15;
        text += strspn(text, " \t" );
        m_content_length = atol(text); //把字符串转换成长整型数
    } else if (strncasecmp(text, "Host:", 5) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else {
        std::cout << "opp! unknow header:" << text << std::endl;
    }
    return NO_REQUEST;

} 

//解析请求体，没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了
http_conn1::HTTP_CODE http_conn1::parse_content(char * text){
    if ( m_read_idx >= ( m_content_length + m_checked_idx ) )
    {
        text[ m_content_length ] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}  

//m_start_line当前正在解析的行的起始位置，将该位置后面的数据赋给text
//此时从状态机已提前将一行的末尾字符\r\n变为\0\0，所以text可以直接取出完整的行进行解析
char* http_conn1::get_line() {
    return m_read_buf + m_start_line;
}

//解析HTTP请求---主状态机
http_conn1::HTTP_CODE http_conn1::process_read() {
    //初始化从状态机状态、HTTP请求解析结果
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char * text = 0;
    //解析一行数据，parse_line为从状态机的具体实现
    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
            || ((line_status = parse_line()) == LINE_OK)) {
            //解析到了一行完整的数据, 或者解析到了请求体，也是完成的数据
            //获取一行数据
            text = get_line();
            //m_start_line是每一个数据行在m_read_buf中的起始位置
		    //m_checked_idx表示从状态机在m_read_buf中读取的位置
            m_start_line = m_checked_idx;
            std::cout << "got 1 http line : " << text << std::endl;
            //主状态机的三种状态转移逻辑
            switch(m_check_state) {
                case CHECK_STATE_REQUESTLINE:  //请求请求行
                {
                    ret = parse_request_line(text);
                    if (ret == BAD_REQUEST) {
                        return BAD_REQUEST;
                    }
                    break;
                }
                case CHECK_STATE_HEADER:
                {
                    //解析请求头
                    ret = parse_headers(text);
                    if (ret == BAD_REQUEST) {
                        return BAD_REQUEST;
                    } else if (ret == GET_REQUEST) {
                        return do_request();
                    }
                    break;
                }
                case CHECK_STATE_CONTENT:
                {
                    //解析消息体
                    ret = parse_content(text);
                    //完整解析POST请求后，跳转到报文响应函数
                    if (ret == GET_REQUEST) {
                        return do_request();
                    }
                    line_status = LINE_OPEN;   //解析完消息体即完成报文解析，避免再次进入循环，更新line_status
                    break;
                }
                default: 
                {
                    return INTERNAL_ERROR;
                }
            }
    }
    return NO_REQUEST;
} 

 //从状态机，解析一行，判断依据\r\n
http_conn1::LINE_STATUS http_conn1::parse_line(){
    char temp;

    for ( ; m_checked_idx < m_read_idx; m_checked_idx++) {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            //下一个字符到达buffer结尾，则接收不完整，需要继续接收
            if ((m_checked_idx + 1) == m_read_idx) { 
                return LINE_OPEN;     //行数据不完整,因为m_read_idx表示客户数据的最后一个字节的下一个位置
            } else if (m_read_buf[m_checked_idx + 1] == '\n') { //下一个字符是\n，将\r\n改为\0\0
                 // GET / HTTP1.1\r\n
                 //----->GET / HTTP1.1\0\0
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;      //读取到一个完整的行，并将这一行的\r\n变为\0，即结束符，结束这一行
            }
            return LINE_BAD;         //都不符合，行出错
        } else if (temp == '\n') {  //如果当前字符是\n，也有可能读取到完整行，一般是上次读取到\r就到buffer末尾了，没有接收完整，再次接收时会出现这种情况
            if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx] == '\r')) {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }    
            return LINE_BAD;
        } 
    }
    //并没有找到\r\n，需要继续接收
    return LINE_OPEN;  //数据不完整
}   


// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn1::HTTP_CODE http_conn1::do_request(){
    // "/home/nowcoder/webserver/resources"
    //初始化的m_real_file赋值为网站根目录
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    //拼接资源/home/jwf/Desktop/webserver/resources/index.html
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if ( stat( m_real_file, &m_file_stat ) < 0 ) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if (!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if (S_ISDIR( m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open( m_real_file, O_RDONLY );
    //客户请求的目标文件被mmap到内存中的起始位置
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
  
}   


// 对内存映射区执行munmap操作，把内存映射释放掉
void http_conn1::unmap() {
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}

//写HTTP响应事件
bool http_conn1::write() { 
    int temp = 0;
    int bytes_have_send = 0;    // 已经发送的字节
    int bytes_to_send = m_write_idx;// 将要发送的字节 （m_write_idx）写缓冲区中待发送的字节数
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        modfd( m_epollfd, m_sockfd, EPOLLIN ); 
        init();
        return true;
    }

    while(1) {
        // 分散写，往多块分散内存一起写数据
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if ( bytes_to_send <= bytes_have_send ) {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();
            if(m_linger) {
                init();
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return true;
            } else {
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return false;
            } 
        }
    }
    return true;
} 



// 往写缓冲中写入待发送的数据
bool http_conn1::add_response( const char* format, ... ) {
    if( m_write_idx >= WRITE_BUFFER_SIZE ) { //当前写的索引大于写缓冲区，返回错误
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

//添加响应状态首行
bool http_conn1::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_conn1::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}

bool http_conn1::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn1::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn1::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_conn1::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_conn1::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}


// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn1::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        //内部错误，500
        case INTERNAL_ERROR:{
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        }
        case BAD_REQUEST:{      //报文语法有误，404
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        } 
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title );
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        default:
            return false;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}






//由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn1::process() {
    //解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        //请求不完整，需要继续读取客户数据
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return; //线程结束了，空闲了
    }
    
    //生成响应
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    //在监听能否往外面写
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}