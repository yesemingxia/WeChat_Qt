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

bool MysqlMgr::AddFriendApply(int uid, int touid) {
    return _dao.AddFriendApply(uid, touid);
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
bool MysqlMgr::AddChatMsg(std::vector<std::shared_ptr<ChatMessage>>& chat_datas) {
    return _dao.AddChatMsg(chat_datas);
}
std::shared_ptr<PageResult> MysqlMgr::LoadChatMsg(int thread_id, int last_message_id, int page_size) {
    return _dao.LoadChatMsg(thread_id, last_message_id, page_size);
}

// 更新用户头像文件名：供 FileWorker 在最后一块头像上传完成后调用
// ResourceServer 专属接口，ChatServer1/2 没有
// @crnl 2026-10-30
bool MysqlMgr::UpdateUserIcon(int uid, const std::string& icon) {
    return _dao.UpdateHeadInfo(uid, icon);
}

// 聊天图片上传完成后更新 chat_message.status
// ResourceServer 专属接口，ChatServer1/2 没有
// @crnl 2026-10-30
bool MysqlMgr::UpdateUploadStatus(int chat_messag_id)
{
    return _dao.UpdateUploadStatus(chat_messag_id);
}

// 按消息 id 查图片发送者/接收者/文件名
// ResourceServer 专属接口，ChatServer1/2 没有
// @crnl 2026-10-30
std::shared_ptr<ChatImgInfo> MysqlMgr::GetImgInfoByMsgId(int msg_id)
{
    return _dao.GetImgInfoByMsgId(msg_id);
}

// 按消息 id 查完整聊天消息，gRPC 通知 ChatServer 时使用
// ResourceServer 专属接口，ChatServer1/2 没有
// @crnl 2026-10-30
std::shared_ptr<ChatMessage> MysqlMgr::GetChatMsgById(int message_id)
{
    return _dao.GetChatMsgById(message_id);
}