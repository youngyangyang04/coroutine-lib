#ifndef _FIBER_THREAD_H_
#define _FIBER_THREAD_H_

#include "scheduler.h"
#include <functional>
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <iostream>

class Scheduler;

class Thread : public std::enable_shared_from_this<Thread>
{
public:
    Thread(std::function<void()> cb, const std::string& name);
    ~Thread();

    int getId() const
    {
        return m_thread_id;
    }

    void join()
    {
    	if (m_thread.joinable()) 
    	{
       		m_thread.join();
    	}
    	std::cout << "thread: " << m_thread_id << " is finished\n";
    }

private:
    // 线程名称
    std::string m_name;
    // 线程id
    int m_thread_id;
    // 线程对象
    std::thread m_thread;
};


#endif // _FIBER_THREAD_H_
