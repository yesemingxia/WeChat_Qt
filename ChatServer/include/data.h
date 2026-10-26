#pragma once
#include <string>
#include <cstdint>

// 会话线程信息（私聊/群聊统一抽象，用于会话列表分页查询）
struct ChatThreadInfo {
	ChatThreadInfo()
		:_thread_id(0), _type(""), _user1_id(0), _user2_id(0) {}
	int64_t _thread_id;   // 会话线程ID（私聊和群聊共用的递增ID）
	std::string _type;    // 会话类型："private"（私聊）或 "group"（群聊）
	int64_t _user1_id;    // 私聊参与者1的uid（群聊时为0）
	int64_t _user2_id;    // 私聊参与者2的uid（群聊时为0）
};

struct UserInfo {
	UserInfo():name(""), pwd(""),uid(0),email(""),nick(""),desc(""),sex(0), icon(""), back("") {}
	std::string name;
	std::string pwd;
	int uid;
	std::string email;
	std::string nick;
	std::string desc;
	int sex;
	std::string icon;
	std::string back;
};

struct ApplyInfo {
	ApplyInfo(int uid, std::string name, std::string desc,
		std::string icon, std::string nick, int sex, int status)
		:_uid(uid),_name(name),_desc(desc),
		_icon(icon),_nick(nick),_sex(sex),_status(status){}

	int _uid;
	std::string _name;
	std::string _desc;
	std::string _icon;
	std::string _nick;
	int _sex;
	int _status;
};

//聊天消息信息
struct ChatMessage {
	int message_id;
	int thread_id;
	int sender_id;
	int recv_id;
	std::string unique_id;
	std::string content;
	std::string chat_time;
	int status;
};

// 查询结果结构，增加next_cursor字段
struct PageResult {
	std::vector<ChatMessage> messages;
	bool load_more;
	int next_cursor;  // 本页最后一条message_id，用于下次查询
};

