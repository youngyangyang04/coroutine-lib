#include "timer.h"

std::shared_ptr<int> Timer::s_default_object = std::make_shared<int>(1);

Timer::Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager):
m_ms(ms), m_cb(cb), m_recurring(recurring), m_manager(manager)
{
	// 获取当前时间
    auto now = std::chrono::system_clock::now();
    // 计算出超时的时间戳
    m_next = now + std::chrono::milliseconds(m_ms);

    m_weak_cond = s_default_object;
}

bool Timer::refresh()
{
	// 获取当前时间
    auto now = std::chrono::system_clock::now();
    // 计算出超时的时间戳
    m_next = now + std::chrono::milliseconds(m_ms);	
    return true;
}

	
bool Timer::reset(uint64_t ms, bool from_now)
{
	auto now = std::chrono::system_clock::now();
	if(from_now)
	{
		if(ms>m_ms)
		{
			m_next += std::chrono::milliseconds(ms-m_ms);	
		}
		else
		{
			m_next -= std::chrono::milliseconds(m_ms-ms);
		}
		
	}
	else
	{
		m_next = now + std::chrono::milliseconds(ms);
	}
	m_ms = ms;
	return true;
}




std::shared_ptr<Timer> TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recurring)
{
	std::shared_ptr<Timer> temp;
	temp.reset(new Timer(ms, cb, recurring, this));
	addTimer(temp);
	return temp->shared_from_this();
}

	
std::shared_ptr<Timer> TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring)
{
	std::shared_ptr<Timer> temp;
	temp.reset(new Timer(ms, cb, recurring, this));
	temp->m_weak_cond = weak_cond;
	addTimer(temp);
	return temp->shared_from_this();
}


uint64_t TimerManager::getNextTime()
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    if (!m_timers.empty())
    {
        auto time = m_timers.top()->m_next;
        auto now = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(time - now).count();
        if (duration > 0)
        {
            return static_cast<uint64_t>(duration);
        }
    }
    return 0;
}

void TimerManager::listExpiredCb(std::vector<std::function<void()>>& cbs)
{
	std::unique_lock<std::shared_mutex> write_lock(m_mutex);
	auto now = std::chrono::system_clock::now();

	while(!m_timers.empty()&&m_timers.top()->m_next<now)
	{
		std::shared_ptr<Timer> temp = m_timers.top();
		m_timers.pop();
		// 可以执行
		if(!temp->m_canceled&&!temp->m_weak_cond.expired())
		{
			cbs.push_back(temp->m_cb);
			if(temp->m_recurring)
			{
				// 重新加入minheap
				temp->refresh();
				m_timers.push(temp);
			}
		}
	}
	// 上次检查时间
	m_previousTime = std::chrono::system_clock::now();
}

bool TimerManager::hasTimer()
{
	std::shared_lock<std::shared_mutex> lock(m_mutex);
	return m_timers.size()>0;
}

// 插⼊到Timer集合的⾸部时，TimerManager通过该⽅法来通知IOManager⽴刻更新当前的epoll_wait超时。
// TimerManager还负责检测是否发⽣了校时，由detectClockRollover⽅法实现
// 再io中将被重载 方法是 立刻tickle() wakeup epoll_wait 走完一轮循环后 会再次设置睡眠时间 进入epoll_wait
void TimerManager::onTiemrInsertedAtFront()
{
	
}

	// 将定时器添加到管理器中
void TimerManager::addTimer(std::shared_ptr<Timer> val)
{
	std::unique_lock<std::shared_mutex> write_lock(m_mutex);
	if(m_timers.empty()||val->m_next<m_timers.top()->m_next)
	{
		onTiemrInsertedAtFront();
	}
	m_timers.push(val);
}

// 检测服务器时间是否被回调 -> 如果往前调整了 那时间就会过很久才能触发
// 用这个机制检查是否有调整 -> 有调整就把现在所有的timer都触发一遍
// sylar一种很原始的实现方式
bool TimerManager::detectClockRollover(uint64_t now_ms)
{
	auto now = std::chrono::system_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_previousTime).count();
	return duration> 60 * 60 * 1000;
}