#pragma once
#include "const.h"
#include "data.h"
#include "message.pb.h"
#include <mysqlx/xdevapi.h>
#include <thread>
#include <iostream>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <atomic>
#include <utility>
#include <string>

class SqlConnection {
public:
    SqlConnection(std::unique_ptr<mysqlx::Session> con, int64_t lasttime)
        :_con(std::move(con)), _last_time(lasttime) {
    }
    std::unique_ptr<mysqlx::Session> _con;
    int64_t _last_time;  // 最后使用时间（毫秒级时间戳）
};

class MySqlPool {
public:
    // 适配新版：url 拆分为 host + port（新版 X DevAPI 用 33060 端口）
    MySqlPool(const std::string& host, unsigned int port,
        const std::string& user, const std::string& pass,
        const std::string& schema, int poolSize, int64_t timeout = 300000)  // 新增超时参数（5分钟）
        : host_(host), port_(port), user_(user), pass_(pass),
        schema_(schema), poolSize_(poolSize), timeout_(timeout), b_stop_(false), _fail_count(0) {
        try {
            // 初始化连接池
            for (int i = 0; i < poolSize_; ++i) {
                std::unique_ptr<mysqlx::Session> con(
                    new mysqlx::Session(host_, port_, user_, pass_)
                );
                con->getSchema(schema_);
                // 封装为 SqlConnection，记录初始时间
                pool_.push(std::make_unique<SqlConnection>(std::move(con), getCurrentTime()));
            }

            // 启动连接检查线程（补回 check_thread 逻辑）
            check_thread_ = std::thread(&MySqlPool::checkConnectionPro, this);
            std::cout << "mysql pool init success, check thread started" << std::endl;
        }
        catch (const mysqlx::Error& e) {
            std::cout << "mysql pool init failed: " << e.what() << std::endl;
        }
    }

    std::unique_ptr<mysqlx::Session> getConnection() {
    std::unique_ptr<SqlConnection> sql_con;      // 先声明，默认是空的
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cond_.wait_for(lock, std::chrono::seconds(5),
                [this] { return b_stop_ || !pool_.empty(); })) {
            return nullptr;                       // 超时；return 时 lock 析构会自动解锁
        }
        if (b_stop_) {
            return nullptr;                       // 同上，自动解锁
        }
        sql_con = std::move(pool_.front());       // 所有权转移到本线程
        pool_.pop();
    }                                             // ← 锁在这里释放

    // 下面两行在锁外执行，是安全的
    sql_con->_last_time = getCurrentTime();
    return std::move(sql_con->_con);
}

    // 适配新版：接收 mysqlx::Session 替代 sql::Connection
    void returnConnection(std::unique_ptr<mysqlx::Session> con) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (b_stop_) {
                return;
            }
        // 封装为 SqlConnection，记录归还时间
        pool_.push(std::make_unique<SqlConnection>(std::move(con), getCurrentTime()));
        }

        cond_.notify_one();
    }

    void Close() {
        b_stop_ = true;
        cond_.notify_all();
        check_cv_.notify_all();
        // 等待检查线程退出
        if (check_thread_.joinable()) {
            check_thread_.join();
        }
    }

    ~MySqlPool() {
        Close();  // 确保线程退出
        std::unique_lock<std::mutex> lock(mutex_);
        // 清空连接队列
        while (!pool_.empty()) {
            pool_.pop();
        }
    }

private:
    // 补回：获取当前毫秒级时间戳（用于判断过期）
    int64_t getCurrentTime() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    // void checkConnectionPro() {
    //     //1先读取“目标处理数”
    //     size_t targetCount;
    //     {
    //         std::lock_guard<std::mutex> lock(mutex_);
    //         targetCount = pool_.size();
    //     }

    //     //2 当前已经处理数
    //     size_t processedCount = 0;
    //     auto now = std::chrono::system_clock::now().time_since_epoch();
    //     long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    //     while (processedCount < targetCount) {
    //         std::unique_ptr<SqlConnection> con;
    //         {
    //             std::lock_guard<std::mutex> lock(mutex_);
    //             if (pool_.empty()) {
    //                 break;
    //             }
    //             con = std::move(pool_.front());
    //             pool_.pop();
    //         }
    //         bool health = true;
    //         //解锁后做检查 
    //         if (timestamp - con->_last_time >= 5000) {
    //             try {
    //                 con->_con->sql("SELECT 1").execute();   // con->_con 是 mysqlx::Session
    //                 con->_last_time = timestamp;
    //             }
    //             catch (mysqlx::Error& e) {
    //                 std::cout << "expired connection replaced" << std::endl;
    //                 health = false;
    //                 _fail_count++;
    //             }
    //         }
    //         if(health)
    //         {
    //             std::lock_guard<std::mutex> lock(mutex_);
    //             pool_.push(std::move(con));
    //             cond_.notify_one();
    //         }

    //         ++processedCount;
        
    //     }
        
    //     while (_fail_count > 0) {
    //         auto b_res = reconnect(timestamp);
    //         if (b_res) {
    //             _fail_count--;
    //         }
    //         else {
    //             break;
    //         }
    //     }

    // }

    // bool reconnect(long long timestamp) {
    //     try {
    //         std::unique_ptr<mysqlx::Session> new_con(
    //             new mysqlx::Session(host_, port_, user_, pass_)
    //         );
    //         new_con->getSchema(schema_);
    //         auto newCon = std::make_unique<SqlConnection>(std::move(new_con), timestamp);
    //         {
    //             std::lock_guard<std::mutex> lock(mutex_);
    //             pool_.push(std::move(newCon));
    //         }
    //         cond_.notify_one();
    //         return true;
    //     }
    //     catch (mysqlx::Error& e) {
    //         std::cout << "Reconnect failed,error is" << e.what();
    //         return false;
    //     }

    // }

    void checkConnectionPro()
        {
            while (!b_stop_.load()) {
                // 这个函数是 std::thread 的入口，异常一旦逃出线程函数就是 std::terminate，
                // 整个 ChatServer 会被直接 abort，所以这里必须兜住。
                try {
                    doHealthCheckPass();
                }
                catch (const std::exception& e) {
                    std::cerr << "[mysqlpool] health pass exception: "
                              << e.what() << std::endl;
                }
                catch (...) {
                    std::cerr << "[mysqlpool] health pass unknown exception" << std::endl;
                }

                // 每 30 秒巡检一次；Close() 会通过 check_cv_ 立即唤醒线程
                std::unique_lock<std::mutex> lock(check_mtx_);
                check_cv_.wait_for(
                    lock,
                    std::chrono::seconds(30),
                    [this]() { return b_stop_.load(); }
                );
            }
        }

    void doHealthCheckPass()
    {
        if (b_stop_.load()) {
            return;
        }

        const long long timestamp = getCurrentTime();

        std::size_t targetCount = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            targetCount = pool_.size();
        }

        // 每轮最多检查进入本轮时的空闲连接数量
        for (std::size_t i = 0;
            i < targetCount && !b_stop_.load();
            ++i) {

            std::unique_ptr<SqlConnection> con;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (pool_.empty()) {
                    break;
                }

                con = std::move(pool_.front());
                pool_.pop();
            }

            bool health = true;

            // 只有空闲超过 5 秒的连接才执行探活
            if (timestamp - con->_last_time >= 5000) {
                try {
                    con->_con->sql("SELECT 1").execute();
                    con->_last_time = getCurrentTime();
                }
                catch (const mysqlx::Error& e) {
                    std::cerr
                        << "[mysqlpool] health check failed: "
                        << e.what() << std::endl;

                    health = false;

                    // 记录一条确认损坏、等待补回的连接
                    _fail_count.fetch_add(1);
                }
                catch (const std::exception& e) {
                    // mysqlx::Error 派生自 std::exception，会先被上面的分支接住；
                    // 这里兜住其余异常（比如 bad_alloc），否则会逃出检查线程。
                    std::cerr
                        << "[mysqlpool] health check exception: "
                        << e.what() << std::endl;

                    health = false;
                    _fail_count.fetch_add(1);
                }
            }

            if (health) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);

                    // 关闭过程中不再把连接放回池子
                    if (b_stop_.load()) {
                        return;
                    }

                    pool_.push(std::move(con));
                }

                cond_.notify_one();
            }

            // health == false 时，con 在本轮循环结束时自动销毁
        }

        // 只补实际损坏的连接。
        // 不能使用 pool_.size() < poolSize_，
        // 因为业务线程借走的连接不在 pool_ 中。
        while (!b_stop_.load() && _fail_count.load() > 0) {
            if (!reconnect(getCurrentTime())) {
                // MySQL 尚未恢复，保留失败数量，30 秒后继续补连
                break;
            }

            _fail_count.fetch_sub(1);
        }
    }

    bool reconnect(long long timestamp)
    {
        try {
            std::unique_ptr<mysqlx::Session> new_con(
                new mysqlx::Session(host_, port_, user_, pass_)
            );

            new_con->getSchema(schema_);

            std::unique_ptr<SqlConnection> newCon =
                std::make_unique<SqlConnection>(
                    std::move(new_con),
                    timestamp
                );

            {
                std::lock_guard<std::mutex> lock(mutex_);

                // 新建连接期间可能调用了 Close()
                if (b_stop_.load()) {
                    return false;
                }

                pool_.push(std::move(newCon));
            }

            cond_.notify_one();
            return true;
        }
        catch (const mysqlx::Error& e) {
            std::cerr
                << "[mysqlpool] reconnect failed: "
                << e.what() << std::endl;
            return false;
        }
        catch (const std::exception& e) {
            std::cerr
                << "[mysqlpool] reconnect exception: "
                << e.what() << std::endl;
            return false;
        }
    }


    // 成员变量补充（补回 check_thread 相关）
    std::string host_;
    unsigned int port_;
    std::string user_;
    std::string pass_;
    std::string schema_;
    int poolSize_;
    int64_t timeout_;  // 连接超时时间（毫秒）
    std::queue<std::unique_ptr<SqlConnection>> pool_;  // 改为存储 SqlConnection 封装类
    std::mutex mutex_;
    std::condition_variable cond_;
    std::atomic<bool> b_stop_;
    std::thread check_thread_;  // 连接检查线程（补回）
    std::mutex  check_mtx_;                 // 只给检查线程的等待用，不和业务锁混用
    std::condition_variable check_cv_;      // 让 Close() 能立刻叫醒检查线程
    std::atomic<int> _fail_count;
};



class MysqlDao
{
public:
    MysqlDao();
    ~MysqlDao();
    int RegUser(const std::string& name, const std::string& email, const std::string& pwd);
    bool CheckEmail(const std::string& name, const std::string& email);
    bool UpdatePwd(const std::string& name, const std::string& newpwd);
    bool CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo);
    std::shared_ptr<UserInfo> GetUser(int uid);
    std::shared_ptr<UserInfo> GetUser(const std::string& name);
    bool GetFriendApplyInfo(int uid, std::vector<std::shared_ptr<ApplyInfo>>& apply_list);
    bool GetFriendList(int uid, std::vector<std::shared_ptr<UserInfo>>& friend_list);
    int GetApplyState(int uid, int touid);
    bool CheckFriend(int uid, int touid);
    bool AddFriendApply(int uid, int touid);
    bool AuthFriendApply(int uid, int touid);
    bool RejectFriendApply(int uid, int touid);
    bool AddFriend(int uid, int touid, const std::string& back_name,
        std::vector<std::shared_ptr<message::AddFriendMsg>>& chat_datas);
    bool GetUserThreads(int64_t userId, int64_t lastId, int pageSize,
        std::vector<std::shared_ptr<ChatThreadInfo>>& threads,
        bool& loadMore, int64_t& nextLastId);
    bool AddChatMsg(std::vector<std::shared_ptr<ChatMessage>>& chat_datas);
    bool CreatePrivateChat(int user1_id, int user2_id, int& thread_id);
    std::shared_ptr<PageResult>LoadChatMsg(int thread_id, int last_message_id, int page_size);
private:
    std::unique_ptr<MySqlPool> pool_;
    std::string schema_;
};
