#include "MysqlDao.h"
#include "ConfigMgr.h"
#include "MysqlMgr.h"

MysqlDao::MysqlDao()
{
    auto& cfg = ConfigMgr::Inst();
    std::string host = cfg.GetValue("Mysql", "Host");
    // 适配新版：将字符串 port 转换为 unsigned int（X DevAPI 要求端口为数值类型）
    std::string port_str = cfg.GetValue("Mysql", "Port");
    unsigned int port = 33060; // 默认 X DevAPI 端口
    if (!port_str.empty()) {
        port = static_cast<unsigned int>(std::stoul(port_str));
    }
    std::string pwd = cfg.GetValue("Mysql", "Passwd");
    std::string schema = cfg.GetValue("Mysql", "Schema");
    std::string user = cfg.GetValue("Mysql", "User");
    schema_ = schema;

    // 适配新版：调用 MySqlPool 构造函数（host + port 分离，不再拼接字符串）
    pool_.reset(new MySqlPool(host, port, user, pwd, schema, 5));
}

MysqlDao::~MysqlDao() {
    if (pool_) { // 防止空指针
        pool_->Close();
    }
}

int MysqlDao::RegUser(const std::string& name,
    const std::string& email,
    const std::string& pwd)
{
    auto con = pool_->getConnection();
    if (!con) return -1;

    try {
        // ⚠️ 强制选择数据库
        con->sql("USE " + schema_).execute();

        // 调用存储过程，只传 3 个 IN 参数
        auto stmt = con->sql("CALL reg_user(?, ?, ?)");
        stmt.bind(name);
        stmt.bind(email);
        stmt.bind(pwd);

        auto res = stmt.execute();

        int result = -1;
        if (res.hasData()) {
            auto row = res.fetchOne();
            if (row) {
                result = row[0].get<int>();
            }
        }

        pool_->returnConnection(std::move(con));
        return result;
    }
    catch (const mysqlx::Error& e) {
        std::cerr << "MySQL Error: " << e.what() << std::endl;
        pool_->returnConnection(std::move(con));
        return -1;
    }
}

bool MysqlDao::CheckEmail(const std::string& name, const std::string& email) {
    auto con = pool_->getConnection();
    try {
        if (!con) return false;

        con->sql("USE " + schema_).execute();
        auto result = con->sql("SELECT email FROM user WHERE name = ?")
            .bind(name)
            .execute();

        for (auto row : result) {
            std::string db_email = row[0].get<std::string>();
            std::cout << "Check Email: " << db_email << std::endl;
            if (db_email != email) {
                return false;
            }
            return true;
        }
        return false;
    }
    catch (const mysqlx::Error& e) {
        std::cerr << "MySQL Error: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::UpdatePwd(const std::string& name, const std::string& newpwd) {
    auto con = pool_->getConnection();
    try {
        if (!con) return false;

        con->sql("USE " + schema_).execute();
        auto result = con->sql("UPDATE user SET pwd = ? WHERE name = ?")
            .bind(newpwd)
            .bind(name)
            .execute();

        std::cout << "Updated rows: " << result.getAffectedItemsCount() << std::endl;
        return true;
    }
    catch (const mysqlx::Error& e) {
        std::cerr << "MySQL Error: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo) {
    auto con = pool_->getConnection(); // 返回 std::unique_ptr<mysqlx::Session>

    // 使用 Defer 确保连接归还连接池
    Defer defer([this, &con]() {
        if (con) {
            pool_->returnConnection(std::move(con));
        }
        });

    try {
        if (!con) {
            return false;
        }

        // 执行参数化查询
        // 注意选择字段的顺序：uid(0), name(1), pwd(2)
        con->sql("USE " + schema_).execute();
        auto result = con->sql("SELECT uid, name, pwd FROM user WHERE name = ?")
            .bind(name)
            .execute();

        auto row = result.fetchOne();
        if (!row) {
            return false; // 用户不存在
        }

        // --- 核心修复部分 ---

        // 1. 获取密码用于验证
        // mysqlx::Row 必须通过数字索引访问，row[2] 对应 SELECT 中的 pwd
        std::string db_pwd = row[2].get<std::string>();

        if (pwd != db_pwd) {
            return false; // 密码错误
        }

        // 2. 填充 userInfo 结构体
        // 使用显式类型转换 (Type Cast)，这是 X DevAPI 最推荐且兼容性最好的写法
        userInfo.uid = row[0].get<int64_t>();      // 对应 uid
        userInfo.name = row[1].get<std::string>();  // 对应 name
        userInfo.pwd = std::move(db_pwd);    // 性能优化：移动字符串
        userInfo.email = "";

        return true;
    }
    catch (const mysqlx::Error& e) {
        std::cerr << "MySQL X DevAPI Error: " << e.what()
            << " (Code: " << e.what() << ")" << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "Standard Exception: " << e.what() << std::endl;
        return false;
    }
}

std::shared_ptr<UserInfo> MysqlDao::GetUser(int uid)
{
    auto con = pool_->getConnection();
    if (!con) return nullptr;

    Defer defer([this, &con]() {
        if (con) {
            pool_->returnConnection(std::move(con));
        }
    });

    try {
        con->sql("USE " + schema_).execute();
        auto result = con->sql(
            "SELECT uid, name, pwd, email, nick, brief, sex, icon FROM user WHERE uid = ?")
            .bind(uid)
            .execute();

        auto row = result.fetchOne();
        if (!row) {
            return nullptr;
        }

        auto user_info = std::make_shared<UserInfo>();
        user_info->uid   = row[0].get<int>();
        user_info->name  = row[1].get<std::string>();
        user_info->pwd   = row[2].get<std::string>();
        user_info->email = row[3].get<std::string>();
        user_info->nick  = row[4].get<std::string>();
        user_info->desc  = row[5].get<std::string>();
        user_info->sex   = row[6].get<int>();
        user_info->icon  = row[7].get<std::string>();
        return user_info;
    }
    catch (const mysqlx::Error& e) {
        std::cerr << "MySQL Error in GetUser: " << e.what() << std::endl;
        return nullptr;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in GetUser: " << e.what() << std::endl;
        return nullptr;
    }
}

std::shared_ptr<UserInfo> MysqlDao::GetUser(const std::string& name)
{
    auto con = pool_->getConnection();
    if (!con) return nullptr;

    Defer defer([this, &con]() {
        if (con) {
            pool_->returnConnection(std::move(con));
        }
    });

    try {
        con->sql("USE " + schema_).execute();
        auto result = con->sql(
            "SELECT uid, name, pwd, email, nick, brief, sex, icon FROM user WHERE name = ?")
            .bind(name)
            .execute();

        auto row = result.fetchOne();
        if (!row) {
            return nullptr;
        }

        auto user_info = std::make_shared<UserInfo>();
        user_info->uid = row[0].get<int>();
        user_info->name = row[1].get<std::string>();
        user_info->pwd = row[2].get<std::string>();
        user_info->email = row[3].get<std::string>();
        user_info->nick = row[4].get<std::string>();
        user_info->desc = row[5].get<std::string>();
        user_info->sex = row[6].get<int>();
        user_info->icon = row[7].get<std::string>();
        return user_info;
    }
    catch (const mysqlx::Error& e) {
        std::cerr << "MySQL Error in GetUser(name): " << e.what() << std::endl;
        return nullptr;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in GetUser(name): " << e.what() << std::endl;
        return nullptr;
    }
}

bool MysqlDao::AddFriendApply(int uid, int touid)
{
    auto con = pool_->getConnection();
    if (!con) return false;

    Defer defer([this, &con]() {
        if (con) {
            pool_->returnConnection(std::move(con));
        }
    });

    try {
        con->sql("USE " + schema_).execute();
        con->sql("INSERT INTO friend_apply(applyuid, touid, status) VALUES(?, ?, 0)")
            .bind(uid)
            .bind(touid)
            .execute();
        return true;
    }
    catch (const mysqlx::Error& e) {
        std::cerr << "MySQL Error in AddFriendApply: " << e.what() << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in AddFriendApply: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::AuthFriendApply(int uid, int touid)
{
    auto con = pool_->getConnection();
    if (!con) return false;

    Defer defer([this, &con]() {
        if (con) {
            pool_->returnConnection(std::move(con));
        }
    });

    try {
        con->sql("USE " + schema_).execute();
        con->sql("UPDATE friend_apply SET status = 1 WHERE applyuid = ? AND touid = ?")
            .bind(uid)
            .bind(touid)
            .execute();
        return true;
    }
    catch (const mysqlx::Error& e) {
        std::cerr << "MySQL Error in AuthFriendApply: " << e.what() << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in AuthFriendApply: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::AddFriend(int uid, int touid, const std::string& back_name)
{
    auto con = pool_->getConnection();
    if (!con) return false;

    Defer defer([this, &con]() {
        if (con) {
            pool_->returnConnection(std::move(con));
        }
    });

    try {
        con->sql("USE " + schema_).execute();
        con->sql("INSERT INTO friend(uid, frienduid, back) VALUES(?, ?, ?)")
            .bind(uid)
            .bind(touid)
            .bind(back_name)
            .execute();
        con->sql("INSERT INTO friend(uid, frienduid, back) VALUES(?, ?, ?)")
            .bind(touid)
            .bind(uid)
            .bind("")
            .execute();
        return true;
    }
    catch (const mysqlx::Error& e) {
        std::cerr << "MySQL Error in AddFriend: " << e.what() << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in AddFriend: " << e.what() << std::endl;
        return false;
    }
}
