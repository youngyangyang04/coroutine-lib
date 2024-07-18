#include "timer.h"

namespace sylar {

bool Timer::cancel() 
{
    std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mutex);

    if(m_cb == nullptr) 
    {
        return false;
    }
    else
    {
        m_cb = nullptr;
    }

    auto it = m_manager->m_timers.find(shared_from_this());
    if(it!=m_manager->m_timers.end())
    {
        m_manager->m_timers.erase(it);
    }
    return true;
}

// refresh 只会向后调整
bool Timer::refresh() 
{
    std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mutex);

    if(!m_cb) 
    {
        return false;
    }

    auto it = m_manager->m_timers.find(shared_from_this());
    if(it==m_manager->m_timers.end())
    {
        return false;
    }

    m_manager->m_timers.erase(it);
    m_next = std::chrono::system_clock::now() + std::chrono::milliseconds(m_ms);
    m_manager->m_timers.insert(shared_from_this());
    return true;
}

bool Timer::reset(uint64_t ms, bool from_now) 
{
    if(ms==m_ms && !from_now)
    {
        return true;
    }

    {
        std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mutex);
    
        if(!m_cb) 
        {
            return false;
        }
        
        auto it = m_manager->m_timers.find(shared_from_this());
        if(it==m_manager->m_timers.end())
        {
            return false;
        }   
        m_manager->m_timers.erase(it); 
    }

    // reinsert
    auto start = from_now ? std::chrono::system_clock::now() : m_next - std::chrono::milliseconds(m_ms);
    m_ms = ms;
    m_next = start + std::chrono::milliseconds(m_ms);
    m_manager->addTimer(shared_from_this()); // insert with lock
    return true;
}

Timer::Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager):
m_recurring(recurring), m_ms(ms), m_cb(cb), m_manager(manager) 
{
    auto now = std::chrono::system_clock::now();
    m_next = now + std::chrono::milliseconds(m_ms);
}

bool Timer::Comparator::operator()(const std::shared_ptr<Timer>& lhs, const std::shared_ptr<Timer>& rhs) const
{
    assert(lhs!=nullptr&&rhs!=nullptr);
    return lhs->m_next < rhs->m_next;
}

TimerManager::TimerManager() 
{
    m_previouseTime = std::chrono::system_clock::now();
}

TimerManager::~TimerManager() 
{
}

std::shared_ptr<Timer> TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recurring) 
{
    std::shared_ptr<Timer> timer(new Timer(ms, cb, recurring, this));
    addTimer(timer);
    return timer;
}

// 如果条件存在 -> 执行cb()
static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb)
{
    std::shared_ptr<void> tmp = weak_cond.lock();
    if(tmp)
    {
        cb();
    }
}

std::shared_ptr<Timer> TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring) 
{
    return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
}

uint64_t TimerManager::getNextTimer()
{
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    
    // reset m_tickled
    m_tickled = false;
    
    if (m_timers.empty())
    {
        // 返回最大值
        return ~0ull;
    }

    auto now = std::chrono::system_clock::now();
    auto time = (*m_timers.begin())->m_next;

    if(now>=time)
    {
        // 已经有timer超时
        return 0;
    }
    else
    {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(time - now);
        return static_cast<uint64_t>(duration.count());            
    }  
}

void TimerManager::listExpiredCb(std::vector<std::function<void()>>& cbs)
{
    auto now = std::chrono::system_clock::now();

    std::unique_lock<std::shared_mutex> write_lock(m_mutex); 

    bool rollover = detectClockRollover();
    
    // 回退 -> 清理所有timer || 超时 -> 清理超时timer
    while (!m_timers.empty() && rollover || !m_timers.empty() && (*m_timers.begin())->m_next <= now)
    {
        std::shared_ptr<Timer> temp = *m_timers.begin();
        m_timers.erase(m_timers.begin());
        
        cbs.push_back(temp->m_cb); 

        if (temp->m_recurring)
        {
            // 重新加入时间堆
            temp->m_next = now + std::chrono::milliseconds(temp->m_ms);
            m_timers.insert(temp);
        }
        else
        {
            // 清理cb
            temp->m_cb = nullptr;
        }
    }
}

bool TimerManager::hasTimer() 
{
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    return !m_timers.empty();
}

// lock + tickle()
void TimerManager::addTimer(std::shared_ptr<Timer> timer)
{
    bool at_front = false;
    {
        std::unique_lock<std::shared_mutex> write_lock(m_mutex);
        auto it = m_timers.insert(timer).first;
        at_front = (it == m_timers.begin()) && !m_tickled;
        
        // only tickle once till one thread wakes up and runs getNextTime()
        if(at_front)
        {
            m_tickled = true;
        }
    }
   
    if(at_front)
    {
        // wake up 
        onTimerInsertedAtFront();
    }
}

bool TimerManager::detectClockRollover() 
{
    bool rollover = false;
    auto now = std::chrono::system_clock::now();
    if(now < (m_previouseTime - std::chrono::milliseconds(60 * 60 * 1000))) 
    {
        rollover = true;
    }
    m_previouseTime = now;
    return rollover;
}

}

