struct ThreadPool {
    vector<thread> workers;
    queue<function<void()>> tasks;
    mutex queue_mutex;
    condition_variable condition;
    bool stop;

    ThreadPool(size_t threads) : stop(false) {
        for (size_t i = 0; i < threads; ++i)
            workers.emplace_back([this] {
                for (;;) {
                    function<void()> task;
                    {
                        unique_lock<mutex> lock(queue_mutex);
                        condition.wait(lock, [this] { return stop || !tasks.empty(); });
                        if (stop && tasks.empty()) return;
                        task = move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
    };

    template<class F, class... Args>
    auto enqueue(F &&f, Args &&...args) -> future<typename invoke_result<F, Args...>::type> {
        using return_type = typename invoke_result<F, Args...>::type;

        auto task =
            make_shared<packaged_task<return_type()>>(bind(forward<F>(f), forward<Args>(args)...));

        future<return_type> res = task->get_future();
        {
            unique_lock<mutex> lock(queue_mutex);

            // don't allow enqueueing after stopping the pool
            assert(!stop);

            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    ~ThreadPool() {
        {
            unique_lock<mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (thread &worker : workers) worker.join();
    }
};