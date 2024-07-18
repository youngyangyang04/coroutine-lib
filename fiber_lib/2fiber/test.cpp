#include "fiber.h"
#include <vector>

using namespace sylar; 

class Scheduler
{
public:
	// 添加协程调度任务
	void schedule(std::shared_ptr<Fiber> task)
	{
		m_tasks.push_back(task);
	}

	// 执行调度任务
	void run()
	{
		std::cout << " number " << m_tasks.size() << std::endl;

		std::shared_ptr<Fiber> task;
		auto it = m_tasks.begin();
		while(it!=m_tasks.end())
		{
				// 迭代器本身也是指针
			task = *it;
			// 由主协程切换到子协程，子协程函数运行完毕后自动切换到主协程
			task->resume();
			it++;
		}
		m_tasks.clear();
	}

private:
	// 任务队列
	std::vector<std::shared_ptr<Fiber>> m_tasks;
};

void test_fiber(int i)
{
	std::cout << "hello world " << i << std::endl;
}

int main()
{
	// 初始化当前线程的主协程
	Fiber::GetThis();

	// 创建调度器
	Scheduler sc;

	// 添加调度任务（任务和子协程绑定）
	for(auto i=0;i<20;i++)
	{
		// 创建子协程
			// 使用共享指针自动管理资源 -> 过期后自动释放子协程创建的资源
			// bind函数 -> 绑定函数和参数用来返回一个函数对象
		std::shared_ptr<Fiber> fiber = std::make_shared<Fiber>(std::bind(test_fiber, i), 0, false);
		sc.schedule(fiber);
	}

	// 执行调度任务
	sc.run();

	return 0;
}