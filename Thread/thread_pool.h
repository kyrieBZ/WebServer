#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include <list>
#include <exception>
#include <cstdio>
#include "locker.h"

// 线程池类，定义为模板类以实现代码复用
template<typename T>
class ThreadPool {
public:
    // 构造函数
    ThreadPool(int threadNum = 8, int maxRequest = 10000) {
        if((threadNum <= 0) || (maxRequest <= 0)) {
            throw std::exception();
        }

        m_thread_num = threadNum;
        m_max_request = maxRequest;
        m_stop = false;
        m_threads = new pthread_t[m_thread_num];
        
        if(!m_threads) {
            throw std::exception();
        }

        // 创建thread_num个线程并设置线程脱离
        for(int i = 0; i < m_thread_num; ++i) {
            printf("create the %dth thread\n", i+1);

            if(pthread_create(m_threads + i, NULL, worker, this) != 0) {
                delete [] m_threads;
                throw std::exception();
            }

            if(pthread_detach(m_threads[i]) != 0) {
                delete [] m_threads;
                throw std::exception();
            }
        }
    }

    // 析构函数
    ~ThreadPool() {
        delete [] m_threads;
        m_stop = true;
    }

    // 添加任务
    bool addTask(T* request) {
        m_queue_locker.lock();
        if(m_work_queue.size() > static_cast<size_t>(m_max_request)) {
            m_queue_locker.unlock();
            return false;
        }

        m_work_queue.push_back(request);
        m_queue_locker.unlock();
        m_queue_start.post();
        return true;
    }

private:
    // 子线程执行函数
    static void* worker(void *arg) {
        ThreadPool *pool = (ThreadPool*)arg;
        pool->run();
        return pool;
    }

    // 线程池运行函数
    void run() {
        while(!m_stop) {
            m_queue_start.wait(); 
            m_queue_locker.lock();
            
            if(m_work_queue.empty()) {
                m_queue_locker.unlock();
                continue;
            }

            T *request = m_work_queue.front();
            m_work_queue.pop_front();
            m_queue_locker.unlock();

            if(!request) {
                continue;
            }
            //处理请求
            request->process();
        }
    }

private:
    int m_thread_num;            // 线程数量
    pthread_t *m_threads;        // 线程池数组
    int m_max_request;           // 请求队列最大容量
    std::list<T*> m_work_queue;  // 请求队列
    Locker m_queue_locker;       // 互斥锁
    Semaphore m_queue_start;         // 信号量
    bool m_stop;                 // 是否结束线程
};

#endif