#include "coroutine.h"
#include <iostream>

/*
1 ucontext_t 结构体记录协程的上下文
2 ucontext_t 结构体中的成员uc_stack 记录了这个协程的栈空间：用来保存协程在运行过程中函数调用所需的栈帧
*/

// 协程上下⽂结构体定义
// 这个结构体是平台相关的，因为不同平台的寄存器不⼀样，下⾯列出的是所有平台都⾄少会包含的4个成员
/*
typedef struct ucontext_t 
{
	// 当前上下⽂结束后，下⼀个激活的上下⽂对象的指针，由makecontext()设置
	struct ucontext_t *uc_link;
	
	// 当前上下⽂的信号屏蔽掩码
	sigset_t uc_sigmask;
	
	// 当前上下⽂使⽤的栈空间，由makecontext()设置
	stack_t uc_stack;
	
	// 平台相关的上下⽂具体内容，包含寄存器的值
	mcontext_t uc_mcontext;
	
	...
} ucontext_t;
*/

// 获取当前运行的协程的上下⽂
int getcontext(ucontext_t *ucp);

// 恢复ucp指向的上下⽂，
// 不会返回，⽽是会跳转到ucp上下⽂对应的入口函数中执⾏，相当于变相调⽤了函数
int setcontext(const ucontext_t *ucp);

// 修改由getcontext()获取到的上下⽂指针ucp，将其与⼀个函数func进⾏绑定，并指定func运⾏时的参数
// 在调⽤makecontext()之前，必须⼿动给ucp分配⼀段内存空间，存储在ucp->uc_stack中，作为该协程的栈空间
// 同时也可以指定ucp->uc_link，表示函数运⾏结束后恢复uc_link指向的上下⽂
// 如果不赋值uc_link，那func函数结束时必须调⽤setcontext()或swapcontext()以重新指定⼀个有效的上下⽂，否则程序就跑⻜了
// makecontext()执⾏完后，ucp就与函数func绑定了，调⽤setcontext()或swapcontext()激活ucp时，func就会被运⾏
void makecontext(ucontext_t *ucp, void (*func)(), int argc, ...);

// 恢复ucp指向的上下⽂，同时将当前的上下⽂存储到oucp中 -> 切换协程
// 不返回，⽽是会跳转到ucp上下⽂对应的函数中执⾏，相当于调⽤了函数
int swapcontext(ucontext_t *oucp, const ucontext_t *ucp);

// 一个线程包含多个协程 -> 以下线程局部变量记录一个线程的协程控制信息
// 当前线程的正在运行的协程
static thread_local Fiber* t_fiber = nullptr;

// 当前线程的主协程
static thread_local std::shared_ptr<Fiber> t_thread_fiber = nullptr;

// 当前线程的协程数量 
static thread_local int s_fiber_count = 0;

// 当前线程的协程id
static thread_local int s_fiber_id = 0;

void Fiber::SetThis(Fiber *f)
{
	t_fiber = f;
}

// 返回当前线程正在执⾏的协程
std::shared_ptr<Fiber> Fiber::GetThis()
{
	if(t_fiber)
	{	// 返回指向当前对象的shared_ptr
		return t_fiber->shared_from_this();
	}

	// 如果当前线程还未创建协程，则创建线程的主协程
		// Fiber()是私有，不能使用make_shared() -> std::shared_ptr<Fiber> main_fiber = std::make_shared<Fiber>();
	std::shared_ptr<Fiber> main_fiber(new Fiber());

	// 确认创建主协程的默认构造函数成功运行
		// .get() -> 智能指针对象获取原始指针
	assert(t_fiber == main_fiber.get());
	
	// 设置指向主协程的智能指针
	t_thread_fiber = main_fiber;
	
	return t_fiber->shared_from_this();
}

	// 仅由GetThis方法调用，定义为私有方法
	// 运行时首先调用GetThis
// 创建线程的主协程 -> 将负责调度子协程
Fiber::Fiber()
{
	// 设置当前正在运行的协程(即t_fiber的值)为此协程
	SetThis(this);
	m_state = RUNNING;
	
	// 获取当前协程的上下文
	if(getcontext(&m_ctx))
	{
		std::cerr << "Fiber() failed\n";
		exit(0);
	}
	
	// 更新当前线程的协程控制信息
	s_fiber_count ++;
	m_id = s_fiber_id++;
	std::cout << "Fiber(): main id = " << m_id << std::endl;
}

// 创建线程的子协程
Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler):
m_cb(cb), m_runInScheduler(run_in_scheduler)
{
	m_state = READY;

	// 子协程分配栈空间
	//m_stacksize = stacksize ? stacksize : g_fiber_stack_size->getValue();
	//m_stack = StackAllocator::Alloc(m_stacksize);
	m_stacksize = stacksize ? stacksize : 128000;
	m_stack = malloc(m_stacksize);

	// 获取当前协程的上下文
	if(getcontext(&m_ctx))
	{
		std::cerr << "Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler) failed\n";
		exit(0);
	}
	
	// 修改为子协程上下文
	m_ctx.uc_link = nullptr;
	m_ctx.uc_stack.ss_sp = m_stack;
	m_ctx.uc_stack.ss_size = m_stacksize;
	
	// 将子协程上下文和函数入口（方法函数指针 -> 要加&）绑定
	makecontext(&m_ctx, &Fiber::MainFunc, 0);
	
	// 更新当前线程的协程控制信息
	m_id = s_fiber_id++;
	s_fiber_count ++;
	std::cout << "Fiber(): child id = " << m_id << std::endl;
}

Fiber::~Fiber()
{
	if(m_stack)
	{
		free(m_stack);
	}
}

// 重置协程状态和入口函数，重用协程栈，不重新创建
void Fiber::reset(std::function<void()> cb)
{
	assert(m_stack != nullptr);
	// 为简化状态管理，强制只有TERM状态的协程才可以重置
	assert(m_state == TERM);
	
	m_cb = cb;
	m_state = READY;

	if(getcontext(&m_ctx))
	{
		std::cerr << "reset() failed\n";
		exit(0);
	}

	m_ctx.uc_link = nullptr;
	m_ctx.uc_stack.ss_sp = m_stack;
	m_ctx.uc_stack.ss_size = m_stacksize;

	makecontext(&m_ctx, &Fiber::MainFunc, 0);
}

// 在⾮对称协程⾥，执⾏resume时的当前执⾏协程⼀定是主协程或调度协程
// 将子协程切换到执行状态，和主协程或调度协程进行交换
void Fiber::resume()
{
	assert(m_state==READY);
	SetThis(this);
	m_state = RUNNING;

	// true -> 调度协程切换到子协程
	if(m_runInScheduler)
	{
		if(swapcontext(&(Scheduler::GetSchedulerFiber()->m_ctx), &m_ctx))
		{
			std::cerr << "resume() to GetSchedulerFiber failed\n";
			pthread_exit(NULL);
		}		
	}
	// false -> 主协程切换到调度协程
	else
	{
		if(swapcontext(&(t_thread_fiber->m_ctx), &m_ctx))
		{
			std::cerr << "resume() to t_thread_fiber failed\n";
			pthread_exit(NULL);
		}	
	}
}

// 执⾏yield时，当前执⾏协程⼀定是⼦协程
// 将子协程让出执行权，切换到主协程或调度协程
void Fiber::yield()
{
	// 协程运⾏完之后会⾃动yield⼀次，⽤于回到主协程，此时状态已为TERM
	assert(m_state==RUNNING || m_state==TERM);
	SetThis(t_thread_fiber.get());
	if(m_state!=TERM)
	{
		m_state = READY;
	}

	// true -> 子协程切换到调度协程
	if(m_runInScheduler)
	{

		if(swapcontext(&m_ctx, &(Scheduler::GetSchedulerFiber()->m_ctx)))
		{
			std::cerr << "yield() to to GetSchedulerFiber failed\n";
			pthread_exit(NULL);
		}		
	}
	// false -> 调度协程切换到主协程
	else
	{
		if(swapcontext(&m_ctx, &(t_thread_fiber->m_ctx)))
		{
			std::cerr << "yield() to t_thread_fiber failed\n";
			pthread_exit(NULL);
		}	
	}	
}

// 协程入口函数的封装 -> 1 调用入口函数 2 调用yield返回主协程
void Fiber::MainFunc()
{
	// 返回当前线程正在执行的协程 
		// shared_from_this()使引用计数加1
	std::shared_ptr<Fiber> curr = GetThis();
	assert(curr!=nullptr);

	// 调用真正的入口函数
	curr->m_cb(); 
	
	// 运行完毕修改协程状态
	curr->m_cb = nullptr;
	curr->m_state = TERM;

	auto raw_ptr = curr.get();
		// 智能指针使用reset()进行关闭
	curr.reset(); // 在yield之前 手动关闭共享指针curr -> 引用计数减1
	
	// 运行完毕 -> yield
	raw_ptr->yield(); 
}

