#include "MysqlMgr.h"


MysqlMgr::~MysqlMgr() {

}

int MysqlMgr::RegUser(const std::string& name, const std::string& email, const std::string& pwd)
{
    return _dao.RegUser(name, email, pwd);
}

MysqlMgr::MysqlMgr() {
}

bool MysqlMgr::CheckEmail(const std::string& name, const std::string& email) {
    return _dao.CheckEmail(name, email);
}

bool MysqlMgr::UpdatePwd(const std::string& name, const std::string& pwd) {
    return _dao.UpdatePwd(name, pwd);
}

bool MysqlMgr::CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo) {
    return _dao.CheckPwd(name, pwd, userInfo);
}

std::shared_ptr<UserInfo> MysqlMgr::GetUser(int uid) {
    return _dao.GetUser(uid);
}

std::shared_ptr<UserInfo> MysqlMgr::GetUser(const std::string& name) {
    return _dao.GetUser(name);
}

bool MysqlMgr::AddFriendApply(int uid, int touid) {
    return _dao.AddFriendApply(uid, touid);
}

bool MysqlMgr::AuthFriendApply(int uid, int touid) {
    return _dao.AuthFriendApply(uid, touid);
}

bool MysqlMgr::AddFriend(int uid, int touid, const std::string& back_name) {
    return _dao.AddFriend(uid, touid, back_name);
}
