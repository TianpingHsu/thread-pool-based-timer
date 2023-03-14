
#pragma once

#include <cstddef>
#include <thread>
#include <future>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <functional>
#include <vector>
#include <queue>
#include <memory>
#include <stdexcept>
#include <list>
#include <algorithm>

namespace POC {

    using TimerId = uint64_t;

    class Timer {
        friend class TimerManager;
        public:
            // https://stackoverflow.com/questions/8711391/should-i-copy-an-stdfunction-or-can-i-always-take-a-reference-to-it
            explicit Timer(std::chrono::milliseconds interval, std::chrono::milliseconds delay, std::function<void()> function)
                : _interval(interval), _delay(delay), _id(genTimerId())
            {
                using namespace std::chrono_literals;
                if (delay != 0ms) {
                    _func = [=]() {
                        std::this_thread::sleep_for(delay);
                        function();
                    };
                } else {
                    _func = function;
                    _expired_time = std::chrono::system_clock::now() + _interval;
                }
            }

            template<class F, class... Args>
                explicit Timer(std::chrono::milliseconds interval, std::chrono::milliseconds delay, F&& func, Args&&... args) 
                : _interval(interval), _delay(delay), _id(genTimerId())
                {
                    using namespace std::chrono_literals;
                    if (delay != 0ms) {
                        _func = [=]() {
                            std::this_thread::sleep_for(delay);
                            func(std::forward(args)...);
                        };
                    } else {
                        using return_type = typename std::result_of<F(Args...)>::type;
                        if (std::is_same<return_type, void>::value) {
                            _func = std::bind(std::forward<F>(func), std::forward<Args>(args)...);
                        } else {
                            auto task = std::make_shared<std::packaged_task<return_type()>>(
                                    std::bind(std::forward<F>(func), std::forward<Args>(args)...)
                                    );
                            _func = [task]() {(*task)();};
                        }
                        _expired_time = std::chrono::system_clock::now() + _interval;
                    }
                }

            void operator()() { _func(); }

            Timer(const Timer&) = delete;
            Timer(const Timer&& ) = delete;
            Timer& operator=(const Timer&) = delete;
            Timer& operator=(const Timer&&) = delete;

        private:
            TimerId genTimerId() {
                static std::mutex mtx;
                static TimerId s_timerId = 0;
                std::unique_lock<std::mutex> lck(mtx);
                s_timerId++;
                return s_timerId;
            }

        private:
            std::chrono::milliseconds _interval, _delay;
            std::chrono::time_point<std::chrono::system_clock> _expired_time;
            std::function<void()> _func;
            TimerId _id;
    };

    class ThreadPool {
        public:
            explicit ThreadPool(size_t);
            void enqueue(std::shared_ptr<Timer> t);
            ~ThreadPool();
        protected:
            // need to keep track of threads so we can join them
            std::vector< std::thread > workers;
            // the task queue
            std::queue<std::shared_ptr<Timer>> tasks;

            // synchronization
            std::mutex queue_mutex;
            std::condition_variable condition;
            bool stop;
    };

    // the constructor just launches some amount of workers
    inline ThreadPool::ThreadPool(size_t threads)
        :   stop(false)
    {
        for(size_t i = 0;i<threads;++i)
            workers.emplace_back(
                    [this]
                    {
                        for(;;)
                        {
                            std::shared_ptr<Timer> task;
                            {
                                std::unique_lock<std::mutex> lock(this->queue_mutex);
                                this->condition.wait(lock,
                                        [this]{ return this->stop || !this->tasks.empty(); });
                                if(this->stop && this->tasks.empty()) return;
                                task = std::move(this->tasks.front());
                                this->tasks.pop();
                            }

                            if (task) (*task)();
                        }
                    }
                    );
    }

    // add new work item to the pool
    inline void ThreadPool::enqueue(std::shared_ptr<Timer> t) {
            std::unique_lock<std::mutex> lock(queue_mutex);
            // don't allow enqueueing after stopping the pool
            if(stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");
            tasks.emplace(t);
            condition.notify_one();

    }

    // the destructor joins all threads
    inline ThreadPool::~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for(std::thread &worker: workers)
            worker.join();
    }

    class TimerManager: public ThreadPool {
        public:

            static TimerManager* getInstance() {
                static TimerManager man;
                return &man;
            }

            TimerId addTimer(Timer* pTimer) {

            }

            void deleteTimer(TimerId* timerId) {

            }

        protected:
            TimerManager(): ThreadPool(std::thread::hardware_concurrency())
            {
                _poller = std::thread([this](){
                            while (!stop) {
                                std::list<std::shared_ptr<Timer>> tmp;
                                while (!_timers.empty()) {
                                    auto _now = std::chrono::system_clock::now();
                                    std::shared_ptr<Timer> t = nullptr;
                                    t = _timers.front(); 
                                    if (t->_expired_time < _now) {
                                        std::this_thread::sleep_for(_now - t->_expired_time);
                                        break;
                                    }
                                    enqueue(t);  // timed out, process timer task
                                    _timers.erase(_timers.begin());
                                    t->_expired_time += t->_interval;  // update expired time
                                    tmp.push_back(t);
                                }
                                if (!tmp.empty()) {
                                    std::copy(tmp.begin(), tmp.end(), _timers.end());
                                    std::sort(_timers.begin(), _timers.end(), [](const std::shared_ptr<Timer> & a, const std::shared_ptr<Timer> &b) {
                                                return a->_expired_time < b->_expired_time;
                                            });
                                }
                            }
                        });
            }
            ~TimerManager() {
                stop = true;
                _poller.join();
                _timers.clear();
            }
            TimerManager(const TimerManager&) = delete;
            TimerManager(const TimerManager&&) = delete;
            TimerManager operator=(const TimerManager&) = delete;
            TimerManager operator=(const TimerManager&&) = delete;
        private:
            std::thread _poller;
            std::vector<std::shared_ptr<Timer>> _timers;  // timer list
    };
}

