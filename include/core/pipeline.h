#pragma once
// ╔══════════════════════════════════════════════════════════╗
// ║  三线程流水线 + 线程安全队列                              ║
// ║  用 std::thread + mutex + condition_variable 纯标准库    ║
// ╚══════════════════════════════════════════════════════════╝

#include <queue>
#include <mutex>               // 互斥锁 — 同一时刻只允许一个线程访问
#include <condition_variable>  // 条件变量 — "等某个条件成立再继续"
#include <thread>
#include <functional>
#include <cstdio>

// ═══════════════════════════════════════════════════
// 线程安全队列 — 生产者-消费者模式的核心数据结构
// ═══════════════════════════════════════════════════
// template<typename T> — 泛型编程, T 可以是任何类型
//   SafeQueue<int>     → 整数队列
//   SafeQueue<cv::Mat> → 图像队列
template<typename T>
class SafeQueue {
    std::queue<T> q_;                    // 底层容器 (普通队列)
    std::mutex    mtx_;                  // 互斥锁 — 保护 q_ 不被并发破坏
    std::condition_variable cv_;         // 条件变量 — 让线程"睡觉等"
    size_t max_size_ = 4;               // 最大容量 (背压控制)
public:
    // 初始化列表 : max_size_(max) — 在对象构造时直接赋值, 比在函数体里赋值高效
    SafeQueue(size_t max = 4) : max_size_(max) {}

    // ── 生产者 push ──
    // 如果队列满了 → 当前线程阻塞等待, 直到消费者 pop 出空间
    void push(T item) {
        // unique_lock: "独占锁", 构造时自动加锁, 析构时自动解锁
        // lock_guard 做不到跟 cv_.wait 配合, 必须用 unique_lock
        std::unique_lock<std::mutex> lock(mtx_);

        // cv_.wait(锁, 条件): 如果条件为 false → 释放锁 + 睡觉
        //                      被唤醒后 → 重新加锁 + 再检查条件
        cv_.wait(lock, [this]{ return q_.size() < max_size_; });

        // std::move — "所有权转移", 不拷贝数据
        // item 是一个右值引用, 数据被"搬"进队列, item 变成空壳
        q_.push(std::move(item));

        // notify_one — 唤醒一个正在 cv_.wait 的消费者
        // "有货了, 起来干活!"
        cv_.notify_one();
    }  // lock 析构 → 自动解锁

    // ── 消费者 pop ──
    // 如果队列空了 → 阻塞等待, 直到生产者 push 进来
    T pop() {
        std::unique_lock<std::mutex> lock(mtx_);
        // 条件: 队列非空
        cv_.wait(lock, [this]{ return !q_.empty(); });
        T item = std::move(q_.front());  // 搬出来 (不拷贝)
        q_.pop();
        cv_.notify_one();  // 唤醒生产者: "有空位了, 可以生产了"
        return item;
    }

    // ── 非阻塞取 (用于退出检测) ──
    bool try_pop(T& item) {
        // lock_guard: 比 unique_lock 轻量, 但不能和 cv_.wait 配合
        // 这里不需要 wait, 用 lock_guard 刚好
        std::lock_guard<std::mutex> lock(mtx_);
        if (q_.empty()) return false;
        item = std::move(q_.front());
        q_.pop();
        cv_.notify_one();
        return true;
    }
};

// ═══════════════════════════════════════════════════
// 简易管线 — 启动 3 个线程, 管理生命周期
// ═══════════════════════════════════════════════════
// 实际使用: main.cpp 里不用这个类, 直接手写 thread + lambda
// 这个类保留作为参考
class Pipeline {
    std::thread t1_, t2_, t3_;  // 3 个线程对象
    // volatile — 告诉编译器"这个变量可能被其他线程修改, 每次用都要从内存读"
    volatile bool running_ = false;
public:
    ~Pipeline() { stop(); }  // RAII: 对象销毁时自动清理线程

    // template — 泛型, F1/F2/F3 可以是任意可调用对象 (函数指针/lambda/函数对象)
    template<typename F1, typename F2, typename F3>
    void start(F1 capture, F2 infer, F3 postprocess) {
        running_ = true;
        // std::thread(可调用对象) — 创建一个新线程, 立即开始执行
        // 线程从这一刻起并行运行, 和主线程同时干活
        t1_ = std::thread(capture);
        t2_ = std::thread(infer);
        t3_ = std::thread(postprocess);
    }

    void stop() {
        running_ = false;
        // join() — "等这个线程跑完再继续"
        // 如果线程已经在阻塞(如 cv_.wait), join 会一直等到它被唤醒并退出
        if (t1_.joinable()) t1_.join();
        if (t2_.joinable()) t2_.join();
        if (t3_.joinable()) t3_.join();
    }
};

/* ═══════════════════════════════════════════════════════════
   线程概念快速参考:

   1. std::mutex (互斥锁)
      ┌─────────────┐
      │ Thread A    │  lock(mtx) → 拿到锁 → 访问共享数据 → unlock
      │ Thread B    │  lock(mtx) → 🔒 阻塞等 A 解锁
      └─────────────┘
      保证同一时刻只有一个线程访问 q_

   2. std::condition_variable (条件变量)
      Thread A (消费者): 队列空 → cv.wait() → 💤睡觉等
      Thread B (生产者): push 后 → cv.notify_one() → 📢 唤醒 A

   3. std::move (移动语义)
      不需要拷贝, "把所有权交给你"
      类比: 你搬家时不是复制房子, 而是把钥匙给我

   4. std::unique_lock vs std::lock_guard
      lock_guard:  构造加锁, 析构解锁, 轻量
      unique_lock: 可以手动加锁/解锁, 必须配合 cv.wait 使用

   5. template<typename T> (泛型)
      编译器帮你生成代码:
      SafeQueue<int> → 生成一个处理 int 的队列类
      SafeQueue<Mat> → 生成一个处理 Mat 的队列类
      写一次, 任意类型都能用
   ═══════════════════════════════════════════════════════════ */