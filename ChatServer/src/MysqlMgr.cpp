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

bool MysqlMgr::GetFriendApplyInfo(int uid, std::vector<std::shared_ptr<ApplyInfo>>& apply_list) {
    return _dao.GetFriendApplyInfo(uid, apply_list);
}

bool MysqlMgr::GetFriendList(int uid, std::vector<std::shared_ptr<UserInfo>>& friend_list) {
	return _dao.GetFriendList(uid, friend_list);
}

int MysqlMgr::GetApplyState(int uid, int touid)
{
	return _dao.GetApplyState(uid, touid);
}

bool MysqlMgr::CheckFriend(int uid, int touid)
{
	return _dao.CheckFriend(uid, touid);
}

bool MysqlMgr::AddFriendApply(int uid, int touid) {    return _dao.AddFriendApply(uid, touid);
}

bool MysqlMgr::AuthFriendApply(int uid, int touid) {
    return _dao.AuthFriendApply(uid, touid);
}

bool MysqlMgr::RejectFriendApply(int uid, int touid) {
    return _dao.RejectFriendApply(uid, touid);
}

bool MysqlMgr::AddFriend(int uid, int touid, const std::string& back_name,
    std::vector<std::shared_ptr<message::AddFriendMsg>>& chat_datas) {
    return _dao.AddFriend(uid, touid, back_name, chat_datas);
}

bool MysqlMgr::GetUserThreads(int64_t userId, int64_t lastId, int pageSize,
    std::vector<std::shared_ptr<ChatThreadInfo>>& threads,
    bool& loadMore, int& nextLastId)
{
    // 桥接 int& 与 DAO 层的 int64_t&（thread_id 在库里是 BIGINT）
    int64_t nid = lastId;
    bool ret = _dao.GetUserThreads(userId, lastId, pageSize, threads, loadMore, nid);
    nextLastId = static_cast<int>(nid);
    return ret;
}

bool MysqlMgr::CreatePrivateChat(int user1_id, int user2_id, int& thread_id)
{
    return _dao.CreatePrivateChat(user1_id, user2_id, thread_id);
}