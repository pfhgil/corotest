//
// Created by stuka on 02.02.2025.
//

#ifndef CORO_H
#define CORO_H

#include <iostream>
#include <coroutine>
#include <optional>

template<typename T>
static constexpr bool always_false = false;

template<typename T, typename DerivedPromiseT>
struct TaskPromiseBase;

template<typename TaskPromiseT, typename InputT>
struct AwaitTransform
{
    static auto transform(TaskPromiseT& promise, InputT)
    {
        static_assert(always_false<InputT>, "This type is not awaitable");
    }
};

template<typename T>
struct Task;

template<typename T, typename DerivedPromiseT>
struct TaskPromiseBase
{
    using promise_type = T;

    // корутина, которая стоит выше текущей корутины. при вызове co_await, co_return или co_yield для текущей корутины
    // мы передаём управление корутине m_awaitingCoroutine
    std::coroutine_handle<> m_awaitingCoroutine;

    // функция, которая создаёт корутину (task) перед началом выполнения тела корутины
    Task<T> get_return_object() noexcept;

    // точка остановки корутины, которая вызывается перед выполнением тела корутины
    // никогда не останавливаемся в данном случае (std::suspend_never)
    std::suspend_never initial_suspend() noexcept
    {
        return {};
    }

    // обработка ошибок
    void unhandled_exception()
    {
        // завершаем программу. можем также перевыкинуть исключение
        std::terminate();
    }

    // этой функцией мы передаём управление вызывающей корутине, реализуя симметричную передачу управления между корутинами
    // момент вызова final_suspend выглядит так:
    // final_suspend: co_await promise.final_suspend();
    // т.е. final_suspend у promise текущей корутины вызывается через co_await.
    // таким образом мы можем вернуть управление вызывающей корутине с помощью awaitable типа TransferAwaitable.
    auto final_suspend() noexcept
    {
        struct TransferAwaitable
        {
            std::coroutine_handle<> m_awaitingCoroutine;

            // вызывается перед await_suspend и, если true, то await_suspend не вызовется, но будет вызван await_resume.
            bool await_ready() noexcept
            {
                return false;
            }

            // функция, которая вызывается, если await_ready() == true.
            // если await_suspend возвращает объект типа std::coroutine_handle<>, то будет управление будет передано
            // корутине, которая вернётся из await_suspend.
            //
            // если await_suspend возвращает std::coroutine_handle, то АВТОМАТИЧЕСКОГО УДАЛЕНИЯ РЕСУРСОВ КОРУТИНЫ НЕ ПРОИСХОДИТ.
            // мы должны вызвать .destroy() у текущей корутины вручную.
            // во всех других случаях (bool, awaitable (например: std::always_suspend, std::never_suspend)) УДАЛЕНИЕ РЕСУРСОВ
            // КОРУТИНЫ ПРОИСХОДИТ АВТОМАТИЧЕСКИ
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept
            {
                return m_awaitingCoroutine ? m_awaitingCoroutine : std::noop_coroutine();
            }

            // вызывается при возобновлении выполнения текущей корутины.
            // но т.к. это final_suspend, то await_resume никогда не вызовется
            void await_resume() noexcept
            {

            }
        };

        // возвращаем awaitable объект. перенаправляем выполнение на вызывающую корутину
        return TransferAwaitable { m_awaitingCoroutine };
    }

    // for co_await
    template<typename InputT>
    auto await_transform(InputT&& input)
    {
        using input_t = std::decay_t<InputT>;
        return AwaitTransform<DerivedPromiseT, input_t>::transform(static_cast<DerivedPromiseT&>(*this), input);
    }
};

template<typename T>
struct TaskValuePromise : public TaskPromiseBase<T, TaskValuePromise<T>>
{
    // значение текущей корутины
    std::optional<T> m_value;

    // сохраняем значение, которое потом будет отдано вызывающей корутине.
    // также это значение может быть получено через m_value.get()
    void return_value(T val)
    {
        m_value = std::move(val);
    }
};

struct TaskVoidPromise : public TaskPromiseBase<void, TaskVoidPromise>
{
    void return_void()
    {
    }
};

template<typename T>
struct Task
{
    template<typename TaskPromiseT, typename InputT>
    friend struct AwaitTransform;

    using promise_type = std::conditional_t<std::is_void_v<T>, TaskVoidPromise, TaskValuePromise<T>>;

    Task(std::coroutine_handle<promise_type> handle) : m_coroutineHandle(handle)
    {

    }

    Task(Task&& other) : m_coroutineHandle(std::exchange(other.m_coroutineHandle, nullptr))
    {

    }

    Task& operator=(Task&& other) noexcept
    {
        if(std::addressof(other) == this || std::addressof(other.m_coroutineHandle) == m_coroutineHandle) return *this;

        if(m_coroutineHandle)
        {
            m_coroutineHandle.destroy();
        }

        m_coroutineHandle = std::exchange(other.m_coroutineHandle, nullptr);

        return *this;
    }

    ~Task()
    {
        if(m_coroutineHandle)
        {
            m_coroutineHandle.destroy();
        }
    }

    bool isReady() const noexcept
    {
        if(m_coroutineHandle)
        {
            return m_coroutineHandle.promise().m_value.has_value();
        }

        return false;
    }

    bool isValid() const noexcept
    {
        return m_coroutineHandle != nullptr;
    }

    bool isAwaited() const noexcept
    {
        return m_coroutineHandle.promise().m_awaitingCoroutine != nullptr;
    }

    T get()
    {
        if(m_coroutineHandle)
        {
            return std::move(*m_coroutineHandle.promise().m_value);
        }

        throw std::runtime_error("get from task without promise");
    }

private:
    std::coroutine_handle<promise_type> m_coroutineHandle;
};

template<typename T, typename DerivedPromiseT>
Task<T> TaskPromiseBase<T, DerivedPromiseT>::get_return_object() noexcept
{
    return Task<T>(std::coroutine_handle<DerivedPromiseT>::from_promise(static_cast<DerivedPromiseT&>(*this)));
}

template<typename TaskPromiseT, typename OtherTaskPromiseT>
struct AwaitTransform<TaskPromiseT, Task<OtherTaskPromiseT>>
{
    using await_type = Task<OtherTaskPromiseT>;

    static auto transform(TaskPromiseT& promise, await_type& otherTask)
    {
        if (!otherTask.isValid())
        {
            throw std::runtime_error("coroutine without promise awaited");
        }
        if (otherTask.isAwaited())
        {
            throw std::runtime_error("coroutine already awaited");
        }

        struct TaskAwaitable
        {
            std::coroutine_handle<typename await_type::promise_type> m_otherCoroutineHandle;

            // останавливаемся только когда awaited корутина не имеет значения
            bool await_ready() noexcept
            {
                if constexpr(std::is_void_v<OtherTaskPromiseT>)
                {
                    return false;
                }
                else
                {
                    return m_otherCoroutineHandle.promise().m_value.has_value();
                }
            }

            // thisCoroutine это хендлер вызывающей корутины, т.е. корутины, которая вызывает co_await для otherTask.
            // сохраняем значение thisCoroutine в m_awaitingCoroutine для того, чтобы вернуться к вызывающей корутине
            // после калькуляции значения в otherTask.
            // напоминание: при выполнении корутины вызывается final_suspend, который передаёт управление вызывающей стороне
            // с помощью awaitable типа TransferAwaitable
            void await_suspend(std::coroutine_handle<> thisCoroutine)
            {
                m_otherCoroutineHandle.promise().m_awaitingCoroutine = thisCoroutine;
            }

            // вызывается когда otherTask выполнится. возвращаем значение вызывающей стороне
            auto await_resume() noexcept
            {
                if constexpr(std::is_void_v<OtherTaskPromiseT>)
                {
                    return;
                }
                else
                {
                    return std::move(*(m_otherCoroutineHandle.promise().m_value));
                }
            }
        };

        // возвращаем awaitable корутину
        return TaskAwaitable { otherTask.m_coroutineHandle };
    }
};

#endif //CORO_H
