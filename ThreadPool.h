//重写怎么说重写

#ifndef THREADPOOL_H
#define THREADPOOL_H
#include<map>
#include<queue>
#include<mutex>
#include<condition_variable>
#include<thread>
#include<functional>
#include<memory>
#include<atomic>
#include<future>


//测试用
#include<iostream>



class Thread
{
	using Func = std::function<void(int)>;
public:
	Thread(Func func)
		:func_(func),
		threadId_(idGenerator_++)
	{}

	size_t getId() { return threadId_; }

	void start()
	{
		std::thread t(func_, threadId_);
		t.detach();
	}



private:
	size_t threadId_;
	Func func_;
	static size_t idGenerator_;

};

size_t Thread::idGenerator_ = 0;



//线程上限
const int MAX_THREAD_SIZE = std::thread::hardware_concurrency();
//任务上限
const int MAX_TASK_SIZE = 1024;
//任务超时时间
const int TIME_OUT_COUNT = 10;


enum class PoolMode
{
	FIXED,
	CACHED
};

using std::cout;
using std::endl;

class ThreadPool
{
public:
	ThreadPool() :
		threadSizeThreshHold_(MAX_THREAD_SIZE),
		idleThreadSize_(0),
		taskSizeThreshHold_(MAX_TASK_SIZE),
		threadInitSize_(0),
		isRunning_(false),
		mode_(PoolMode::FIXED)
	{

	}

	~ThreadPool()
	{
		isRunning_ = false;
		std::unique_lock<std::mutex>lock(queMtx_);
		condQueNotEmpty_.notify_all();
		condExit_.wait(lock, [&]()->bool {return threads_.size() == 0; });
		
	}
	//设置线程池模式接口
	void setPoolMode(PoolMode mode)
	{
		//为了确保用户是在启动线程池之前设置的...
		if (!isRunning_)
		{
			mode_ = mode;
		}

	}
	// 
	//开启线程池
	void start(int initThreadNum = 6)
	{
		threadInitSize_ = initThreadNum;

		//创建线程
		for (size_t i = 0; i < threadInitSize_; i++)
		{
			auto tptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			size_t threadid = tptr->getId();
			threads_.emplace(threadid, std::move(tptr));
		}
		//将线程启动
		for (size_t i = 0; i < threadInitSize_; i++)
		{
			threads_[i]->start();
			idleThreadSize_++;//空闲的线程增加了..
		}
	}

	//用户提交线程的方法   用package_task<> 和 future
	//应该以怎样的形式存在呢?... auto res =  pool.submitTask(add,10,20);   ---> res.get();得到结果    
	//auto res =  pool.submitTask(add,10,20);
	template<typename Func,typename... Args>
	auto submitTask(Func&& func, Args... args) -> std::future<decltype(func(args...))>
	{
		//返回值类型
		using RType = decltype(func(args...));
		//打包任务
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));

		std::future<RType> result = task->get_future();


		//获取锁
		std::unique_lock<std::mutex>lock(queMtx_);
		//条件是否满足
		if (!condQueNotFull_.wait_for(lock, std::chrono::seconds(1), [&]() -> bool {return tasks_.size() < taskSizeThreshHold_; }))
		{
			//超时了,不能访问
			std::cerr << "提交任务超时......无法成功提交" << std::endl;
			//返回一个空的值
			auto failTask = std::make_shared<std::packaged_task<RType()>>([]()->RType {return RType(); });
			(*failTask)();
			return failTask->get_future();
		}

		//添加任务到任务队列
		tasks_.emplace([task]() {(*task)(); });
		condQueNotEmpty_.notify_all();

		//Cached
		
		if (mode_ == PoolMode::CACHED && tasks_.size() > idleThreadSize_ && threads_.size() < threadSizeThreshHold_)
		{
			std::cout << "由于任务过多,临时创建新的线程" << std::endl;
			//创建新的线程
			auto tptr = make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadid = tptr->getId();
			threads_.emplace(threadid, std::move(tptr));

			//运行
			threads_[threadid]->start();
			//空闲线程  + 1
			idleThreadSize_++;
		}

		return result;
	}

private:
	//线程池模式
	PoolMode mode_;
	std::atomic_bool isRunning_;


	//...用指针保存吧...直接保存要拷贝...线程是不能被拷贝的..
	std::map<size_t, std::unique_ptr<Thread>> threads_;
	size_t threadInitSize_;
	size_t threadSizeThreshHold_;
	size_t idleThreadSize_;


	//任务
	using Task = std::function<void()>;
	std::queue<Task>tasks_;
	size_t taskSizeThreshHold_;


	//线程安全相关
	std::mutex queMtx_;
	std::condition_variable condQueNotFull_;
	std::condition_variable condQueNotEmpty_;


	//退出时使用的条件变量
	std::condition_variable condExit_;



private:
	//供线程运行的函数,传入线程id,用来之后在map中删除他
	void threadFunc(size_t threadId)
	{
		auto lastFinishTime = std::chrono::high_resolution_clock::now();
		while (1)
		{
			Task task;
			//取出任务
			{
				//获得锁,从任务队列里面取
				std::unique_lock<std::mutex>lock(queMtx_);
				std::cout << "tid : " << std::this_thread::get_id() << " 尝试从任务队列中获得任务..." << std::endl;
				while (tasks_.size() == 0)
				{
					if (!isRunning_)
					{
						//需要结束调用这个县城了
						threads_.erase(threadId);
						condExit_.notify_all();
						return;
					}
					if (mode_ == PoolMode::CACHED)
					{
						//空闲了一段时间就要停止了
						if (std::cv_status::timeout == condQueNotEmpty_.wait_for(lock, std::chrono::seconds(1)))
						{
							auto nowTime = std::chrono::high_resolution_clock::now();
							auto duration = std::chrono::duration_cast<std::chrono::seconds>(nowTime - lastFinishTime);
							if (duration.count() >= TIME_OUT_COUNT && threads_.size() > threadInitSize_)
							{
								//删除线程
								threads_.erase(threadId);
								condExit_.notify_all();
								return;
							}
						}
					}
					else
					{
						condQueNotEmpty_.wait(lock);
					}
				}
				task = tasks_.front();
				tasks_.pop();
				// 空闲线程 - 1
				idleThreadSize_--;

				if (tasks_.size() > 0)
				{
					condQueNotEmpty_.notify_all();
				}
				condQueNotFull_.notify_all();
			}

			
			//执行任务
			task();
			//任务结束
			lastFinishTime = std::chrono::high_resolution_clock::now();
			idleThreadSize_++;
		}
		
		
	}

};


#endif // !THREADPOOL_H
