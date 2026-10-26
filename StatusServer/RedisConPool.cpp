#include "RedisConPool.h"

RedisConPool::RedisConPool(size_t poolSize, const char* host, int port, const char* pwd)
    : poolSize_(poolSize), host_(host == nullptr ? "" : host), port_(port),
    pwd_(pwd == nullptr ? "" : pwd), b_stop_(false) {
    for (size_t i = 0; i < poolSize_; ++i) {
        auto* context = createConnection();
        if (context != nullptr) {
            connections_.push(context);
        }
    }
}

RedisConPool::~RedisConPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!connections_.empty()) {
        //@cuiruoni 连接池析构时需要显式释放 hiredis 连接，不能只出队不释放
        redisFree(connections_.front());
        connections_.pop();
    }
}

redisContext* RedisConPool::createConnection() {
    //@cuiruoni 这里统一处理连接建立和 AUTH，后续连接失效重建时也复用这套逻辑
    auto* context = redisConnect(host_.c_str(), port_);
    if (context == nullptr) {
        std::cout << "Redis connect failed: null context" << std::endl;
        return nullptr;
    }

    if (context->err != 0) {
        std::cout << "Redis connect failed: " << context->errstr << std::endl;
        redisFree(context);
        return nullptr;
    }

    if (pwd_.empty()) {
        return context;
    }

    auto reply = (redisReply*)redisCommand(context, "AUTH %s", pwd_.c_str());
    if (reply == nullptr) {
        std::cout << "Redis auth failed: null reply" << std::endl;
        redisFree(context);
        return nullptr;
    }

    if (reply->type == REDIS_REPLY_ERROR) {
        std::cout << "Redis auth failed: "
            << (reply->str == nullptr ? "unknown error" : reply->str) << std::endl;
        freeReplyObject(reply);
        redisFree(context);
        return nullptr;
    }

    freeReplyObject(reply);
    std::cout << "Redis auth success" << std::endl;
    return context;
}
void RedisConPool::Close() {
	b_stop_ = true;
	cond_.notify_all();
}

redisContext* RedisConPool::getConnection() {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] {
        if (b_stop_) {
            return true;
        }
        return !connections_.empty();
        });
    //如果停止则直接返回空指针
    if (b_stop_) {
        return  nullptr;
    }
    auto* context = connections_.front();
    connections_.pop();
    return context;
}

void RedisConPool::returnConnection(redisContext* context) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (context == nullptr) {
        return;
    }

    if (b_stop_) {
        redisFree(context);
        return;
    }

    //@cuiruoni 坏连接不能直接放回池里，否则后续请求会反复取到同一个失效连接
    if (context->err != 0) {
        std::cout << "Redis connection dropped: " << context->errstr << std::endl;
        redisFree(context);
        context = createConnection();
        if (context == nullptr) {
            cond_.notify_one();
            return;
        }
    }

    connections_.push(context);
    cond_.notify_one();
}
