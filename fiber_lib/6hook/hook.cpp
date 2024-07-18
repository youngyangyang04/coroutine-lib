#include "hook.h"
#include "ioscheduler.h"
#include <dlfcn.h>
#include <iostream>
#include <cstdarg>
#include "fd_manager.h"
#include <string.h>

// apply XX to all functions
#define HOOK_FUN(XX) \
    XX(sleep) \
    XX(usleep) \
    XX(nanosleep) \
    XX(socket) \
    XX(connect) \
    XX(accept) \
    XX(read) \
    XX(readv) \
    XX(recv) \
    XX(recvfrom) \
    XX(recvmsg) \
    XX(write) \
    XX(writev) \
    XX(send) \
    XX(sendto) \
    XX(sendmsg) \
    XX(close) \
    XX(fcntl) \
    XX(ioctl) \
    XX(getsockopt) \
    XX(setsockopt) 

namespace sylar{

// if this thread is using hooked function 
static thread_local bool t_hook_enable = false;

bool is_hook_enable()
{
    return t_hook_enable;
}

void set_hook_enable(bool flag)
{
    t_hook_enable = flag;
}

void hook_init()
{
	static bool is_inited = false;
	if(is_inited)
	{
		return;
	}

	// test
	is_inited = true;


// assignment -> sleep_f = (sleep_fun)dlsym(RTLD_NEXT, "sleep"); -> dlsym -> fetch the original symbols/function
#define XX(name) name ## _f = (name ## _fun)dlsym(RTLD_NEXT, #name);
	HOOK_FUN(XX)
#undef XX
}

// static variable initialisation will run before the main function
struct HookIniter
{
	HookIniter()
	{
		hook_init();
	}
};

static HookIniter s_hook_initer;

} // end namespace sylar

struct timer_info 
{
    int cancelled = 0;
};

// universal template for read and write function
template<typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, const char* hook_fun_name, uint32_t event, int timeout_so, Args&&... args) 
{
    if(!sylar::t_hook_enable) 
    {
        return fun(fd, std::forward<Args>(args)...);
    }

    std::shared_ptr<sylar::FdCtx> ctx = sylar::FdMgr::GetInstance()->get(fd);
    if(!ctx) 
    {
        return fun(fd, std::forward<Args>(args)...);
    }

    if(ctx->isClosed()) 
    {
        errno = EBADF;
        return -1;
    }

    if(!ctx->isSocket() || ctx->getUserNonblock()) 
    {
        return fun(fd, std::forward<Args>(args)...);
    }

    // get the timeout
    uint64_t timeout = ctx->getTimeout(timeout_so);
    // timer condition
    std::shared_ptr<timer_info> tinfo(new timer_info);

retry:
	// run the function
    ssize_t n = fun(fd, std::forward<Args>(args)...);
    
    // EINTR ->Operation interrupted by system ->retry
    while(n == -1 && errno == EINTR) 
    {
        n = fun(fd, std::forward<Args>(args)...);
    }
    
    // 0 resource was temporarily unavailable -> retry until ready 
    if(n == -1 && errno == EAGAIN) 
    {
        sylar::IOManager* iom = sylar::IOManager::GetThis();
        // timer
        std::shared_ptr<sylar::Timer> timer;
        std::weak_ptr<timer_info> winfo(tinfo);

        // 1 timeout has been set -> add a conditional timer for canceling this operation
        if(timeout != (uint64_t)-1) 
        {
            timer = iom->addConditionTimer(timeout, [winfo, fd, iom, event]() 
            {
                auto t = winfo.lock();
                if(!t || t->cancelled) 
                {
                    return;
                }
                t->cancelled = ETIMEDOUT;
                // cancel this event and trigger once to return to this fiber
                iom->cancelEvent(fd, (sylar::IOManager::Event)(event));
            }, winfo);
        }

        // 2 add event -> callback is this fiber
        int rt = iom->addEvent(fd, (sylar::IOManager::Event)(event));
        if(rt) 
        {
            std::cout << hook_fun_name << " addEvent("<< fd << ", " << event << ")";
            if(timer) 
            {
                timer->cancel();
            }
            return -1;
        } 
        else 
        {
            sylar::Fiber::GetThis()->yield();
     
            // 3 resume either by addEvent or cancelEvent
            if(timer) 
            {
                timer->cancel();
            }
            // by cancelEvent
            if(tinfo->cancelled == ETIMEDOUT) 
            {
                errno = tinfo->cancelled;
                return -1;
            }
            goto retry;
        }
    }
    return n;
}



extern "C"{

// declaration -> sleep_fun sleep_f = nullptr;
#define XX(name) name ## _fun name ## _f = nullptr;
	HOOK_FUN(XX)
#undef XX

// only use at task fiber
unsigned int sleep(unsigned int seconds)
{
	if(!sylar::t_hook_enable)
	{
		return sleep_f(seconds);
	}

	std::shared_ptr<sylar::Fiber> fiber = sylar::Fiber::GetThis();
	sylar::IOManager* iom = sylar::IOManager::GetThis();
	// add a timer to reschedule this fiber
	iom->addTimer(seconds*1000, [fiber, iom](){iom->scheduleLock(fiber, -1);});
	// wait for the next resume
	fiber->yield();
	return 0;
}

int usleep(useconds_t usec)
{
	if(!sylar::t_hook_enable)
	{
		return usleep_f(usec);
	}

	std::shared_ptr<sylar::Fiber> fiber = sylar::Fiber::GetThis();
	sylar::IOManager* iom = sylar::IOManager::GetThis();
	// add a timer to reschedule this fiber
	iom->addTimer(usec/1000, [fiber, iom](){iom->scheduleLock(fiber);});
	// wait for the next resume
	fiber->yield();
	return 0;
}

int nanosleep(const struct timespec* req, struct timespec* rem)
{
	if(!sylar::t_hook_enable)
	{
		return nanosleep_f(req, rem);
	}	

	int timeout_ms = req->tv_sec*1000 + req->tv_nsec/1000/1000;

	std::shared_ptr<sylar::Fiber> fiber = sylar::Fiber::GetThis();
	sylar::IOManager* iom = sylar::IOManager::GetThis();
	// add a timer to reschedule this fiber
	iom->addTimer(timeout_ms, [fiber, iom](){iom->scheduleLock(fiber, -1);});
	// wait for the next resume
	fiber->yield();	
	return 0;
}

int socket(int domain, int type, int protocol)
{
	if(!sylar::t_hook_enable)
	{
		return socket_f(domain, type, protocol);
	}	

	int fd = socket_f(domain, type, protocol);
	if(fd==-1)
	{
		std::cerr << "socket() failed:" << strerror(errno) << std::endl;
		return fd;
	}
	sylar::FdMgr::GetInstance()->get(fd, true);
	return fd;
}

int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeout_ms) 
{
    if(!sylar::t_hook_enable) 
    {
        return connect_f(fd, addr, addrlen);
    }

    std::shared_ptr<sylar::FdCtx> ctx = sylar::FdMgr::GetInstance()->get(fd);
    if(!ctx || ctx->isClosed()) 
    {
        errno = EBADF;
        return -1;
    }

    if(!ctx->isSocket()) 
    {
        return connect_f(fd, addr, addrlen);
    }

    if(ctx->getUserNonblock()) 
    {

        return connect_f(fd, addr, addrlen);
    }

    // attempt to connect
    int n = connect_f(fd, addr, addrlen);
    if(n == 0) 
    {
        return 0;
    } 
    else if(n != -1 || errno != EINPROGRESS) 
    {
        return n;
    }

    // wait for write event is ready -> connect succeeds
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    std::shared_ptr<sylar::Timer> timer;
    std::shared_ptr<timer_info> tinfo(new timer_info);
    std::weak_ptr<timer_info> winfo(tinfo);

    if(timeout_ms != (uint64_t)-1) 
    {
        timer = iom->addConditionTimer(timeout_ms, [winfo, fd, iom]() 
        {
            auto t = winfo.lock();
            if(!t || t->cancelled) 
            {
                return;
            }
            t->cancelled = ETIMEDOUT;
            iom->cancelEvent(fd, sylar::IOManager::WRITE);
        }, winfo);
    }

    int rt = iom->addEvent(fd, sylar::IOManager::WRITE);
    if(rt == 0) 
    {
        sylar::Fiber::GetThis()->yield();

        // resume either by addEvent or cancelEvent
        if(timer) 
        {
            timer->cancel();
        }

        if(tinfo->cancelled) 
        {
            errno = tinfo->cancelled;
            return -1;
        }
    } 
    else 
    {
        if(timer) 
        {
            timer->cancel();
        }
        std::cerr << "connect addEvent(" << fd << ", WRITE) error";
    }

    // check out if the connection socket established 
    int error = 0;
    socklen_t len = sizeof(int);
    if(-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len)) 
    {
        return -1;
    }
    if(!error) 
    {
        return 0;
    } 
    else 
    {
        errno = error;
        return -1;
    }
}


static uint64_t s_connect_timeout = -1;
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	return connect_with_timeout(sockfd, addr, addrlen, s_connect_timeout);
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
	int fd = do_io(sockfd, accept_f, "accept", sylar::IOManager::READ, SO_RCVTIMEO, addr, addrlen);	
	if(fd>=0)
	{
		sylar::FdMgr::GetInstance()->get(fd, true);
	}
	return fd;
}

ssize_t read(int fd, void *buf, size_t count)
{
	return do_io(fd, read_f, "read", sylar::IOManager::READ, SO_RCVTIMEO, buf, count);	
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
	return do_io(fd, readv_f, "readv", sylar::IOManager::READ, SO_RCVTIMEO, iov, iovcnt);	
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
	return do_io(sockfd, recv_f, "recv", sylar::IOManager::READ, SO_RCVTIMEO, buf, len, flags);	
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen)
{
	return do_io(sockfd, recvfrom_f, "recvfrom", sylar::IOManager::READ, SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);	
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
	return do_io(sockfd, recvmsg_f, "recvmsg", sylar::IOManager::READ, SO_RCVTIMEO, msg, flags);	
}

ssize_t write(int fd, const void *buf, size_t count)
{
	return do_io(fd, write_f, "write", sylar::IOManager::WRITE, SO_SNDTIMEO, buf, count);	
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
	return do_io(fd, writev_f, "writev", sylar::IOManager::WRITE, SO_SNDTIMEO, iov, iovcnt);	
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
	return do_io(sockfd, send_f, "send", sylar::IOManager::WRITE, SO_SNDTIMEO, buf, len, flags);	
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen)
{
	return do_io(sockfd, sendto_f, "sendto", sylar::IOManager::WRITE, SO_SNDTIMEO, buf, len, flags, dest_addr, addrlen);	
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
	return do_io(sockfd, sendmsg_f, "sendmsg", sylar::IOManager::WRITE, SO_SNDTIMEO, msg, flags);	
}

int close(int fd)
{
	if(!sylar::t_hook_enable)
	{
		return close_f(fd);
	}	

	std::shared_ptr<sylar::FdCtx> ctx = sylar::FdMgr::GetInstance()->get(fd);

	if(ctx)
	{
		auto iom = sylar::IOManager::GetThis();
		if(iom)
		{	
			iom->cancelAll(fd);
		}
		// del fdctx
		sylar::FdMgr::GetInstance()->del(fd);
	}
	return close_f(fd);
}

int fcntl(int fd, int cmd, ... /* arg */ )
{
  	va_list va; // to access a list of mutable parameters

    va_start(va, cmd);
    switch(cmd) 
    {
        case F_SETFL:
            {
                int arg = va_arg(va, int); // Access the next int argument
                va_end(va);
                std::shared_ptr<sylar::FdCtx> ctx = sylar::FdMgr::GetInstance()->get(fd);
                if(!ctx || ctx->isClosed() || !ctx->isSocket()) 
                {
                    return fcntl_f(fd, cmd, arg);
                }
                // 用户是否设定了非阻塞
                ctx->setUserNonblock(arg & O_NONBLOCK);
                // 最后是否阻塞根据系统设置决定
                if(ctx->getSysNonblock()) 
                {
                    arg |= O_NONBLOCK;
                } 
                else 
                {
                    arg &= ~O_NONBLOCK;
                }
                return fcntl_f(fd, cmd, arg);
            }
            break;

        case F_GETFL:
            {
                va_end(va);
                int arg = fcntl_f(fd, cmd);
                std::shared_ptr<sylar::FdCtx> ctx = sylar::FdMgr::GetInstance()->get(fd);
                if(!ctx || ctx->isClosed() || !ctx->isSocket()) 
                {
                    return arg;
                }
                // 这里是呈现给用户 显示的为用户设定的值
                // 但是底层还是根据系统设置决定的
                if(ctx->getUserNonblock()) 
                {
                    return arg | O_NONBLOCK;
                } else 
                {
                    return arg & ~O_NONBLOCK;
                }
            }
            break;

        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
        case F_SETFD:
        case F_SETOWN:
        case F_SETSIG:
        case F_SETLEASE:
        case F_NOTIFY:
#ifdef F_SETPIPE_SZ
        case F_SETPIPE_SZ:
#endif
            {
                int arg = va_arg(va, int);
                va_end(va);
                return fcntl_f(fd, cmd, arg); 
            }
            break;


        case F_GETFD:
        case F_GETOWN:
        case F_GETSIG:
        case F_GETLEASE:
#ifdef F_GETPIPE_SZ
        case F_GETPIPE_SZ:
#endif
            {
                va_end(va);
                return fcntl_f(fd, cmd);
            }
            break;

        case F_SETLK:
        case F_SETLKW:
        case F_GETLK:
            {
                struct flock* arg = va_arg(va, struct flock*);
                va_end(va);
                return fcntl_f(fd, cmd, arg);
            }
            break;

        case F_GETOWN_EX:
        case F_SETOWN_EX:
            {
                struct f_owner_exlock* arg = va_arg(va, struct f_owner_exlock*);
                va_end(va);
                return fcntl_f(fd, cmd, arg);
            }
            break;

        default:
            va_end(va);
            return fcntl_f(fd, cmd);
    }	
}

int ioctl(int fd, unsigned long request, ...)
{
    va_list va;
    va_start(va, request);
    void* arg = va_arg(va, void*);
    va_end(va);

    if(FIONBIO == request) 
    {
        bool user_nonblock = !!*(int*)arg;
        std::shared_ptr<sylar::FdCtx> ctx = sylar::FdMgr::GetInstance()->get(fd);
        if(!ctx || ctx->isClosed() || !ctx->isSocket()) 
        {
            return ioctl_f(fd, request, arg);
        }
        ctx->setUserNonblock(user_nonblock);
    }
    return ioctl_f(fd, request, arg);
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
	return getsockopt_f(sockfd, level, optname, optval, optlen);
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    if(!sylar::t_hook_enable) 
    {
        return setsockopt_f(sockfd, level, optname, optval, optlen);
    }

    if(level == SOL_SOCKET) 
    {
        if(optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) 
        {
            std::shared_ptr<sylar::FdCtx> ctx = sylar::FdMgr::GetInstance()->get(sockfd);
            if(ctx) 
            {
                const timeval* v = (const timeval*)optval;
                ctx->setTimeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
            }
        }
    }
    return setsockopt_f(sockfd, level, optname, optval, optlen);	
}

}
