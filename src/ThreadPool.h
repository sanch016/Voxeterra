#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads) {
        for (size_t i = 0; i < numThreads; ++i) {
            m_workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(m_mutex);
                        m_condition.wait(lock, [this] {
                            return m_stop || !m_tasks.empty();
                        });

                        if (m_stop && m_tasks.empty()) return;

                        task = std::move(m_tasks.front());
                        m_tasks.pop();
                    }

                    m_activeTasks.fetch_add(1, std::memory_order_relaxed);
                    task();
                    m_activeTasks.fetch_sub(1, std::memory_order_release);
                    m_doneCondition.notify_all();
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_condition.notify_all();
        for (auto& worker : m_workers) {
            if (worker.joinable()) worker.join();
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <class F>
    void enqueue(F&& f) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tasks.emplace(std::forward<F>(f));
        }
        m_condition.notify_one();
    }

    void waitForAll() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_doneCondition.wait(lock, [this] {
            return m_tasks.empty() && m_activeTasks.load(std::memory_order_acquire) == 0;
        });
    }

    size_t pendingTasks() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_tasks.size();
    }

    int activeTasks() const {
        return m_activeTasks.load(std::memory_order_acquire);
    }

private:
    std::vector<std::thread>              m_workers;
    std::queue<std::function<void()>>     m_tasks;

    mutable std::mutex                    m_mutex;
    std::condition_variable               m_condition;
    std::condition_variable               m_doneCondition;

    bool              m_stop = false;
    std::atomic<int>  m_activeTasks{0};
};
