
#pragma once
#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <future>

class ThreadPool {
public:
    ThreadPool(size_t n);
    ~ThreadPool();

    template<class F>
    auto enqueue(F&& f) -> std::future<decltype(f())>;

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

template<class F>
auto ThreadPool::enqueue(F&& f) -> std::future<decltype(f())> {
    auto task = std::make_shared<std::packaged_task<decltype(f())()>>(std::forward<F>(f));
    std::future<decltype(f())> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        tasks.emplace([task]() { (*task)(); });
    }
    condition.notify_one();
    return res;
}
