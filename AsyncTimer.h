//
// Created by stuka on 02.02.2025.
//

#ifndef ASYNCTIMER_H
#define ASYNCTIMER_H

#include "coro.h"
#include <chrono>

struct AsyncTimerTask
{
    template<typename, typename>
    friend struct AwaitTransform;

    std::chrono::steady_clock::time_point m_timePoint;
    std::coroutine_handle<> m_coroutineHandle;

    static void processTimers() noexcept
    {
        // здесь мы используем два вектора, т.к. если вызывать handle.resume() во время итерации по m_timerTasks,
        // то может произойти сбой, т.к. handle.resume() вновь запустит корутину, которая в конечном итоге передаст
        // управление вызывающей стороне и вызывающая сторона может вызвать co_await с таймером вновь. в итоге
        // добавится новый таймер в m_timerTasks прямо во время итерации по m_timerTasks, что приведёт к инвалидации
        // итераторов и вызовет сбой приложения

        m_handlesToResume.clear();

        auto it = m_timerTasks.begin();
        while (it != m_timerTasks.end())
        {
            auto& timer = *it;

            if (it->m_timePoint < std::chrono::steady_clock::now())
            {
                m_handlesToResume.push_back(timer.m_coroutineHandle);
                it = m_timerTasks.erase(it);
            }
            else
            {
                ++it;
            }
        }

        for(const auto& handle : m_handlesToResume)
        {
            handle.resume();
        }
    }

private:
    static inline std::vector<AsyncTimerTask> m_timerTasks;
    static inline std::vector<std::coroutine_handle<>> m_handlesToResume;
};

template<typename TaskPromiseT, typename Rep, typename Period>
struct AwaitTransform<TaskPromiseT, std::chrono::duration<Rep, Period>>
{
    using await_type = std::chrono::duration<Rep, Period>;

    static auto transform(TaskPromiseT& promise, await_type duration)
    {
        struct TimerAwaitable
        {
            await_type m_duration;

            // всегда останавливаемся
            bool await_ready() noexcept
            {
                return false;
            }

            // создаём таймер с временной точкой когда он должен остановиться.
            // как только временная точка будет достигнута, корутина продолжится
            void await_suspend(std::coroutine_handle<TaskPromiseT> h)
            {
                AsyncTimerTask::m_timerTasks.push_back(AsyncTimerTask {
                        std::chrono::steady_clock::now() + m_duration, h
                    }
                );
            }

            void await_resume() noexcept
            {
            }
        };

        // возвращаем awaitable объект
        return TimerAwaitable{duration};
    }
};

#endif //ASYNCTIMER_H
