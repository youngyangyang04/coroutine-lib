#include "thread.h"

#include <sys/syscall.h> 
#include <iostream>
#include <unistd.h>  

namespace sylar {

// 线程信息
static thread_local Thread* t_thread          = nullptr;
static thread_local std::string t_thread_name = "UNKNOWN";

pid_t Thread::GetThreadId()
{
	return syscall(SYS_gettid);
}

Thread* Thread::GetThis()
{
    return t_thread;
}

const std::string& Thread::GetName() 
{
    return t_thread_name;
}

void Thread::SetName(const std::string &name) 
{
    if (t_thread) 
    {
        t_thread->m_name = name;
    }
    t_thread_name = name;
}

Thread::Thread(std::function<void()> cb, const std::string &name): 
m_cb(cb), m_name(name) 
{
    int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);
    if (rt) 
    {
        std::cerr << "pthread_create thread fail, rt=" << rt << " name=" << name;
        throw std::logic_error("pthread_create error");
    }
    // 等待线程函数完成初始化
    m_semaphore.wait();
}

Thread::~Thread() 
{
    if (m_thread) 
    {
        pthread_detach(m_thread);
        m_thread = 0;
    }
}

void Thread::join() 
{
    if (m_thread) 
    {
        int rt = pthread_join(m_thread, nullptr);
        if (rt) 
        {
            std::cerr << "pthread_join failed, rt = " << rt << ", name = " << m_name << std::endl;
            throw std::logic_error("pthread_join error");
        }
        m_thread = 0;
    }
}

void* Thread::run(void* arg) 
{
    Thread* thread = (Thread*)arg;

    t_thread       = thread;
    t_thread_name  = thread->m_name;
    thread->m_id   = GetThreadId();
    pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());

    std::function<void()> cb;
    cb.swap(thread->m_cb); // swap -> 可以减少m_cb中只能指针的引用计数
    
    // 初始化完成
    thread->m_semaphore.signal();

    cb();
    return 0;
}

} 

