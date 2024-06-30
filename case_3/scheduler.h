#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include "coroutine.h"
#include "fiber_thread.h"
#include <mutex>
#include <vector>

class Fiber;

class Thread;

class Scheduler
{
public:
	// 创建调度器
		// threads 线程数, use_caller 调度器线程是否也用来执行任务, name 名称
	Scheduler(size_t threads = 1, bool use_caller = true, const std::string& name="Scheduler");
	// 析构函数
	virtual ~Scheduler();
	
	// 获取调度器名称
	const std::string& getName() const {return m_name;}
	
	// 获取调度器指针
	static Scheduler* GetThis();
	// 设置当前的协程调度器
	void SetThis();
	
	// 获取调度协程指针
		// 原文为static Fiber *GetMainFiber(); 为避免歧义做了如下更改
	static Fiber* GetSchedulerFiber();
	
	// 添加调度任务
		//  thread 指定运⾏该任务的线程，-1表示任意线程
		// 原文使用的是模板 这里使用重载
	void scheduleLock(std::shared_ptr<Fiber> fc, int thread_id=-1);
	void scheduleLock(std::function<void()> fc, int thread_id=-1);
	
	// 获取当前的线程号
	static int GetThreadId();
	// 新线程创建时设置线程号
	static void SetThreadId(int thread_id);

	// 启动调度器
	virtual void start();
	// 停止调度器 -> 所有调度任务都执行完了之后返回
	virtual void stop();

	// 有新任务 -> 通知调度协程开始执行
	virtual void tickle();

protected:
	// 调度协程入口函数函数 
		// 持有this指针 也可以访问到protected的成员函数
	virtual void run();
	
	// 无任务调度时执行idle协程
	virtual void idle();
	
	// 返回是否可以停止
	virtual bool stopping() {return m_stopping;}

	// 返回是否有空闲线程 -> 当调度协程进入idle时空闲线程数加1，从idle协程返回时空闲线程数减1
	bool hasIdleThreads() {return m_idleThreadCount>0;}

private:
	// 调度任务，协程/函数⼆选⼀，可指定在哪个线程上调度
	struct ScheduleTask
	{
		std::shared_ptr<Fiber> fiber;
		std::function<void()> cb;
		int thread;

		ScheduleTask()
		{
			fiber = nullptr;
			cb = nullptr;
			thread = -1;
		}

		ScheduleTask(std::shared_ptr<Fiber> f, int thr)
		{
			fiber = f;
			thread = thr;
		}

		ScheduleTask(std::shared_ptr<Fiber>* f, int thr)
		{
			fiber.swap(*f);
			thread = thr;
		}	

		ScheduleTask(std::function<void()> f, int thr)
		{
			cb = f;
			thread = thr;
		}		

		void reset()
		{
			fiber = nullptr;
			cb = nullptr;
			thread = -1;
		}	
	};

private:
	// 协程调度器名称
	std::string m_name;
	// 互斥锁
	std::mutex m_mutex;
	// 线程池
	std::vector<std::shared_ptr<Thread>> m_threads;
	// 任务队列
	std::vector<ScheduleTask> m_tasks;
	// 线程池的线程ID数组
	std::vector<int> m_threadIds;
	// 工作线程的数量，不包含use_caller主线程
	size_t m_threadCount = 0;
	// 活跃的线程数
	std::atomic<size_t> m_activeThreadCount = {0};
	// idle线程数
	std::atomic<size_t> m_idleThreadCount = {0};

	// 是否使用caller线程执行任务
	bool m_useCaller;
	// use_caller为true时，调度器所在线程的调度协程 -> 必须在类内持有 不然创建完就会被释放
	std::shared_ptr<Fiber> m_rootFiber;
	// 调度器所在线程的id
	int m_rootThread;

	// 是否正在停止

	// 用于唤醒在idle协程中的线程
	std::atomic<int> tickler;

protected:
	bool m_stopping = false;	
};

#endif