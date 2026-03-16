#pragma once
#include <functional>

class Defer {
public:
    // 构造：接收可调用对象（lambda/函数等）
    explicit Defer(std::function<void()> func) noexcept
        : func_(std::move(func)) {
    }

    // 析构：执行清理逻辑（关键！）
    ~Defer() noexcept {
        if (func_) {
            try {
                func_();
            }
            catch (...) {
                // 析构函数中禁止抛出异常！静默忽略或记录日志
                // （实际项目中可替换为日志宏：LOG_ERROR("Defer cleanup failed");）
            }
        }
    }

    // 禁止拷贝（避免重复执行）
    Defer(const Defer&) = delete;
    Defer& operator=(const Defer&) = delete;

    // 允许移动（C++14+ 场景更安全）
    Defer(Defer&& other) noexcept : func_(std::move(other.func_)) {
        other.func_ = nullptr;
    }
    Defer& operator=(Defer&& other) noexcept {
        if (this != &other) {
            func_ = std::move(other.func_);
            other.func_ = nullptr;
        }
        return *this;
    }

private:
    std::function<void()> func_;
};

