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

class ThreadPool {//�̳߳�ʵ��
public:
    ThreadPool(size_t);
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)//������Ӻ��� 
        -> std::future<typename std::result_of<F(Args...)>::type>;
    ~ThreadPool();
private:
    // need to keep track of threads so we can join them
    std::vector< std::thread > workers;//�����߳�
    // the task queue
    std::queue< std::function<void()> > tasks;//������У��洢��������ǩ��Ҫ��Ϊvoid()����
    
    // synchronization
    std::mutex queue_mutex;//������
    std::condition_variable condition;//��������
    bool stop;//���б�־
};
 
// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads)
    :   stop(false)//���б�־
{
    for(size_t i = 0;i<threads;++i)
        workers.emplace_back(//empplace_back����thread�Ĺ��캯������lamada[this](){}�����
            [this]
            {
                for(;;)//����ѭ��
                {
                    std::function<void()> task;

                    {//RAII:unique_lock�Զ�����
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock,
                            [this]{ return this->stop || !this->tasks.empty(); });//�����������̳߳������������������Ϊ��


                        if(this->stop && this->tasks.empty())//ֹͣ������������пշ��أ��߳̽���
                            return;
                        task = std::move(this->tasks.front());//ȡ����ִ��
                        this->tasks.pop();
                    }

                    task();//ִ��
                }
            }
        );
}

// add new work item to the pool
// queue<function<void()>> �������
// F()+Args&& --->bind()��----->lamada��װΪ void()----->���
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    //bind������Ͳ���,��packaged_task��װΪ����Ϊreturn_type����Ϊ�յĿɵ��ö��󲢷���shared_ptrָ��
    auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // don't allow enqueueing after stopping the pool
        if(stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace([task](){ (*task)(); });//ʹ��lamada��װΪ�����ͷ��ض�Ϊ�յĿɵ��ö���,  std::queue<std::function<void()>> tasks
    }
    condition.notify_one();//�����߳�
    return res;//����future
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;//�����߳̿����������У�����ѭ���У���Ƶ����� stop ��ֵ��
    }
    condition.notify_all();
    for(std::thread &worker: workers)
        worker.join();
}

#endif
