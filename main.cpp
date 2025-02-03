#include <functional>

#include "AsyncTimer.h"

struct Timer
{
    bool m_isCyclic = false;

    // in seconds
    double m_destination { };

    std::function<void()> onDestinationAchieved;

    void start() noexcept
    {
        // to seconds
        m_dt = double(m_currentTime - m_lastTime) / 1'000'000'000.0;

        m_lastTime = m_currentTime;
        m_currentTime = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()
        ).count();

        m_currentTimeForDestination += m_dt;

        if(m_currentTimeForDestination >= m_destination && m_destination >= 0.0)
        {
            onDestinationAchieved();

            if(!m_isCyclic)
            {
                m_destination = -1.0;
            }
            else
            {
                m_currentTimeForDestination = 0.0;
            }
        }
    }

    const double& getDT() const noexcept
    {
        return m_dt;
    }

    [[nodiscard]] bool done() const noexcept
    {
        return m_currentTimeForDestination >= m_destination;
    }

private:
    double m_dt { };

    double m_currentTimeForDestination { };

    // in ns
    std::int64_t m_currentTime { };
    // in ns
    std::int64_t m_lastTime { };
};

// todo: make shared task

using namespace std::chrono_literals;

std::vector<std::shared_ptr<Task<std::vector<int>>>> tasks;
std::vector<std::shared_ptr<Task<int>>> tasks0;

Task<int> task1(int a)
{
    co_await 1s;

    co_return a;
}

Task<void> task2(int a)
{
    co_await std::chrono::seconds(a);
}

Task<std::vector<int>> bebeLyaLya()
{
    co_await task2(10);

    auto r0 = task1(2);
    auto r1 = task1(2);

    auto r2 = co_await r0 + co_await r1;

    std::vector<int> s;
    s.resize(100000);

    s[0] = r2;

    co_return s;
}

int main()
{
    // auto result = std::make_shared<Task<int>>(bebeLyaLya());

    Timer t;
    t.m_destination = 0.5;
    t.m_isCyclic = true;
    t.onDestinationAchieved = []() {
        auto result = std::make_shared<Task<std::vector<int>>>(bebeLyaLya());

        tasks.push_back(result);
    };

    Timer t0;
    t0.m_destination = 0.25;
    t0.m_isCyclic = true;
    t0.onDestinationAchieved = []() {
        std::cout << "bebebe" << std::endl;
    };

    while (true)
    {
        t.start();
        // t0.start();

        AsyncTimerTask::processTimers();

        auto it = tasks.begin();
        while (it != tasks.end())
        {
            auto& task = *it;
            if (task->isReady())
            {
                std::cout << "ready: " << task->get()[0] << std::endl;

                it = tasks.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    return 0;
}