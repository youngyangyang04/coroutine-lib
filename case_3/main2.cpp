#include "io_scheduler.h"

int main(int argc, char const *argv[])
{
	{
		// 启动shcheduler
		IOManager server(3, true, "scheduler");
		sleep(2);

		// 启动工作线程
		server.start();
		sleep(2);	

		// 启动服务器
		server.startup();
		sleep(15);

		// 越界 服务器关闭 处理完当前事件时候全部关闭
	}	
	sleep(5);
	return 0;
}