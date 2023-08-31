# thread-pool
基于C++17的线程池


### 支持功能
1. 基于可变参模板编程和引用折叠原理,实现线程池submitTask接口,支持任意任务函数参数的传递
2. 使用future类型定值submitTask提交任务的返回值
3. 使用map和queue容器管理线程对象和任务
4. 基于条件变量condition_variable和互斥锁mutex实现任务提交线程和任务执行线程间的通信机制
5. 支持fixed和cached模式的线程池定制


### 使用方法
使用方法类似于std::thread
通过提交任务的形式使用(支持可变参数).

~~~

int sum1(int a, int b)
{
    this_thread::sleep_for(chrono::seconds(2));
    // 比较耗时
    return a + b;
}
int sum2(int a, int b, int c)
{
    this_thread::sleep_for(chrono::seconds(2));
    return a + b + c;
}
// io线程 
void io_thread(int listenfd)
{

}
// worker线程
void worker_thread(int clientfd)
{

}
int main()
{
    ThreadPool pool;
    // pool.setMode(PoolMode::MODE_CACHED);
    pool.start(2);

    future<int> r1 = pool.submitTask(sum1, 1, 2);
    future<int> r2 = pool.submitTask(sum2, 1, 2, 3);
    future<int> r3 = pool.submitTask([](int b, int e)->int {
        int sum = 0;
        for (int i = b; i <= e; i++)
            sum += i;
        return sum;
        }, 1, 100);
    future<int> r4 = pool.submitTask([](int b, int e)->int {
        int sum = 0;
        for (int i = b; i <= e; i++)
            sum += i;
        return sum;
        }, 1, 100);
    future<int> r5 = pool.submitTask([](int b, int e)->int {
        int sum = 0;
        for (int i = b; i <= e; i++)
            sum += i;
        return sum;
        }, 1, 100);
    //future<int> r4 = pool.submitTask(sum1, 1, 2);

    cout << r1.get() << endl;
    cout << r2.get() << endl;
    cout << r3.get() << endl;
    cout << r4.get() << endl;
    cout << r5.get() << endl;
~~~

### 项目问题

1. 在ThreadPool的资源回收,等待线程池所有线程退出时,发生死锁问题,导致进程无法退出
2. 在windows平台下运行良好的线程池,在linux平台下发生死锁问题,平台运行结果差异化

### 分析定位问题

通过gdb attach到正在运行的进程,通过info threads,threads tid,bt等命令查看各个线程的调用堆栈信息,结合项目代码,定位到发生死锁的代码片段,分析死锁问题产生的原因,xxx,以及最终的解决方案.
