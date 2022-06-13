#include "heap_timer.h"
#include "../http/http_conn.h"

time_heap::time_heap(int cap):capacity(cap), cur_size(0) {
    array = new heap_timer*[capacity];
    if(!array) throw std::exception();
    for(int i = 0; i < capacity; ++i) array[i] = nullptr;
}


time_heap::time_heap(heap_timer** init_array, int size, int capacity):cur_size(size), capacity(capacity) {
    if(capacity < size) throw std::exception();
    array = new heap_timer* [capacity];
    if(!array) throw std::exception();
    for(int i = 0; i < capacity; ++i) array[i] = nullptr;
    if(size != 0) {
        for(int i = 0; i < size; ++i) array[i] = init_array[i];
        for(int i = (cur_size - 1) / 2; i >= 0; --i) heapify(i);
    }
}

time_heap::~time_heap() {
    for(int i = 0; i < cur_size; ++i) delete array[i];
    delete[] array;
}

void time_heap::add_timer(heap_timer* timer) {
    if(!timer) return;
    //堆大小不够，扩容。
    if(cur_size >= capacity) resize();
    int hole = cur_size++;
    int parent = 0;
    while (hole > 0) {
        parent = (hole - 1) / 2;
        if (array[parent]->expire <= timer->expire) break;
        array[hole] = array[parent];
        hole = parent;
    }
    array[hole] = timer; 
}
void time_heap::del_timer(heap_timer* timer) {
    if(!timer) return;
    timer->cb_func = nullptr;
}
heap_timer* time_heap::top() const {
    if(empty()) return nullptr;
    return array[0];
}

void time_heap::pop_timer() {
    if(empty()) return;
    if(array[0]) {
        delete array[0];
        array[0] = array[--cur_size];
        heapify(0);
    }
}

void time_heap::tick() {
    heap_timer* tmp = array[0];
    time_t cur = time(nullptr);
    while(!empty()){
        if(!tmp) break;
        if(tmp->expire > cur) break;
        if(array[0]->cb_func) array[0]->cb_func(array[0]->user_data);
        pop_timer();
        tmp = array[0];
    }
}

bool time_heap::empty() const {
    return cur_size == 0;
}

void time_heap::heapify(int hole) {
    int t = hole;
    if (2 * hole + 1 < cur_size && array[2 * hole + 1]->expire < array[t]->expire) t = 2 * hole + 1; 
    if (2 * hole + 2 < cur_size && array[2 * hole + 2]->expire < array[t]->expire) t = 2 * hole + 2; 
    if (t != hole) {
        swap(array[t], array[hole]);
        heapify(t);
    }
}

void time_heap::resize() {
    heap_timer** temp = new heap_timer* [2 * capacity];
    for(int i = 0; i < 2 * capacity; ++i) temp[i] = nullptr;
    if(!temp) throw std::exception();
    capacity = 2 * capacity;
    for(int i = 0; i < cur_size; ++i) temp[i] = array[i];
    delete[] array;
    array = temp;
}

void Utils::init(int timeslot) {
    m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig) {
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}




int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;


class Utils;
void cb_func(client_data *user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}









