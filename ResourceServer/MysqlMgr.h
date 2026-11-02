#pragma once
#include "const.h"
#include "MysqlDao.h"
#include "data.h"
#include "Singleton.h"
//#include <rpcasync.h>

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
        bool& loadMore, int& nextLastId);

    bool CreatePrivateChat(int user1_id, int user2_id, int& thread_id);
    bool AddChatMsg(std::vector<std::shared_ptr<ChatMessage>>& chat_datas);
    std::shared_ptr<PageResult> LoadChatMsg(int thread_id, int last_message_id, int page_size);

    // ResourceServer 专用：文件/聊天图片相关接口，ChatServer1/2 的 MysqlMgr 里没有
    // @crnl 2026-10-30
    bool UpdateUserIcon(int uid, const std::string& icon);               // 更新头像文件名
    bool UpdateUploadStatus(int chat_messag_id);                        // 图片上传完成后更新消息状态
    std::shared_ptr<ChatImgInfo> GetImgInfoByMsgId(int msg_id);          // 按消息 id 查图片信息
    std::shared_ptr<ChatMessage> GetChatMsgById(int message_id);         // 按消息 id 查完整聊天消息
private:
    MysqlMgr();
    MysqlDao  _dao;
};

