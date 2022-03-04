#ifndef LOCKER_H
#define LOCKER_H
/*实现多线程同步，通过锁机制，确保任一时刻只能有一个线程能进入关键代码段*/

#include <pthread.h> //包含互斥锁的头文件
#include <exception> //包含异常对象的头文件
#include <semaphore.h> //包含信号量的头文件
//线程同步机制封装类

//互斥锁类
class locker {
public:
    locker() {
        //锁构建失败的处理
        if (pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }
    ~locker() {
        pthread_mutex_destroy(&m_mutex);
    }
    //上锁
    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    //解锁
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    //获取互斥量的成员
    pthread_mutex_t* get() {
        return &m_mutex;
    }
private:
    pthread_mutex_t m_mutex;

};

//条件变量类：主要判断队列中是否有数据，没数据就让线程停在那，有数据唤醒线程往下面去操作。
class cond {
public:
    cond() {
        if (pthread_cond_init(&m_cond, NULL) != 0) {
            throw std::exception();
        }
    }
    ~cond() {
        pthread_cond_destroy(&m_cond);
    }

    //条件变量配合互斥锁使用的
    bool wait(pthread_mutex_t* mutex) {
        return pthread_cond_wait(&m_cond, mutex) == 0;
    }
    //带超时时间
    bool timedwait(pthread_mutex_t* mutex, struct timespec t) {
        return pthread_cond_timedwait(&m_cond, mutex, &t) == 0;
    }
    //让一个或多个线程唤醒
    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }
    //将所有线程唤醒
    bool broadcast(pthread_mutex_t* mutex) {
        return pthread_cond_broadcast(&m_cond) == 0;
    }
private:
    pthread_cond_t m_cond;

};

//信号量类
class sem {
public:
    sem() {
        if (sem_init(&m_sem, 0 ,0) != 0) {
            throw std::exception();
        }
    }

    sem(int num) {
        if (sem_init(&m_sem, 0 ,num) != 0) {
            throw std::exception();
        }
    }

    ~sem() {
        sem_destroy(&m_sem);
    }
    //等待信号量
    bool wait() {
        return sem_wait(&m_sem) == 0;
    }
    //增加信号量
    bool post() {
        return sem_post(&m_sem) == 0;
    }
private:
    sem_t m_sem;
};

#endif
