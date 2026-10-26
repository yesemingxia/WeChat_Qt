#include "RedisMgr.h"
#include "ConfigMgr.h"
#include "RedisConPool.h"
#include "DistLock.h"

namespace {
//@cuiruoni SET 失败时补充 Redis 上下文和 reply 细节，便于定位是连接、鉴权还是服务端返回异常
void LogRedisSetFailure(const std::string& key, const std::string& value, redisContext* connect, redisReply* reply) {
    std::cout << "Execut command [ SET " << key << "  " << value << " ] failure !";

    if (connect != nullptr && connect->err != 0) {
        std::cout << " context_err=" << connect->errstr;
    }

    if (reply != nullptr) {
        std::cout << " reply_type=" << reply->type;
        if (reply->str != nullptr) {
            std::cout << " reply=" << reply->str;
        }
    }

    std::cout << std::endl;
}
}

RedisMgr::RedisMgr()
{
    auto& gCfgMgr = ConfigMgr::GetInstance();
    auto host = gCfgMgr["Redis"]["Host"];
    auto port = gCfgMgr["Redis"]["Port"];
    auto pwd = gCfgMgr["Redis"]["Passwd"];
    _con_pool.reset(new RedisConPool(5, host.c_str(), atoi(port.c_str()), pwd.c_str()));
}

RedisMgr::~RedisMgr()
{
}

bool RedisMgr::Get(const std::string& key, std::string& value)
{
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }
    auto reply = (redisReply*)redisCommand(connect, "GET %s", key.c_str());
    if (reply == NULL) {
        std::cout << "[ GET  " << key << " ] failed" << std::endl;
        _con_pool->returnConnection(connect);
        return false;
    }

    if (reply->type != REDIS_REPLY_STRING) {
        std::cout << "[ GET  " << key << " ] failed" << std::endl;
        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    value = reply->str;
    freeReplyObject(reply);

    std::cout << "Succeed to execute command [ GET " << key << "  ]" << std::endl;
    _con_pool->returnConnection(connect);
    return true;
}

bool RedisMgr::Set(const std::string& key, const std::string& value) {
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }

    auto reply = (redisReply*)redisCommand(connect, "SET %s %s", key.c_str(), value.c_str());

    if (NULL == reply)
    {
        LogRedisSetFailure(key, value, connect, reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    if (!(reply->type == REDIS_REPLY_STATUS && (strcmp(reply->str, "OK") == 0 || strcmp(reply->str, "ok") == 0)))
    {
        LogRedisSetFailure(key, value, connect, reply);
        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    freeReplyObject(reply);
    std::cout << "Execut command [ SET " << key << "  " << value << " ] success ! " << std::endl;
    _con_pool->returnConnection(connect);
    return true;
}

bool RedisMgr::Auth(const std::string& password)
{
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }

    auto reply = (redisReply*)redisCommand(connect, "AUTH %s", password.c_str());
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) {
        std::cout << "认证失败" << std::endl;
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
        _con_pool->returnConnection(connect);
        return false;
    }
    else {
        freeReplyObject(reply);
        std::cout << "认证成功" << std::endl;
        _con_pool->returnConnection(connect);
        return true;
    }
}

bool RedisMgr::LPush(const std::string& key, const std::string& value)
{
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }

    auto reply = (redisReply*)redisCommand(connect, "LPUSH %s %s", key.c_str(), value.c_str());
    if (NULL == reply)
    {
        std::cout << "Execut command [ LPUSH " << key << "  " << value << " ] failure ! " << std::endl;
        _con_pool->returnConnection(connect);
        return false;
    }

    if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0) {
        std::cout << "Execut command [ LPUSH " << key << "  " << value << " ] failure ! " << std::endl;
        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    std::cout << "Execut command [ LPUSH " << key << "  " << value << " ] success ! " << std::endl;
    freeReplyObject(reply);
    _con_pool->returnConnection(connect);
    return true;
}

bool RedisMgr::LPop(const std::string& key, std::string& value) {
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }

    auto reply = (redisReply*)redisCommand(connect, "LPOP %s", key.c_str());
    if (reply == nullptr || reply->type == REDIS_REPLY_NIL) {
        std::cout << "Execut command [ LPOP " << key << " ] failure ! " << std::endl;
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
        _con_pool->returnConnection(connect);
        return false;
    }
    value = reply->str;
    std::cout << "Execut command [ LPOP " << key << " ] success ! " << std::endl;
    freeReplyObject(reply);
    _con_pool->returnConnection(connect);
    return true;
}

bool RedisMgr::RPush(const std::string& key, const std::string& value) {
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }

    auto reply = (redisReply*)redisCommand(connect, "RPUSH %s %s", key.c_str(), value.c_str());
    if (NULL == reply)
    {
        std::cout << "Execut command [ RPUSH " << key << "  " << value << " ] failure ! " << std::endl;
        _con_pool->returnConnection(connect);
        return false;
    }

    if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0) {
        std::cout << "Execut command [ RPUSH " << key << "  " << value << " ] failure ! " << std::endl;
        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    std::cout << "Execut command [ RPUSH " << key << "  " << value << " ] success ! " << std::endl;
    freeReplyObject(reply);
    _con_pool->returnConnection(connect);
    return true;
}

bool RedisMgr::RPop(const std::string& key, std::string& value) {
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }

    auto reply = (redisReply*)redisCommand(connect, "RPOP %s", key.c_str());
    if (reply == nullptr || reply->type == REDIS_REPLY_NIL) {
        std::cout << "Execut command [ RPOP " << key << " ] failure ! " << std::endl;
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
        _con_pool->returnConnection(connect);
        return false;
    }
    value = reply->str;
    std::cout << "Execut command [ RPOP " << key << " ] success ! " << std::endl;
    freeReplyObject(reply);
    _con_pool->returnConnection(connect);
    return true;
}

bool RedisMgr::HSet(const std::string& key, const std::string& hkey, const std::string& value) {
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }

    auto reply = (redisReply*)redisCommand(connect, "HSET %s %s %s", key.c_str(), hkey.c_str(), value.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER) {
        std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << value << " ] failure ! " << std::endl;
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
        _con_pool->returnConnection(connect);
        return false;
    }
    std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << value << " ] success ! " << std::endl;
    freeReplyObject(reply);
    _con_pool->returnConnection(connect);
    return true;
}

bool RedisMgr::HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen)
{
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }

    const char* argv[4];
    size_t argvlen[4];
    argv[0] = "HSET";
    argvlen[0] = 4;
    argv[1] = key;
    argvlen[1] = strlen(key);
    argv[2] = hkey;
    argvlen[2] = strlen(hkey);
    argv[3] = hvalue;
    argvlen[3] = hvaluelen;
    auto reply = (redisReply*)redisCommandArgv(connect, 4, argv, argvlen);
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER) {
        std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << hvalue << " ] failure ! " << std::endl;
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
        _con_pool->returnConnection(connect);
        return false;
    }
    std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << hvalue << " ] success ! " << std::endl;
    freeReplyObject(reply);
    _con_pool->returnConnection(connect);
    return true;
}

std::string RedisMgr::HGet(const std::string& key, const std::string& hkey)
{
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return "";
    }

    redisReply* reply = nullptr;
    const char* argv[3];
    size_t argvlen[3];
    argv[0] = "HGET";
    argvlen[0] = 4;
    argv[1] = key.c_str();
    argvlen[1] = key.length();
    argv[2] = hkey.c_str();
    argvlen[2] = hkey.length();
    reply = (redisReply*)redisCommandArgv(connect, 3, argv, argvlen);
    if (reply == nullptr || reply->type == REDIS_REPLY_NIL) {
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
        std::cout << "Execut command [ HGet " << key << " " << hkey << "  ] failure ! " << std::endl;
        _con_pool->returnConnection(connect);
        return "";
    }

    std::string value = reply->str;
    freeReplyObject(reply);
    std::cout << "Execut command [ HGet " << key << " " << hkey << " ] success ! " << std::endl;
    _con_pool->returnConnection(connect);
    return value;
}

bool RedisMgr::Del(const std::string& key)
{
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }

    auto reply = (redisReply*)redisCommand(connect, "DEL %s", key.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER) {
        std::cout << "Execut command [ Del " << key << " ] failure ! " << std::endl;
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
        _con_pool->returnConnection(connect);
        return false;
    }
    std::cout << "Execut command [ Del " << key << " ] success ! " << std::endl;
    freeReplyObject(reply);
    _con_pool->returnConnection(connect);
    return true;
}

bool RedisMgr::ExistsKey(const std::string& key)
{
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }

    auto reply = (redisReply*)redisCommand(connect, "EXISTS %s", key.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER || reply->integer == 0) {
        std::cout << "Not Found [ Key " << key << " ]  ! " << std::endl;
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
        _con_pool->returnConnection(connect);
        return false;
    }
    std::cout << " Found [ Key " << key << " ] exists ! " << std::endl;
    freeReplyObject(reply);
    _con_pool->returnConnection(connect);
    return true;
}

void RedisMgr::Close()
{
    _con_pool->Close();
}

std::string RedisMgr::acquireLock(const std::string& lockName, int lockTimeout, int acquireTimeout)
{
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return "";
    }
    Defer defer([&connect,this](){
        _con_pool->returnConnection(connect);
        });

    return DistLock::Inst().acquireLock(connect, lockName, lockTimeout,acquireTimeout);
}

bool RedisMgr::releaseLock(const std::string& lockName, const std::string& identifier)
{
    if (identifier.empty()) {
        return true;
    }
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }
    Defer defer([&connect,this]() {
        _con_pool->returnConnection(connect);
        });

    return DistLock::Inst().releaseLock(connect, lockName, identifier);
}
