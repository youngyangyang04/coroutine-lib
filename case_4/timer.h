#include <memory>       // std::shared_ptr, std::weak_ptr, std::make_shared
#include <functional>   // std::function
#include <chrono>       // std::chrono::system_clock, std::chrono::time_point, std::chrono::milliseconds
#include <queue>        // std::priority_queue
#include <vector>       // std::vector
#include <shared_mutex> // std::shared_mutex, std::shared_lock
#include <mutex>


/*
最⼩堆很适合处理这种定时⽅案，将所有定时器按最⼩堆来组织，可以很⽅便地获取到当前的最⼩超时时间
sylar的定时器采⽤最⼩堆设计，所有定时器根据绝对的超时时间点进⾏排序，每次取出离当前时间最近的⼀个超时
时间点，计算出超时需要等待的时间，然后等待超时。超时时间到后，获取当前的绝对时间点，然后把最⼩堆⾥超
时时间点⼩于这个时间点的定时器都收集起来，执⾏它们的回调函数。

注意，在注册定时事件时，⼀般提供的是相对时间，⽐如相对当前时间3秒后执⾏。sylar会根据传⼊的相对时间和
当前的绝对时间计算出定时器超时时的绝对时间点，然后根据这个绝对时间点对定时器进⾏最⼩堆排序。因为依赖
的是系统绝对时间，所以需要考虑校时因素，这点会在后⾯讨论。

sylar定时器的超时等待基于epoll_wait，精度只⽀持毫秒级，因为epoll_wait的超时精度也只有毫秒级。
关于定时器和IO协程调度器的整合。IO协程调度器的idle协程会在调度器空闲时阻塞在epoll_wait上，等待IO事件
发⽣。在之前的代码⾥，epoll_wait具有固定的超时时间，这个值是5秒钟。加⼊定时器功能后，epoll_wait的超时
时间改⽤当前定时器的最⼩超时时间来代替。epoll_wait返回后，根据当前的绝对时间把已超时的所有定时器收集
起来，执⾏它们的回调函数。

由于epoll_wait的返回并不⼀定是超时引起的，也有可能是IO事件唤醒的，所以在epoll_wait返回后不能想当然地
假设定时器已经超时了，⽽是要再判断⼀下定时器有没有超时，这时绝对时间的好处就体现出来了，通过⽐较当前
的绝对时间和定时器的绝对超时时间，就可以确定⼀个定时器到底有没有超时。
*/

class TimerManager;

// timer -> 实现协程调度器对定时任务的调度 -> 服务器上经常要处理定时事件，⽐如3秒后关闭⼀个连接，或是定期检测⼀个客户端的连接状态
class Timer : public std::enable_shared_from_this<Timer>
{
	// 该类可以访问所有private的成员
	friend class TimerManager;

public:
	// 刷新定时器的执行时间
	bool refresh();
	// 重置定时器时间
		// ms 定时器执行间隔时间， from_now是否从当前时间开始计算
	bool reset(uint64_t ms, bool from_now);

	bool cancel()
	{
		m_canceled = true;
		return true;
	}

	// Timer的构造函数被定义成私有⽅式，只能通过TimerManager类来创建Timer对象
private:
	// 构造函数
		// ms 定时器执行间隔时间, cb 回调函数, recurring 是否重复执行, manager 定时器管理器
	Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager);

private:
	// 执行周期
	uint64_t m_ms;
	// 回调函数
	std::function<void()> m_cb;
	// 定时器是否循环
	bool m_recurring;	
	// 定时器管理器
	TimerManager* m_manager;

	// 执行的精确时间戳
	std::chrono::time_point<std::chrono::system_clock> m_next;	
	
	// 是否被删除
	bool m_canceled = false;
	// 条件对象
    std::weak_ptr<void> m_weak_cond;

    // 该指针指向一个永久存在的对象 给那些不需要额外检查对象是否存在的Timer
    static std::shared_ptr<int> s_default_object;

private:
	// 仿函数 -> ⽤于⽐较两个Timer对象，⽐较的依据是绝对超时时间
		// 最小堆的比较函数
	struct Comparator
	{
		bool operator() (const std::shared_ptr<Timer>& lhs, const std::shared_ptr<Timer>& rhs) const
		{
			return lhs->m_next > rhs->m_next;
		}
	};
};

/*

，TimerManager包含⼀个std::set类型的Timer集合，这个集合
就是定时器的最⼩堆结构，因为set⾥的元素总是排序过的，所以总是可以很⽅便地获取到当前的最⼩定时器。
TimerManager提供创建定时器，获取最近⼀个定时器的超时时间，以及获取全部已经超时的定时器回调函数的⽅
法，并且提供了⼀个onTimerInsertedAtFront()⽅法，这是⼀个虚函数，由IOManager继承时实现，当新的定时器
插⼊到Timer集合的⾸部时，TimerManager通过该⽅法来通知IOManager⽴刻更新当前的epoll_wait超时。
TimerManager还负责检测是否发⽣了校时，由detectClockRollover⽅法实现。
*/

// 直接将超时时间当作tick周期，每次都取出所有定时器中超时时间最⼩的超时值作为⼀个tick
// ⼀旦tick触发, 处理完已超时的定时器后，再从剩余的定时器中找出超时时间最⼩的⼀个, 并将这个最⼩时间作为下⼀个tick

// 所有的Timer对象都由TimerManager类进⾏管理
class TimerManager
{
	friend class Timer;

public:
	TimerManager() 
	{
		m_previousTime = std::chrono::system_clock::now();
	};
	virtual ~TimerManager() {};

	// 添加定时器
	std::shared_ptr<Timer> addTimer(uint64_t ms, std::function<void()> cb, bool recurring = false);

	// 添加条件定时器
	std::shared_ptr<Timer> addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring = false);

	// 拿到定时器中超时时间最⼩的超时值
	uint64_t getNextTime();

	// 获取需要执⾏的定时器的回调函数数组
	void listExpiredCb(std::vector<std::function<void()>>& cbs);

	// 是否有定时器存在
	bool hasTimer();


protected:
	// 当有新的定时器插⼊到定时器的⾸部时 -> 执⾏该函数
	virtual void onTiemrInsertedAtFront();

	// 将定时器添加到管理器中
	void addTimer(std::shared_ptr<Timer> val);

	// 检测服务器时间是否被调后了
	bool detectClockRollover();

private:
	// mutex
	std::shared_mutex m_mutex;
	// 最小堆
	std::priority_queue<std::shared_ptr<Timer>, std::vector<std::shared_ptr<Timer>>, Timer::Comparator> m_timers;
	// 是否触发onTiemrInsertedAtFront
	bool m_tickled = false;
	// 上次执行时间
	std::chrono::time_point<std::chrono::system_clock> m_previousTime;

public:
	void refreshAllTimer();
};


/*

1. 创建定时器时只传入了相对超时时间，内部要先进行转换，根据当前时间把相对时间转化成绝对时间。

2. sylar支持创建条件定时器，也就是在创建定时器时绑定一个变量，在定时器触发时判断一下该变量是否存在如果变量已被释放，那就取消触发。

3. 关于onTimerInsertedAtFront()方法的作用。这个方法是IOManager提供给TimerManager使用的，
当TimerManager检测到新添加的定时器的超时时间比当前最小的定时器还要小时，
TimerManager通过这个方法来通知IOManager立刻更新当前的epoll_wait超时，
否则新添加的定时器的执行时间将不准确。
实际实现时，只需要在onTimerInsertedAtFront()方法内执行一次tickle就行了，
tickle之后，epoll_wait会立即退出，并重新从TimerManager中获取最近的超时时间，
这时拿到的超时时间就是新添加的最小定时器的超时时间了。

4. 每次超时之后除了要检查有没有要触发的定时器，还顺便检查一下系统时间有没有被往回调。
如果系统时间往回调了1个小时以上，那就触发全部定时器。
个人感觉这个办法有些粗糙，其实只需要换个时间源就可以解决校时问题，
换成clock_gettime(CLOCK_MONOTONIC_RAW)的方式获取系统的单调时间，就可以解决这个问题了
*/