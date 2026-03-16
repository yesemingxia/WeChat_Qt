#pragma once
#include "const.h"
#include "MysqlDao.h"
#include "data.h"
#include "Singleton.h"
#include <rpcasync.h>

class MysqlMgr : public Singleton<MysqlMgr>
{
    friend class Singleton<MysqlMgr>;
public:
    ~MysqlMgr();
    int RegUser(const std::string& name, const std::string& email, const std::string& pwd);
    bool CheckEmail(const std::string& name, const std::string& email);
    bool UpdatePwd(const std::string& name, const std::string& pwd);
    bool CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo);
    std::shared_ptr<UserInfo> GetUser(int uid);
    std::shared_ptr<UserInfo> GetUser(const std::string& name);
    bool AddFriendApply(int uid, int touid);
    bool AuthFriendApply(int uid, int touid);
    bool AddFriend(int uid, int touid, const std::string& back_name);
private:
    MysqlMgr();
    MysqlDao  _dao;
};

