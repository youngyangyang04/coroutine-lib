#include "scheduler.h"

// 全局变量（线程局部变量）
// 调度器 -> 由同一个调度器下的所有线程共用
static thread_local Scheduler* t_scheduler = nullptr;
// 当前线程的调度协程 -> 每个线程独有一份，包括caller线程
	// 加上前⾯协程模块的t_fiber和t_thread_fiber，每个线程总共可以记录三个协程的上下⽂信息。
static thread_local Fiber* t_scheduler_fiber = nullptr;
// 当前线程的线程id 
// 主线程之外的线程将在创建工作线程后修改
static thread_local int s_thread_id = 0;

// 获取调度器指针
Scheduler* Scheduler::GetThis()
{
	return t_scheduler;
}

void Scheduler::setThis()
{
	t_scheduler = this;
}

Fiber* Scheduler::GetSchedulerFiber()
{
	return t_scheduler_fiber;
}

int Scheduler::GetThreadId()
{
	return s_thread_id;
}

void Scheduler::SetThreadId(int thread_id)
{
	s_thread_id = thread_id;
}

Scheduler::Scheduler(size_t threads, bool use_caller, const std::string &name):
m_useCaller(use_caller), m_name(name)
{
	assert(threads>0);
	assert(Scheduler::GetThis()==nullptr);	

	tickler = 0;

	// 设置调度器指针
	t_scheduler = this;

	// 调度器所在线程的id
	m_rootThread = 0;	

	// 主协程参与执行任务
	if(use_caller)
	{
		threads --;

		// 创建当前协程为主协程
		Fiber::GetThis();

		// 创建调度协程并设置到m_rootFiber
			// 调度协程第三个参数设置为false -> yield时，调度协程应该返回主协程
		m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false));

		// 设置调度协程指针
		t_scheduler_fiber = m_rootFiber.get();
		
		// 加入线程池的线程ID数组
		m_threadIds.push_back(m_rootThread);
	}

	// 还需要创建的额外线程
	m_threadCount = threads;
}

// 初始化调度线程池
	// 如果只使⽤caller线程进⾏调度，那这个⽅法啥也不做 
void Scheduler::start()
{
	std::cout << "Scheduler start() starts" << std::endl;
	std::lock_guard<std::mutex> lock(m_mutex);
	if(m_stopping)
	{
		std::cerr << "Scheduler is stopped" << std::endl;
		return;
	}

	assert(m_threads.empty());
	m_threads.resize(m_threadCount);
	for(size_t i=0;i<m_threadCount;i++)
	{
			// bind函数的函数对象如果是类方法，参数需要this指针
		// 创建线程 并运行调度函数 -> 该协程自动成为调度协程
		m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this), m_name + "_" + std::to_string(i)));
		// 添加到线程池的线程ID数组
		m_threadIds.push_back(m_threads[i]->getId());
	}
	std::cout << "Scheduler start() ends" << std::endl;
}

// 调度协程运行的函数 -> 不停地从任务队列取任务切换到子协程并执⾏ -> 结束时都会回到调度协程
// 当任务队列为空时 -> 代码切换到idle协程 -> idle协程啥也不做直接就yield了，状态还是READY状态 -> 这⾥其实就是个忙等待
// 只有当调度器检测到停⽌标志时，idle协程才会真正结束，调度协程也会检测到idle协程状态为TERM，并且随之退出整个调度协程
	// 这⾥还可以看出⼀点，对于⼀个任务协程，只要其从resume中返回了，那不管它的状态是 TERM还是READY，调度器都不会⾃动将其再次加⼊调度，因为前⾯说过，⼀个成熟的协程是要学会⾃我管理的。
void Scheduler::run()
{
	std::cout << "Schedule::run() starts in thread: " << GetThreadId() << std::endl;
	// 设置当前的协程调度器
	setThis();
	// 非调度器所在线程需要设置调度协程 此时和case_1情况相同 -> 调度协程就是主协程
	if(GetThreadId() != m_rootThread)
	{
		assert(t_scheduler_fiber==nullptr);
		// 创建主协程并将其设置到调度协程指针
		t_scheduler_fiber = Fiber::GetThis().get();
	}

	ScheduleTask task;
	while(true)
	{
		task.reset();
		// mutex范围
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_tasks.begin();
			// 1 遍历所有调度任务
			while(it!=m_tasks.end())
			{
				// 指定了调度线程，但不是在当前线程上调度，然后跳过这个任务，继续下⼀个
				if(it->thread!=-1&&it->thread!=GetThreadId())
				{
					it++;
					continue;
				}

				// 发现可执行的任务
				assert(it->fiber||it->cb);
				if(it->fiber)
				{
					assert(it->fiber->getState()==Fiber::READY);
				}

				// 2 取出任务
				task = *it;
				m_tasks.erase(it); 
				tickler --; // 剩余任务数-1
				m_activeThreadCount++;
				break;
			}		

		}


		// 4 执行任务
		// 任务为协程任务
		if(task.fiber)
		{
			task.fiber->resume();
			m_activeThreadCount--;
			task.reset();
		}
		// 任务为函数任务
		else if(task.cb)
		{
			// 使用任务协程绑定这个函数
			std::shared_ptr<Fiber> cb_fiber = std::make_shared<Fiber>(task.cb);
			cb_fiber->resume();
			m_activeThreadCount--;
			task.reset();	
			cb_fiber.reset();
		}
		// 未取出任务 -> 任务为空 -> 切换到idle协程
		else
		{		
			// 调度器已经关闭
			if(m_stopping==true)
			{
				break;
			}
			// 创建idle协程
			std::shared_ptr<Fiber> idle_fiber = std::make_shared<Fiber>(std::bind(&Scheduler::idle, this));
			// 运行idle函数
			m_idleThreadCount++;
			idle_fiber->resume();
			m_idleThreadCount--;
		}
	}
	std::cout << "Schedule::run() ends in thread: " << GetThreadId() << std::endl;
}

/*
当主线程（调度器线程）不参与调度时，即use_caller为false时，就必须要创建其他线程进⾏协程调度：
因为有单独的线程⽤于协程调度，那只需要让新线程的⼊⼝函数作为调度协程，从任务队列⾥取任务执⾏就⾏了，
main函数与调度协程完全不相关，main函数只需要向调度器添加任务，然后在适当的时机停⽌调度器即可。
当调度器停⽌时，main函数要等待调度线程结束后再退出。

当主线程也参与调度时，，即use_caller为true时，可以是多线程，也可以是单线程，多线程时调度线程的协程切
换如上，那主线程和单线程的时候协程是怎样切换的呢：
代码
使⽤了caller线程作为工作线程 -> 调度器依赖stop⽅法来执⾏caller线程的调度协程
仅使⽤caller线程来调度 -> 那调度器真正开始执⾏调度的位置就是这个stop⽅法
*/

void Scheduler::stop()
{
	std::cout << "Schedule::stop() starts" << std::endl;
	if(stopping())
	{
		return;
	}
	
	// 和原文修改，只能由调度器所在线程线程发起stop
	assert(GetThreadId()==m_rootThread);

	// 所有线程开始工作
	tickle();

	// 不再添加新任务 -> 当任务为0时不在进行idle
	m_stopping = true;

	// 调度器所在线程开始处理任务
		// 在use caller情况下，调度器协程结束时，应该返回caller协程
	if(m_rootFiber)
	{
		m_rootFiber->resume();
		std::cout << "m_rootFiber ends" << std::endl;
	}

	std::vector<std::shared_ptr<Thread>> thrs;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
			// 交换vector中的内容
		thrs.swap(m_threads);
	}
	// 等待所有额外创建的线程结束
	for(auto &i : thrs)
	{
		i->join();
	}
	std::cout << "Schedule::stop() ends" << std::endl;
}

void Scheduler::tickle()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	tickler = m_tasks.size();
}

// 当任务数为0时一直在睡眠 -> tickle之后修改任务数 -> 工作线程开始工作
void Scheduler::idle()
{
	do
	{
		std::cout << "sleeping in thread: " << GetThreadId() << std::endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(3000));
	}while(tickler==0);
	tickler --;
	return;
}

void Scheduler::scheduleLock_fiber(std::shared_ptr<Fiber> fc, int thread_id)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	ScheduleTask task;
	
	task.fiber = fc;
	task.cb = nullptr;
	task.thread = thread_id;

	m_tasks.push_back(task);	
}

void Scheduler::scheduleLock_cb(std::function<void()> fc, int thread_id)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	ScheduleTask task;

	task.fiber = nullptr;
	task.cb = fc;
	task.thread = thread_id;	

	m_tasks.push_back(task);	
}

