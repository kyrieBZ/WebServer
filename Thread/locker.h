#ifndef LOCKER_H
#define LOCKER_H

#include<pthread.h>
#include<exception>
#include<semaphore.h>

//线程同步机制封装类

//互斥锁类
class Locker{
    public:
    Locker(){
        //初始化互斥锁成功返回0，失败返回非零值
        if(pthread_mutex_init(&m_mutex,nullptr)!=0){
            throw std::exception();
        }
    }

    ~Locker(){
        pthread_mutex_destroy(&m_mutex);
    }

    //注意：同样的，当加锁/解锁成功会返回0，失败返回非零值

    //加锁
    bool lock(){
        return pthread_mutex_lock(&m_mutex)==0;
    }

    //解锁
    bool unlock(){
        return pthread_mutex_unlock(&m_mutex)==0;
    }

    //获取互斥锁
    pthread_mutex_t *getMutex(){
        return &m_mutex;
    }


    private:
    pthread_mutex_t m_mutex;//互斥锁

};

//条件变量类
class Condition{
    public:
    Condition(){
        if(pthread_cond_init(&m_condition,NULL)!=0){
            throw std::exception();
        }
    }

    ~Condition(){
        pthread_cond_destroy(&m_condition);
    }

    bool wait(pthread_mutex_t *mutex){
        return pthread_cond_wait(&m_condition,mutex)==0;
    }

    bool timeWait(pthread_mutex_t *mutex,struct timespec time){
        return pthread_cond_timedwait(&m_condition,mutex,&time)==0;
    }

    //唤醒一个或多个
    bool signal(pthread_mutex_t *mutex){
        return pthread_cond_signal(&m_condition)==0;
    }

    //全部唤醒
    bool broadcast(){
        return pthread_cond_broadcast(&m_condition)==0;
    }

    private:
    pthread_cond_t m_condition;//控制线程的条件变量
};

//信号量类
class Semaphore{
    public:
    Semaphore(){
        if(sem_init(&m_semaphore,0,0)!=0){
            throw std::exception();
        }
    }

    Semaphore(int num){
        if(sem_init(&m_semaphore,num,num)!=0){
            throw std::exception();
        }
    }

    ~Semaphore(){
        sem_destroy(&m_semaphore);
    }

    //阻塞信号量
    bool wait(){
        return sem_wait(&m_semaphore)==0;
    }

    //释放信号量
    bool post(){
        return sem_post(&m_semaphore);
    }


    private:
    sem_t m_semaphore;

};



#endif