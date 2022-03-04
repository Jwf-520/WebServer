#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h> //包含线程的头文件
#include <list>
#include "locker.h"
#include <exception>
#include <iostream>

//线程池类，定义成模板类是为了代码的复用性，模板参数T是任务类
template<typename T>
class threadpool{
public:
    //初始化线程的数量，等待任务请求数量10000
    threadpool(int thread_number = 100, int max_requests = 10000);
    ~threadpool();

    //往请求队列添加任务
    bool append(T* request);

private:
    //工作线程运行的函数，它不断从工作队列中取出任务并执行
    static void* worker(void* arg);
    void run();
private:
    //线程池中线程的数量
    int m_thread_number;

    //线程池数组，大小为m_thread_number
    pthread_t* m_threads;

    //请求队列中允许的最大请求数
    int m_max_requests;

    //请求队列
    std::list<T*> m_workerqueue;

    //保护请求队列的互斥锁
    locker m_queuelocker;

    //信号量用来判断是否有任务需要处理
    sem m_queuestat;

    //是否结束线程
    bool m_stop;
};
//线程池的构造函数
template<typename T> 
threadpool<T>::threadpool(int thread_number, int max_requests):
    m_thread_number(thread_number), m_max_requests(max_requests),
    m_stop(false), m_threads(NULL) {
        //参数错误，抛出错误
        if ((thread_number <= 0) || (max_requests <= 0)) {
            throw std::exception();
        }
        m_threads = new pthread_t[m_thread_number];
        if (!m_threads) {
            throw std::exception();
        }
        //创建thread_number个线程，并将它们设置为线程脱离(让这些线程自己释放资源)
        for (int i = 0; i < thread_number; i++) {
            std::cout << "create the " << i << "th" << " thread" << std::endl; 
            //c++中线程执行的回调函数必须是静态函数，c语言中worker是一个全局函数

            //重点！！！！
            //这里为什么传入this，this代表本对象，作为参数传递到worker函数当中，解决了worker静态函数无法访问非静态成员变量
            if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
                delete[] m_threads;
                throw std::exception();
            }
            //线程释放资源失败
            if (pthread_detach(m_threads[i]) != 0) {
                delete[] m_threads;
                throw std::exception();
            }

        }
}
//线程池的析构函数
template<typename T> 
threadpool<T>::~threadpool() {
    delete[] m_threads;
    m_stop = true;
}

//向任务队列添加任务
template<typename T> 
bool threadpool<T>::append(T* request) {
    //操作工作队列时一定要上锁，因为它被所有线程共享
    m_queuelocker.lock();
    //工作队列的任务数大于最大的等待请求，就解锁,任务添加工作队列失败
    if (m_workerqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workerqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();    //添加一个任务，信号量加1
    return true;
}
/*
1. 静态static成员函数不同于非静态函数，它只属于类本身，而不属于每一个对象实例。静态函数随着类的加载而独立存在。
与之相反的是非静态成员，他们当且仅当实例化对象之后才存在。也就是说，静态成员函数产生在前，非静态成员函数产生在后，不可能让静态函数去访问一个不存在的东西。
2. 在访问非静态变量的时候，用的是this指针；而static静态函数没有this指针，所以静态函数也确实没有办法访问非静态成员。
*/
template<typename T> 
void* threadpool<T>::worker(void* arg) {
    //传入的是this，this就是线程池指针
    //转换arg
    threadpool* pool = (threadpool*) arg;
    //创建好的线程池从工作队列取数据，做任务
    pool->run();
    return pool;
}

template<typename T> 
void threadpool<T>::run() {
    //线程池一直去循环
    while (!m_stop) {
        //如果信号量有值，就不会阻塞，如果没有值，就会在这阻塞，需要执行任务，信号量减1
        m_queuestat.wait();
        //走到这，说明有数据，上锁
        m_queuelocker.lock();
        if (m_workerqueue.empty()) {
            m_queuelocker.unlock();
            continue; //继续循环看这个队列有没有数据
        }

        //走到下面说明有数据
        //先获取第一个任务
        T* request = m_workerqueue.front();
        //删掉任务
        m_workerqueue.pop_front();
        m_queuelocker.unlock();
        //获取到request往下走，没获取到的话，再继续循环
        if (!request) {
            continue;
        }
        
        request->process(); //执行具体业务的函数
    }
}

#endif