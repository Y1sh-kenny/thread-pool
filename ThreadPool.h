//��д��ô˵��д

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


//������
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



//�߳�����
const int MAX_THREAD_SIZE = std::thread::hardware_concurrency();
//��������
const int MAX_TASK_SIZE = 1024;
//����ʱʱ��
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
	//�����̳߳�ģʽ�ӿ�
	void setPoolMode(PoolMode mode)
	{
		//Ϊ��ȷ���û����������̳߳�֮ǰ���õ�...
		if (!isRunning_)
		{
			mode_ = mode;
		}

	}
	// 
	//�����̳߳�
	void start(int initThreadNum = 6)
	{
		threadInitSize_ = initThreadNum;

		//�����߳�
		for (size_t i = 0; i < threadInitSize_; i++)
		{
			auto tptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			size_t threadid = tptr->getId();
			threads_.emplace(threadid, std::move(tptr));
		}
		//���߳�����
		for (size_t i = 0; i < threadInitSize_; i++)
		{
			threads_[i]->start();
			idleThreadSize_++;//���е��߳�������..
		}
	}

	//�û��ύ�̵߳ķ���   ��package_task<> �� future
	//Ӧ������������ʽ������?... auto res =  pool.submitTask(add,10,20);   ---> res.get();�õ����    
	//auto res =  pool.submitTask(add,10,20);
	template<typename Func,typename... Args>
	auto submitTask(Func&& func, Args... args) -> std::future<decltype(func(args...))>
	{
		//����ֵ����
		using RType = decltype(func(args...));
		//�������
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));

		std::future<RType> result = task->get_future();


		//��ȡ��
		std::unique_lock<std::mutex>lock(queMtx_);
		//�����Ƿ�����
		if (!condQueNotFull_.wait_for(lock, std::chrono::seconds(1), [&]() -> bool {return tasks_.size() < taskSizeThreshHold_; }))
		{
			//��ʱ��,���ܷ���
			std::cerr << "�ύ����ʱ......�޷��ɹ��ύ" << std::endl;
			//����һ���յ�ֵ
			auto failTask = std::make_shared<std::packaged_task<RType()>>([]()->RType {return RType(); });
			(*failTask)();
			return failTask->get_future();
		}

		//��������������
		tasks_.emplace([task]() {(*task)(); });
		condQueNotEmpty_.notify_all();

		//Cached
		
		if (mode_ == PoolMode::CACHED && tasks_.size() > idleThreadSize_ && threads_.size() < threadSizeThreshHold_)
		{
			std::cout << "�����������,��ʱ�����µ��߳�" << std::endl;
			//�����µ��߳�
			auto tptr = make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadid = tptr->getId();
			threads_.emplace(threadid, std::move(tptr));

			//����
			threads_[threadid]->start();
			//�����߳�  + 1
			idleThreadSize_++;
		}

		return result;
	}

private:
	//�̳߳�ģʽ
	PoolMode mode_;
	std::atomic_bool isRunning_;


	//...��ָ�뱣���...ֱ�ӱ���Ҫ����...�߳��ǲ��ܱ�������..
	std::map<size_t, std::unique_ptr<Thread>> threads_;
	size_t threadInitSize_;
	size_t threadSizeThreshHold_;
	size_t idleThreadSize_;


	//����
	using Task = std::function<void()>;
	std::queue<Task>tasks_;
	size_t taskSizeThreshHold_;


	//�̰߳�ȫ���
	std::mutex queMtx_;
	std::condition_variable condQueNotFull_;
	std::condition_variable condQueNotEmpty_;


	//�˳�ʱʹ�õ���������
	std::condition_variable condExit_;



private:
	//���߳����еĺ���,�����߳�id,����֮����map��ɾ����
	void threadFunc(size_t threadId)
	{
		auto lastFinishTime = std::chrono::high_resolution_clock::now();
		while (1)
		{
			Task task;
			//ȡ������
			{
				//�����,�������������ȡ
				std::unique_lock<std::mutex>lock(queMtx_);
				std::cout << "tid : " << std::this_thread::get_id() << " ���Դ���������л������..." << std::endl;
				while (tasks_.size() == 0)
				{
					if (!isRunning_)
					{
						//��Ҫ������������س���
						threads_.erase(threadId);
						condExit_.notify_all();
						return;
					}
					if (mode_ == PoolMode::CACHED)
					{
						//������һ��ʱ���Ҫֹͣ��
						if (std::cv_status::timeout == condQueNotEmpty_.wait_for(lock, std::chrono::seconds(1)))
						{
							auto nowTime = std::chrono::high_resolution_clock::now();
							auto duration = std::chrono::duration_cast<std::chrono::seconds>(nowTime - lastFinishTime);
							if (duration.count() >= TIME_OUT_COUNT && threads_.size() > threadInitSize_)
							{
								//ɾ���߳�
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
				// �����߳� - 1
				idleThreadSize_--;

				if (tasks_.size() > 0)
				{
					condQueNotEmpty_.notify_all();
				}
				condQueNotFull_.notify_all();
			}

			
			//ִ������
			task();
			//�������
			lastFinishTime = std::chrono::high_resolution_clock::now();
			idleThreadSize_++;
		}
		
		
	}

};


#endif // !THREADPOOL_H
