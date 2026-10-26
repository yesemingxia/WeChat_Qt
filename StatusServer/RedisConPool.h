#pragma once
#include "const.h"

class RedisConPool {
public:
    RedisConPool(size_t poolSize, const char* host, int port, const char* pwd);

    ~RedisConPool();

    redisContext* getConnection();

    void returnConnection(redisContext* context);

    void Close();
    bool Get(const std::string& key, std::string& value);

private:
    //@cuiruoni 统一通过该函数创建并鉴权 Redis 连接，避免连接初始化逻辑分散
    redisContext* createConnection();
    std::atomic<bool> b_stop_;
    size_t poolSize_;
    std::string host_;
    int port_;
    std::string pwd_;
    std::queue<redisContext*> connections_;
    std::mutex mutex_;
    std::condition_variable cond_;
};
