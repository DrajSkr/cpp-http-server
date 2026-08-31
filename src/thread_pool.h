#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <stdexcept>
#include <iostream>

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency()) : stop(false) {
        if (numThreads == 0) {
            numThreads = 8; // fallback
        }
        std::cout << "[ThreadPool] Starting with " << numThreads << " worker threads." << std::endl;
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this, i] {
                std::cout << "[ThreadPool] Worker " << i << " started." << std::endl;
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] {
                            return this->stop || !this->tasks.empty();
                        });
                        if (this->stop && this->tasks.empty())
                            return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    // no copy
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template<class F, class... Args>
    void enqueue(F&& f, Args&&... args) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop) {
                throw std::runtime_error("[ThreadPool] Cannot enqueue on a stopped ThreadPool.");
            }
            tasks.emplace(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        }
        condition.notify_one();
    }

    // num pending tasks
    size_t pendingTasks() const {
        std::unique_lock<std::mutex> lock(queue_mutex);
        return tasks.size();
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers) {
            if (worker.joinable())
                worker.join();
        }
        std::cout << "[ThreadPool] All workers shut down." << std::endl;
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    mutable std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

extern ThreadPool* g_threadPool;
