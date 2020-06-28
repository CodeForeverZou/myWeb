#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <pthread.h>

#include </var/tmp/git_re/practice/web/httpWeb/locker.h>

template <typename T>
class threadPool
{
private:
    static void* worker(void* arg); // 线程工作函数
    void run();

    int m_thread_number;    // 线程数量
    int m_max_requests;     // 同时处理的最大请求
    pthread_t* m_threads;   // 线程池 保存线程信息
    std::list<T*> m_workqueue;      // 工作队列
    locker m_queuelocker;   // 互斥锁
    sem m_queuestat;        // 信号量
    bool m_stop;
public:
    threadPool(int threadNumber = 8, int maxRequests = 10000);
    ~threadPool();
    bool append(T* request);
};

// 构造
template <typename T>
threadPool<T>::threadPool(int threadNumber, int maxRequests){
    if((threadNumber <= 0) || (maxRequests <= 0)) throw std::exception();
    
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads) throw std::exception();
    
    for (int i = 0; i < threadNumber; i++)
    {
        printf("create the %dth thread\n", i);
        // 依次创建 threadNumber个 线程
        if(pthread_create(m_threads + i, NULL, worker, this) != 0){
            delete [] m_threads;
            throw std::exception();
        }
        // 独立线程
        if(pthread_detach(m_threads[i])){
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template<typename T> 
threadPool<T>::~threadPool(){
    delete [] m_threads;
    m_stop = true;
}

template<typename T>
bool threadPool<T>::append(T* request){
    // 添加到请求队列，需要互斥访问
    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_requests){
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    // V一下可用队列
    m_queuestat.V();
    return true;
}

template<typename T>
void* threadPool<T>::worker(void* arg){
    threadPool* pool = (threadPool*) arg;
    pool->run();
    return pool;
}

// 真正的工作函数
template<typename T>
void threadPool<T>::run(){
    while (!m_stop)
    {
        // 用之前 P一下
        m_queuestat.P();
        // 互斥访问 请求队列
        m_queuelocker.lock();
        if (m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        // 出队
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if(!request) continue;
        request->process();     // 调用模板 对应的执行函数
    }
}

#endif