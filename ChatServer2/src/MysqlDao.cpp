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

bool MysqlDao::GetFriendApplyInfo(int uid, std::vector<std::shared_ptr<ApplyInfo>>& apply_list)
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
        auto result = con->sql(
            "SELECT u.uid, u.name, u.brief, u.icon, u.nick, u.sex, fa.status "
            "FROM friend_apply fa JOIN user u ON fa.from_uid = u.uid "
            "WHERE fa.to_uid = ?")
            .bind(uid)
            .execute();

        bool any = false;
        for (auto row : result) {
            any = true;
            int from_uid = row[0].get<int>();
            std::string name = row[1].get<std::string>();
            std::string desc = row[2].get<std::string>();
            std::string icon = row[3].get<std::string>();
            std::string nick = row[4].get<std::string>();
            int sex = row[5].get<int>();
            int status = row[6].get<int>();
            apply_list.emplace_back(std::make_shared<ApplyInfo>(
                from_uid, name, desc, icon, nick, sex, status));
        }

        return any;
    }
    catch (const mysqlx::Error& e) {
        std::cerr << "MySQL Error in GetFriendApplyInfo: " << e.what() << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in GetFriendApplyInfo: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::GetFriendList(int uid, std::vector<std::shared_ptr<UserInfo>>& friend_list)
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
        auto result = con->sql(
            "SELECT u.uid, u.name, u.icon, u.nick, u.sex, u.brief, f.back "
            "FROM friend f JOIN user u ON f.frienduid = u.uid "
            "WHERE f.uid = ?")
            .bind(uid)
            .execute();

        bool any = false;
        for (auto row : result) {
            any = true;
            auto info = std::make_shared<UserInfo>();
            info->uid = row[0].get<int>();
            info->name = row[1].get<std::string>();
            info->icon = row[2].get<std::string>();
            info->nick = row[3].get<std::string>();
            info->sex = row[4].get<int>();
            info->desc = row[5].get<std::string>();
            info->back = row[6].get<std::string>();
            friend_list.emplace_back(info);
        }

        return any;
    }
    catch (const mysqlx::Error& e) {
        std::cerr << "MySQL Error in GetFriendList: " << e.what() << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in GetFriendList: " << e.what() << std::endl;
        return false;
    }
}

int MysqlDao::GetApplyState(int uid, int touid)
{
    auto con = pool_->getConnection();
    if (!con) return -1;

    Defer defer([this, &con]() {
        if (con) {
            pool_->returnConnection(std::move(con));
        }
    });

    try {
        con->sql("USE " + schema_).execute();
        auto res = con->sql("SELECT status FROM friend_apply WHERE from_uid = ? AND to_uid = ?")
            .bind(uid)
            .bind(touid)
            .execute();

        auto row = res.fetchOne();
        if (row) {
            return (int)row[0];
        }
        return -1;
    }
    catch (const mysqlx::Error& e) {
        std::cerr << "Exception in GetApplyState: " << e.what() << std::endl;
        return -1;
    }
}

bool MysqlDao::CheckFriend(int uid, int touid)
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
        auto res = con->sql("SELECT id FROM friend WHERE uid = ? AND frienduid = ?")
            .bind(uid)
            .bind(touid)
            .execute();

        auto row = res.fetchOne();
        return (bool)row;
    }
    catch (const mysqlx::Error& e) {
        std::cerr << "Exception in CheckFriend: " << e.what() << std::endl;
        return false;
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
        // If a request already exists, refresh it instead of failing.
        con->sql("INSERT INTO friend_apply(from_uid, to_uid, status) VALUES(?, ?, 0) "
                 "ON DUPLICATE KEY UPDATE status = 0")
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
        auto res = con->sql("UPDATE friend_apply SET status = 1 WHERE from_uid = ? AND to_uid = ?")
            .bind(touid)
            .bind(uid)
            .execute();
        return res.getAffectedItemsCount() > 0;
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

bool MysqlDao::RejectFriendApply(int uid, int touid)
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
        auto res = con->sql("UPDATE friend_apply SET status = 2 WHERE from_uid = ? AND to_uid = ?")
            .bind(touid)
            .bind(uid)
            .execute();
        return res.getAffectedItemsCount() > 0;
    }
    catch (const mysqlx::Error& e) {
        std::cerr << "MySQL Error in RejectFriendApply: " << e.what() << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in RejectFriendApply: " << e.what() << std::endl;
        return false;
    }
}

bool MysqlDao::AddFriend(int uid, int touid, const std::string& back_name,
    std::vector<std::shared_ptr<message::AddFriendMsg>>& chat_datas)
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
        con->startTransaction();

        // 1. 建立双向好友关系（uid = 同意人，touid = 申请人）
        con->sql("INSERT IGNORE INTO friend(uid, frienduid, back) VALUES(?, ?, ?)")
            .bind(uid)
            .bind(touid)
            .bind(back_name)
            .execute();
        con->sql("INSERT IGNORE INTO friend(uid, frienduid, back) VALUES(?, ?, ?)")
            .bind(touid)
            .bind(uid)
            .bind("")
            .execute();

        // 2. 复用或新建私聊会话，保持 min/max 规则
        int64_t uid1 = std::min<int64_t>(uid, touid);
        int64_t uid2 = std::max<int64_t>(uid, touid);
        int64_t thread_id = 0;

        auto threadRes = con->sql(
            "SELECT thread_id FROM private_chat "
            "WHERE user1_id = ? AND user2_id = ? FOR UPDATE;")
            .bind(uid1)
            .bind(uid2)
            .execute();

        auto threadRow = threadRes.fetchOne();
        if (threadRow) {
            thread_id = threadRow[0].get<int64_t>();
        }
        else {
            con->sql("INSERT INTO chat_thread(type, created_at) "
                     "VALUES('private', NOW());").execute();

            auto idRes = con->sql("SELECT LAST_INSERT_ID();").execute();
            auto idRow = idRes.fetchOne();
            thread_id = idRow[0].get<int64_t>();

            con->sql("INSERT INTO private_chat(thread_id, user1_id, user2_id, created_at) "
                     "VALUES(?, ?, ?, NOW());")
                .bind(thread_id)
                .bind(uid1)
                .bind(uid2)
                .execute();
        }

        // 3. 插入“成为好友”的初始消息，并回填 chat_datas
        con->sql("INSERT INTO chat_message(thread_id, sender_id, recv_id, content, "
                 "created_at, updated_at, status) "
                 "VALUES(?, ?, ?, ?, NOW(), NOW(), 2);")
            .bind(thread_id)
            .bind(uid)
            .bind(touid)
            .bind("We are friends now!")
            .execute();

        auto msgIdRes = con->sql("SELECT LAST_INSERT_ID();").execute();
        auto msgIdRow = msgIdRes.fetchOne();
        int64_t msg_id = msgIdRow[0].get<int64_t>();

        auto chat_msg = std::make_shared<message::AddFriendMsg>();
        chat_msg->set_sender_id(uid);
        chat_msg->set_msg_id(static_cast<int32_t>(msg_id));
        chat_msg->set_thread_id(static_cast<int32_t>(thread_id));
        chat_msg->set_msgcontent("We are friends now!");
        chat_msg->set_unique_id("");
        chat_msg->set_status(2);
        chat_datas.push_back(chat_msg);

        con->commit();
        return true;
    }
    catch (const mysqlx::Error& e) {
        std::cerr << "MySQL Error in AddFriend: " << e.what() << std::endl;
        try {
            con->rollback();
        }
        catch (...) {
        }
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in AddFriend: " << e.what() << std::endl;
        try {
            con->rollback();
        }
        catch (...) {
        }
        return false;
    }
}

bool MysqlDao::GetUserThreads(
    int64_t userId,
    int64_t lastId,
    int      pageSize,
    std::vector<std::shared_ptr <ChatThreadInfo>>& threads,
    bool& loadMore,
    int64_t& nextLastId)
{
    // 初始状态
    loadMore = false;
    nextLastId = lastId;
    threads.clear();

    auto con = pool_->getConnection();
    if (!con) {
        return false;
    }

    Defer defer([this, &con]() {
        pool_->returnConnection(std::move(con));
        });

    try {
        con->sql("USE " + schema_).execute();

        // 分页查询：CTE + UNION ALL + ORDER BY thread_id + LIMIT pageSize+1
        // LIMIT 值为服务端内部计算的整数，直接拼入 SQL，其余参数用 bind 防注入
        std::string sql =
            "WITH all_threads AS ( "
            "  SELECT thread_id, 'private' AS type, user1_id, user2_id "
            "    FROM private_chat "
            "   WHERE (user1_id = ? OR user2_id = ?) "
            "     AND thread_id > ? "
            "  UNION ALL "
            "  SELECT thread_id, 'group' AS type, 0 AS user1_id, 0 AS user2_id "
            "    FROM group_chat_member "
            "   WHERE user_id = ? "
            "     AND thread_id > ? "
            ") "
            "SELECT thread_id, type, user1_id, user2_id "
            "  FROM all_threads "
            " ORDER BY thread_id "
            " LIMIT " + std::to_string(pageSize + 1) + ";";

        auto result = con->sql(sql)
            .bind(userId)   // private.user1_id
            .bind(userId)   // private.user2_id
            .bind(lastId)   // private.thread_id > lastId
            .bind(userId)   // group.user_id
            .bind(lastId)   // group.thread_id > lastId
            .execute();

        // 先把所有行读到临时容器
        std::vector<std::shared_ptr<ChatThreadInfo>> tmp;
        for (auto row : result) {
            auto cti = std::make_shared<ChatThreadInfo>();
            cti->_thread_id = row[0].get<int64_t>();
            cti->_type = row[1].get<std::string>();
            cti->_user1_id = row[2].get<int64_t>();
            cti->_user2_id = row[3].get<int64_t>();
            tmp.push_back(cti);
        }

        // 多取到一条说明还有下一页
        if ((int)tmp.size() > pageSize) {
            loadMore = true;
            tmp.pop_back();  // 丢掉第 pageSize+1 条
        }

        // 如果还有数据，更新 nextLastId 为最后一条的 thread_id
        if (!tmp.empty()) {
            nextLastId = tmp.back()->_thread_id;
        }

        // 移入输出向量
        threads = std::move(tmp);
    }
    catch (const mysqlx::Error& e) {
        std::cerr << "MySQL Error in GetUserThreads: " << e.what() << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in GetUserThreads: " << e.what() << std::endl;
        return false;
    }

    return true;
}

bool MysqlDao::CreatePrivateChat(int user1_id, int user2_id, int& thread_id)
{
    auto con = pool_->getConnection();
    if (!con) {
        return false;
    }
    Defer defer([this, &con]() {
        pool_->returnConnection(std::move(con));
        });

    try {
        // 选中数据库 schema
        con->sql("USE " + schema_).execute();
        // 开启事务
        con->sql("START TRANSACTION;").execute();

        int64_t uid1 = std::min((int64_t)user1_id, (int64_t)user2_id);
        int64_t uid2 = std::max((int64_t)user1_id, (int64_t)user2_id);

        // 1. 查询私聊是否存在，FOR UPDATE行锁
        auto res = con->sql(
            "SELECT thread_id FROM private_chat "
            "WHERE (user1_id = ? AND user2_id = ?) FOR UPDATE;")
            .bind(uid1)
            .bind(uid2)
            .execute();

        bool exists = false;
        int64_t exist_thread_id = 0;
        for (auto row : res) {
            exists = true;
            exist_thread_id = row[0].get<int64_t>();
            break;
        }
        if (exists) {
            thread_id = static_cast<int>(exist_thread_id);
            con->sql("COMMIT;").execute();
            return true;
        }

        // 2. 插入 chat_thread
        con->sql("INSERT INTO chat_thread (type, created_at) VALUES ('private', NOW());")
            .execute();

        // 获取自增ID LAST_INSERT_ID
        auto idRes = con->sql("SELECT LAST_INSERT_ID();").execute();
        int64_t new_thread_id = 0;
        for (auto row : idRes) {
            new_thread_id = row[0].get<int64_t>();
            break;
        }
        thread_id = static_cast<int>(new_thread_id);

        // 3. 插入 private_chat
        con->sql(
            "INSERT INTO private_chat (thread_id, user1_id, user2_id, created_at) "
            "VALUES (?, ?, ?, NOW());")
            .bind(new_thread_id)
            .bind(uid1)
            .bind(uid2)
            .execute();

        // 提交事务
        con->sql("COMMIT;").execute();
        return true;
    }
    catch (const mysqlx::Error& e) {
        std::cerr << "MySQL Error in CreatePrivateChat: " << e.what() << std::endl;
        con->sql("ROLLBACK;").execute();
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in CreatePrivateChat: " << e.what() << std::endl;
        con->sql("ROLLBACK;").execute();
        return false;
    }
    return false;
}
