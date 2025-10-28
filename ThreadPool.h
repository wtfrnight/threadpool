#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool {//线程池实现
public:
    ThreadPool(size_t);
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)//任务入队函数 
        -> std::future<typename std::result_of<F(Args...)>::type>;
    ~ThreadPool();
private:
    // need to keep track of threads so we can join them
    std::vector< std::thread > workers;//工作线程
    // the task queue
    std::queue< std::function<void()> > tasks;//任务队列，存储的任务函数签名要求为void()类型
    
    // synchronization
    std::mutex queue_mutex;//互斥量
    std::condition_variable condition;//条件变量
    bool stop;//运行标志
};
 
// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads)
    :   stop(false)//运行标志
{
    for(size_t i = 0;i<threads;++i)
        workers.emplace_back(//empplace_back运行thread的构造函数传递lamada[this](){}运算符
            [this]
            {
                for(;;)//无限循环
                {
                    std::function<void()> task;

                    {//RAII:unique_lock自动析构
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock,
                            [this]{ return this->stop || !this->tasks.empty(); });//休眠条件：线程池仍在运行且任务队列为空


                        if(this->stop && this->tasks.empty())//停止运行且任务队列空返回，线程结束
                            return;
                        task = std::move(this->tasks.front());//取任务并执行
                        this->tasks.pop();
                    }

                    task();//执行
                }
            }
        );
}

// add new work item to the pool
// queue<function<void()>> 任务队列
// F()+Args&& --->bind()绑定----->lamada包装为 void()----->入队
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    //bind绑定任务和参数,用packaged_task包装为返回为return_type参数为空的可调用对象并返回shared_ptr指针
    auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // don't allow enqueueing after stopping the pool
        if(stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace([task](){ (*task)(); });//使用lamada包装为参数和返回都为空的可调用对象,  std::queue<std::function<void()>> tasks
    }
    condition.notify_one();//唤醒线程
    return res;//返回future
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;//工作线程可能正在运行（处于循环中，会频繁检查 stop 的值）
    }
    condition.notify_all();
    for(std::thread &worker: workers)
        worker.join();
}

#endif
