#include "scheduler.h"

static unsigned int test_number;

void task()
{
	std::cout << "task " << test_number << " is under processing in thread: " << Scheduler::GetThreadId() << std::endl;
	test_number ++;
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

int main(int argc, char const *argv[])
{
	std::cout << "begin test\n";
	{
		// 可以尝试把false 变为true 此时调度器所在线程也将加入工作线程
		std::shared_ptr<Scheduler> scheduler = std::make_shared<Scheduler>(3, true, "scheduler_1");
		
		scheduler->start();

		sleep(5);

		for(int i=0;i<20;i++)
		{
			std::shared_ptr<Fiber> fiber = std::make_shared<Fiber>(task);
			scheduler->scheduleLock_fiber(fiber);
		}
		// 所有工作线程开始运行
		scheduler->stop();
	}
	return 0;
}