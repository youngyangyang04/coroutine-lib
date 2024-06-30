#include "fiber_thread.h"
// 工作线程id从1开始 主线程为0
static thread_local std::atomic<int> s_working_thread_id{1};

// 构造函数
Thread::Thread(std::function<void()> cb, const std::string& name):
m_name(name) 
{
    m_thread_id = s_working_thread_id++;

    m_thread = std::thread([this, cb](){Scheduler::SetThreadId(this->m_thread_id); cb();});
}

// 析构函数
Thread::~Thread()
{
    if (m_thread.joinable()) 
    {
        m_thread.join();
    }
}

