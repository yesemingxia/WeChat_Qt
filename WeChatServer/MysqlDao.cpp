#include "MysqlDao.h"
#include "ConfigMgr.h"
#include "MysqlMgr.h"

MysqlDao::MysqlDao()
{
    auto& cfg = ConfigMgr::GetInstance();
    const auto& host = cfg["Mysql"]["Host"];
    // 适配新版：将字符串 port 转换为 unsigned int（X DevAPI 要求端口为数值类型）
    const auto& port_str = cfg["Mysql"]["Port"];
    unsigned int port = 33060; // 默认 X DevAPI 端口
    if (!port_str.empty()) {
        port = static_cast<unsigned int>(stoul(port_str));
    }
    const auto& pwd = cfg["Mysql"]["Passwd"];
    const auto& schema = cfg["Mysql"]["Schema"];
    const auto& user = cfg["Mysql"]["User"];
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
        std::string db_pwd = (std::string)row[2];

        if (pwd != db_pwd) {
            return false; // 密码错误
        }

        // 2. 填充 userInfo 结构体
        // 使用显式类型转换 (Type Cast)，这是 X DevAPI 最推荐且兼容性最好的写法
        userInfo.uid = (int64_t)row[0];      // 对应 uid
        userInfo.name = (std::string)row[1];  // 对应 name
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
