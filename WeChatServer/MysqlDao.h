#pragma once
#include "const.h"
#include <mysqlx/xdevapi.h>


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
        schema_(schema), poolSize_(poolSize), timeout_(timeout), b_stop_(false) {
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
            check_thread_ = std::thread(&MySqlPool::checkExpiredConnections, this);
            std::cout << "mysql pool init success, check thread started" << std::endl;
        }
        catch (const mysqlx::Error& e) {
            std::cout << "mysql pool init failed: " << e.what() << std::endl;
        }
    }

    // 适配新版：返回 mysqlx::Session 替代 sql::Connection
    std::unique_ptr<mysqlx::Session> getConnection() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] {
            if (b_stop_) {
                return true;
            }
            return !pool_.empty();
            });

        if (b_stop_) {
            return nullptr;
        }

        // 取出封装的 SqlConnection
        std::unique_ptr<SqlConnection> sql_con(std::move(pool_.front()));
        pool_.pop();

        // 更新最后使用时间
        sql_con->_last_time = getCurrentTime();
        // 剥离 Session 返回
        return std::move(sql_con->_con);
    }

    // 适配新版：接收 mysqlx::Session 替代 sql::Connection
    void returnConnection(std::unique_ptr<mysqlx::Session> con) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (b_stop_) {
            return;
        }
        // 封装为 SqlConnection，记录归还时间
        pool_.push(std::make_unique<SqlConnection>(std::move(con), getCurrentTime()));
        cond_.notify_one();
    }

    void Close() {
        b_stop_ = true;
        cond_.notify_all();
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

    // 补回：检查过期连接的核心线程函数（原 check_thread 逻辑）
    void checkExpiredConnections() {
        while (!b_stop_) {
            // 每 30 秒检查一次
            std::this_thread::sleep_for(std::chrono::seconds(30));

            std::unique_lock<std::mutex> lock(mutex_);
            int64_t now = getCurrentTime();
            std::queue<std::unique_ptr<SqlConnection>> new_pool;

            // 遍历所有连接，清理过期的
            while (!pool_.empty()) {
                std::unique_ptr<SqlConnection> con = std::move(pool_.front());
                pool_.pop();

                // 判断是否过期：当前时间 - 最后使用时间 > 超时时间
                if (now - con->_last_time < timeout_) {
                    // 未过期，保留
                    new_pool.push(std::move(con));
                }
                else {
                    // 过期，丢弃并重新创建一个新连接补充
                    try {
                        std::unique_ptr<mysqlx::Session> new_con(
                            new mysqlx::Session(host_, port_, user_, pass_)
                        );
                        new_con->getSchema(schema_);
                        new_pool.push(std::make_unique<SqlConnection>(std::move(new_con), now));
                        std::cout << "expired connection replaced" << std::endl;
                    }
                    catch (const mysqlx::Error& e) {
                        std::cout << "replace expired connection failed: " << e.what() << std::endl;
                    }
                }
            }

            // 替换为清理后的连接池
            pool_.swap(new_pool);
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
private:
    std::unique_ptr<MySqlPool> pool_;
    std::string schema_;
};
