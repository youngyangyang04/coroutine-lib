#include "io_scheduler.h"
#include <algorithm>

// 不需要可以关闭部分debug信息
static int debug = 1;

		// 此处涉及边沿触发模式 
		// epoll 只在文件描述符从不可读/不可写变为可读/可写时（文件描述符的状态发生了变化），触发一次事件
		// 因此，必须在事件触发时一次性处理所有可读/可写的数据
		// 如果你没有读取所有数据，那么下次 epoll_wait 不会再次通知你

		// 此处涉及非nonblock io -> 配合epoll使用

		/* epoll 定义好的结构体
		typedef union epoll_data 
		{
		    void        *ptr;     // Pointer to user-defined data 
		    int          fd;      // File descriptor 
		    uint32_t     u32;     // 32-bit unsigned integer 
		    uint64_t     u64;     // 64-bit unsigned integer 
		} epoll_data_t;

		struct epoll_event 
		{
		    uint32_t events;    // Epoll events 
		    epoll_data_t data;  // User data variable 
		};
		*/

// 构造函数 -> 初始化管道和epollfd
IOManager::IOManager(size_t threads, bool use_caller, const std::string &name):
Scheduler(threads, use_caller, name)
{
	// 创建epollfd
	m_epfd = epoll_create(5000);
	assert(m_epfd>0);

	// 初始化pipe, m_tickleFds[0]为读端, m_tickleFds[1]为写端
	int rt = pipe(m_tickleFds);
	assert(!rt);

	// 注册pipe读端的读事件(EPOLLET边沿触发)
	epoll_event event;
	event.events = EPOLLIN | EPOLLET;
	event.data.fd = m_tickleFds[0];

	// 设置为非阻塞，配合epoll
	rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);
	assert(!rt);

	// 将事件加入epoll监听集合 -> 如果管道可读，会从idle()中的epoll_wait()返回
	rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
	assert(!rt);

	contextResize(32);

	std::cout << "IOManager::IOManager succeeded\n";
}

// 开启服务器
void IOManager::startup()
{
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(listen_fd<0)
	{
		perror("socket");
		exit(1);
	}

	// 设置fd重用
	int opt = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	// 服务器端地址
	struct sockaddr_in server_address;
	server_address.sin_port = htons(8080);
	server_address.sin_family  = AF_INET;
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);

	int rt = bind(listen_fd, (struct sockaddr *)&server_address, sizeof(server_address));
	if(rt<0)
	{
		perror("bind");
		exit(1);		
	}

	rt = listen(listen_fd, 5);
	if(rt<0)
	{
		perror("listen");
		exit(1);			
	}
	
	// 注册listen_fd的读事件(EPOLLET边沿触发)
	epoll_event event;
	event.events = EPOLLIN | EPOLLET;
	event.data.fd = listen_fd;

	// 2 设置listen_fd为非阻塞
	rt = fcntl(listen_fd, F_SETFL, O_NONBLOCK);
	assert(!rt);

	// 3 将事件加入epoll监听集合 
	rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, listen_fd, &event);
	assert(!rt);

	m_listen_fd = listen_fd;

	std::cout << "IOManager::startup succeeded\n";
}

// 调整fdcontext数组大小
	// 这里不需要锁 外面会上锁
void IOManager::contextResize(int size)
{
	while(m_fdContexts.size()<size)
	{
		m_fdContexts.push_back(new FdContext());
	}
}

// 有任务需要调度时tickle() -> 通过pipe写 -> idle协程从epoll_wait中退出 -> run()可以调度其他任务
	// 重载父类tickler()方法
void IOManager::tickle()
{
	std::cout << "IOManager::tickle() in thread: " << GetThreadId() << std::endl;
	if(!hasIdleThreads())
	{
		return;
	}
	// 有空闲线程 -> 发送数据使之从epoll_wait中返回 -> 进而退出idle，回到run()的开头位置 -> 调度其他任务
	int rt = write(m_tickleFds[1], "T", 1);
	assert(rt==1);
}

// idle函数会在多线程下操作，但是线程安全的，因为epoll_wait是线程安全的，如果一个事件在一个线程是就绪并取出，那他就不会出现在其他线程的epoll
// 阻塞在等待IO事件上,idle退出的时机使epoll_wait返回，对应的操作使tickle()和注册的IO事件就绪
	// 重载父类的idle方法
void IOManager::idle()
{
	// ⼀次epoll_wait最多检测256个就绪事件，如果就绪事件超过了这个数，那么会在下轮epoll_wati继续处理
	std::unique_ptr<epoll_event[]> events(new epoll_event[256]);

	// 服务器尚未关闭
	while(!m_stopping)
	{
		std::cout << " IOManager::idle() resume, sleeping in thread: " << GetThreadId() << std::endl;

		// 1 开始监听事件集合, 阻塞在epoll_wait上，等待事件的发生或计时器到期
		uint64_t wait_time = getNextTime();
		if(wait_time==0)
		{	// 如果没有计时器
			wait_time = (uint64_t)5000;
		}

		wait_time = std::min((uint64_t)5000, wait_time);

		int rt = epoll_wait(m_epfd, events.get(), 256, wait_time);
		if(rt<0)
		{
			// EINTR错误 -> 忽略 -> 重新epoll_wait()
			if(errno==EINTR)
			{
				continue;
			}
			// 其他错误
			std::cerr << "epoll_wait on " << m_epfd << " failed, errno: " << errno << std::endl;
			break;
		}

		// 如果系统时间被修改
		if(detectClockRollover())
		{
			// 修改现在所有计时器的时间
			refreshAllTimer();
		}

		// 将所有超时计时器的handler函数加入任务队列
		std::vector<std::function<void()>> cbs;
		listExpiredCb(cbs);
		for(auto cb : cbs)
		{
			scheduleLock(cb);	
		}

		// 2 遍历所有就绪事件
		for(int i=0;i<rt;i++)
		{
				// unique_ptr数组支持[]运算符
			epoll_event& event = events[i];
			
			//  事件来自ticklefd[0] -> ⽤于唤醒调度协程
			if(event.data.fd==m_tickleFds[0])
			{
				if(debug) std:: cout << "!!!!!!!!!!!!!!pipe event fd = " << event.data.fd << std::endl;
				uint8_t dummy[256];
				// 这时只需要把管道⾥的内容读完即可，本轮idle结束Scheduler::run会重新执⾏协程调度
				while(read(m_tickleFds[0], dummy, sizeof(dummy))>0);
				continue;
			}

			// 新连接
			if(event.data.fd==m_listen_fd&&!m_stopping)
			{
				if(debug) std:: cout << "!!!!!!!!!!!!!!new fd = " << event.data.fd << std::endl;
				// 建立新连接fd
				struct sockaddr_in client_addr;
				socklen_t len = sizeof(client_addr);
				int fd = accept(m_listen_fd, (struct sockaddr *)&client_addr, &len);

				// 设置为非阻塞
				int rt = fcntl(fd, F_SETFL, O_NONBLOCK);
				assert(!rt);

				// 当相同新连接使用相同fd时，会重置之前fd对应的fdcontext
				FdContext* fd_ctx = nullptr;
					// 上读锁 -> 使用c++17 shared_mutex
				std::shared_lock<std::shared_mutex> read_lock(m_mutex);
				// fdcontext存在
				if((int)m_fdContexts.size()>fd)
				{
					fd_ctx = m_fdContexts[fd];
					read_lock.unlock();
				}
				// 不存在 
				else
				{
					read_lock.unlock();
					// 上写锁
					std::unique_lock<std::shared_mutex> write_lock(m_mutex);
					contextResize(fd*1.5);
					fd_ctx = m_fdContexts[fd];
					write_lock.unlock();
				}

				{
					std::lock_guard<std::mutex> lock(fd_ctx->mutex);
					// 为新fd重新创建协程，之前的协程被智能指针自动释放
					std::shared_ptr<Fiber> fiber = std::make_shared<Fiber>(std::bind(&IOManager::doCommonProcess, this, fd));
					// 重置fdcontext
					fd_ctx->reset(fd, fiber);
					// 为该fd加入一个计时器 如果长时间为响应将自动关闭
					fd_ctx->m_timer = addTimer(5000, std::bind(&IOManager::closefd, this, fd), false);
				}

				setEvent(fd, READ);
				continue;
			}

			// 其他fd上的事件 -> 将该fd的协程加入调度队列 -> 要执⾏的话还要等idle协程退出
			FdContext* fd_ctx = (FdContext*)event.data.ptr; 
			if(debug) std:: cout << "!!!!!!!!!!!!!!fd = " << fd_ctx->fd << ", event :" << fd_ctx->events << std::endl; 
  			scheduleLock(fd_ctx->handler.fiber);		
		} // end for()

 		// 3 处理完所有事件 -> idle协程yield，这样可以让调度协程(Scheduler::run)重新检查是否有协程任务要执行
 		std::cout << " IOManager::idle() yields, sleeping in thread: " << GetThreadId() << std::endl;
		std::shared_ptr<Fiber> curr = Fiber::GetThis();
		auto raw_ptr = curr.get();
		curr.reset(); 
		raw_ptr->yield(); 

	} // end while()
	std::cout << " IOManager::idle() stops, sleeping in thread: " << GetThreadId() << std::endl;
}

IOManager::~IOManager()
{
	stop();
	std::cout << "~IOManager()\n";
	close(m_listen_fd);
	close(m_epfd);
	close(m_tickleFds[0]);
	close(m_tickleFds[1]);

	for(size_t i=0;i<m_fdContexts.size();i++)
	{
		if(m_fdContexts[i])
		{
			delete m_fdContexts[i];
		}
	}
}

// 设置监听读事件 或者 写事件 同一时间只监听一种事件
bool IOManager::setEvent(int fd, Event event)
{
	assert(event==READ||event==WRITE);

	// 1 获取fdcontext
	FdContext* fd_ctx = nullptr;
	// 上读锁 -> 使用c++17 shared_mutex
	std::shared_lock<std::shared_mutex> read_lock(m_mutex);
	// fdcontext存在
	if((int)m_fdContexts.size()>fd)
	{
		fd_ctx = m_fdContexts[fd];
		read_lock.unlock();
	}
	// 不存在 
	else
	{
		read_lock.unlock();
		std::cerr << "There is no FdContext, fd = " << fd << ", on epollfd = " << m_epfd << std::endl;
		return false;
	}

	std::lock_guard<std::mutex> lock(fd_ctx->mutex);
	// 对⼀个fd不允许重复添加相同的事件
	if((fd_ctx->events&event)!=0)
	{
		std::cerr << "The event is alread set, fd = " << fd << ", on epollfd = " << m_epfd << std::endl;
		return false;
	}

	// 2 将事件加入监听集合
	int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
	epoll_event epevent;
	epevent.events = EPOLLET | event; // 边沿触发模式
	epevent.data.ptr = fd_ctx;

	int rt = epoll_ctl(m_epfd, op, fd, &epevent);
	if(rt)
	{
		std::cerr << "setEvent epoll_ctl() failed, fd = " << fd << ", on epollfd = " << m_epfd << ": " << strerror(errno) << std::endl;
		return false;
	}

	// 3 修改fdcontext
	fd_ctx->events = event;
	return true;
}

// 删除fd上的所有事件
bool IOManager::delAllEvent(int fd)
{
	// 1 获取fdcontext
	FdContext* fd_ctx = nullptr;
	// 上读锁 -> 使用c++17 shared_mutex
	std::shared_lock<std::shared_mutex> read_lock(m_mutex);
	// fdcontext存在
	if((int)m_fdContexts.size()>fd)
	{
		fd_ctx = m_fdContexts[fd];
		read_lock.unlock();
	}
	// 不存在 
	else
	{
		std::cerr << "There is no FdContext, fd = " << fd << ", on epollfd = " << m_epfd << std::endl;
		read_lock.unlock();
		return false;
	}

	std::lock_guard<std::mutex> lock(fd_ctx->mutex);
	// fd上没有任何事件
	if(fd_ctx->events==NONE)
	{
		std::cerr << "There is no events listening, fd = " << fd << ", on epollfd = " << m_epfd << std::endl;
		return false;
	}	

	// 2 删除全部事件 
	int op = EPOLL_CTL_DEL;
	epoll_event epevent;
	epevent.events = 0;
	epevent.data.ptr = fd_ctx;

	int rt = epoll_ctl(m_epfd, op, fd, &epevent);
	if(rt)
	{
		std::cerr << "delAllEvent epoll_ctl() failed, fd = " << fd << ", on epollfd = " << m_epfd << ": " << strerror(errno) << std::endl;
		return false;
	}

	// 3 修改fdcontext
	fd_ctx->events = NONE;
	return true;
}

// http服务器或其他情况 在这个函数修改 -> 对每个fd执行的标准协程
	// 读写循环 协程的好处就是可以用一个函数解决所有问题
	// 目前实现的是简单的echo服务 -> 主要实现的读写的流程
void IOManager::doCommonProcess(int fd)
{	
	std::shared_lock<std::shared_mutex> read_lock(m_mutex);
	FdContext* fd_ctx = m_fdContexts[fd];
	char* buffer = fd_ctx->buffer;
	read_lock.unlock();
	
	while(true) // 该范围无锁
	{

		if(debug) std::cout << "读事件进入 fd = " << fd << ", " << buffer << std::endl;

		// 一旦进入协程 删掉正在监听的读任务 只在需要时加入监听集合
		delAllEvent(fd);
		int ret;
		int total = 0;

		// 1 读
		do 
		{
		    ret = read(fd, buffer + total, sizeof(buffer) - total);
		    if (ret > 0) 
		    {
		        total += ret;
		    }
		} while (ret > 0);

		// 需要关闭链接的情况
	    if (ret == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) 
	    {
	        closefd(fd);
	        return;
	    }

		buffer[total] = '\0';

		// 收到信息 -> 将之前的timer去掉 加入新的timer
		auto sp = fd_ctx->m_timer.lock(); // 尝试获取一个 shared_ptr
		if (sp) 
		{
		    sp->cancel(); // 如果成功获取到 shared_ptr，则调用 cancel()
		} 
		fd_ctx->m_timer = addTimer(5000, std::bind(&IOManager::closefd, this, fd), false);

		// 2 进行http协议解析

		// 再次进入时 将由写事件触发
		setEvent(fd, WRITE);
		std::shared_ptr<Fiber> curr_fiber = Fiber::GetThis();
		auto raw_ptr = curr_fiber.get();
		curr_fiber.reset(); 
		raw_ptr->yield();

		if(debug) std::cout << "写事件进入 fd = " << fd << ", " << buffer << std::endl;
		// 3 写
		ret = write(fd, buffer, total);
	    if (ret == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) 
	    {
	        closefd(fd);
	        return;
	    }

	    // 再次进入时 将由读事件触发
	    setEvent(fd, READ);
		std::shared_ptr<Fiber> curr_fiber2 = Fiber::GetThis();
		auto raw_ptr2 = curr_fiber2.get();
		curr_fiber2.reset(); 
		raw_ptr2->yield();				
	} // end while(true)
}

void IOManager::onTiemrInsertedAtFront()
{
	tickle();
}

// 删除监听事件并关闭fd
void IOManager::closefd(int fd)
{
	if(fd)
	{
		epoll_event epevent;
		epevent.events = 0;
		int rt = epoll_ctl(m_epfd, EPOLL_CTL_DEL, fd, &epevent);
		if(rt)
		{
			std::cerr << "delAllEvent epoll_ctl() failed, fd = " << fd << ", on epollfd = " << m_epfd << ": " << strerror(errno) << std::endl;
		}
		close(fd);		
	}
}