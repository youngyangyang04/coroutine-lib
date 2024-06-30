#ifndef _IO_SCHEDULER_H_
#define _IO_SCHEDULER_H_

#include "scheduler.h"
#include <shared_mutex>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>

class Scheduler;

class IOManager : public Scheduler
{
public:
	// 读写事件的定义
		// 只有读和写事件，其他epoll事件会归类到这两类事件, ⽐如EPOLLRDHUP, EPOLLERR, EPOLLHUP等
	enum Event
	{
		// 无事件
		NONE  = 0x0,
		// 读事件 <-> EPOLLIN
		READ  = 0X1,
		// 写事件 <-> EPOLLOUT
		WRITE = 0X4
	};

	// fd上下文由三个部分组成，分别是描述符-事件类型-回调函数
		// fd有可读和可写两种事件，每种事件对应不同的回调函数
	struct FdContext
	{
		// 事件上下文，记录该事件的回调函数
		struct EventContext
		{
			// 调度器
			Scheduler* scheduler = nullptr;
			// 事件回调协程
			std::shared_ptr<Fiber> fiber;
			// 事件回调函数
			std::function<void()> cb;
		};

		EventContext handler;
		
		// fd
		int fd = 0;
		// 在此注册fd的关心的事件
			// 事件是用位掩码来记录的，不同位代表不同的事件，用一个Event变量记录即可
		Event events = NONE;
		// mutex
		std::mutex mutex;

		char buffer[256];
	};

private:
	// epoll_fd
	int m_epfd = 0;
	// pipe, fd[0]为读端, fd[1]为写端
	int m_tickleFds[2];
	// test RWMutexType
	std::shared_mutex m_mutex;
	// 记录fd上下文
	std::vector<FdContext*> m_fdContexts;

	int m_listen_fd;

public:
	IOManager(size_t threads, bool use_caller, const std::string &name);
	~IOManager();
	void tickle();
	void idle();

	void startup();

	void contextResize(int size);

	bool setEvent(int fd, Event event);

	bool delAllEvent(int fd);

	void doCommonProcess(int fd);

};

#endif