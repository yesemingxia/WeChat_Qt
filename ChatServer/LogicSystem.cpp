#include "LogicSystem.h"
#include "StatusGrpcClient.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "UserMgr.h"
#include "ChatGrpcClient.h"
#include "CServer.h"
#include <cctype>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>

LogicSystem::LogicSystem() :_b_stop(false) {
	RegisterCallBacks();
	_worker_thread = std::thread(&LogicSystem::DealMsg, this);
}

LogicSystem::~LogicSystem() {
	_b_stop = true;
	_consume.notify_one();
	_worker_thread.join();
}

void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> msg) {
	std::unique_lock<std::mutex> unique_lk(_mutex);
	_msg_que.push(msg);
	if (_msg_que.size() == 1) {
		unique_lk.unlock();
		_consume.notify_one();
	}
}

void LogicSystem::DealMsg() {
	for (;;) {
		std::unique_lock<std::mutex> unique_lk(_mutex);
		while (_msg_que.empty() && !_b_stop) {
			_consume.wait(unique_lk);
		}

		if (_b_stop) {
			while (!_msg_que.empty()) {
				auto msg_node = _msg_que.front();
				std::cout << "recv_msg id  is " << msg_node->_recvnode->_msg_id << std::endl;
				auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
				if (call_back_iter == _fun_callbacks.end()) {
					_msg_que.pop();
					continue;
				}
				call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id,
					std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
				_msg_que.pop();
			}
			break;
		}

		auto msg_node = _msg_que.front();
		std::cout << "recv_msg id  is " << msg_node->_recvnode->_msg_id << std::endl;
		auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
		if (call_back_iter == _fun_callbacks.end()) {
			_msg_que.pop();
			std::cout << "msg id [" << msg_node->_recvnode->_msg_id << "] handler not found" << std::endl;
			continue;
		}
		call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id,
			std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
		_msg_que.pop();
	}
}

void LogicSystem::RegisterCallBacks() {
	_fun_callbacks[MSG_CHAT_LOGIN] = std::bind(&LogicSystem::LoginHandler, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

	_fun_callbacks[ID_SEARCH_USER_REQ] = std::bind(&LogicSystem::SearchInfo, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

	_fun_callbacks[ID_ADD_FRIEND_REQ] = std::bind(&LogicSystem::AddFriendApply, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

	_fun_callbacks[ID_AUTH_FRIEND_REQ] = std::bind(&LogicSystem::AuthFriendApply, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

	_fun_callbacks[ID_TEXT_CHAT_MSG_REQ] = std::bind(&LogicSystem::DealChatTextMsg, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

	_fun_callbacks[ID_HEART_BEAT_REQ] = std::bind(&LogicSystem::HeartBeatHandler, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
}

void LogicSystem::LoginHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);
	auto uid = root["uid"].asInt();
	auto token = root["token"].asString();
	std::cout << "user login uid is  " << uid << " user token  is " << token << std::endl;

	Json::Value  rtvalue;
	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, MSG_CHAT_LOGIN_RSP);
	});

	auto rsp = StatusGrpcClient::GetInstance()->Login(uid, token);
	if (rsp.error() != ErrorCodes::Success) {
		rtvalue["error"] = rsp.error();
		return;
	}

	auto uid_str = std::to_string(uid);
	auto lock_key = LOCK_PREFIX + uid_str;
	auto identifier = RedisMgr::GetInstance()->acquireLock(lock_key, LOCK_TIME_OUT, ACQUIRE_TIME_OUT);
	Defer defer2([identifier, lock_key, this]() {
		RedisMgr::GetInstance()->releaseLock(lock_key, identifier);
	});

	if (identifier.empty()) {
		rtvalue["error"] = ErrorCodes::RPCFailed;
		return;
	}

	std::string redis_session_id = "";
	auto bsuccess = RedisMgr::GetInstance()->Get(USER_SESSION_PREFIX + uid_str, redis_session_id);
	if (bsuccess) {
		auto old_session = _server->GetSession(redis_session_id);
		if (old_session) {
			old_session->Close();
			_server->ClearSession(redis_session_id);
		}
	}

	session->SetUserId(uid);
	std::string base_key = USER_BASE_INFO + uid_str;
	auto user_info = std::make_shared<UserInfo>();
	bool b_base = GetBaseInfo(base_key, uid, user_info);
	if (!b_base) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["uid"] = uid;
	rtvalue["token"] = token;
	rtvalue["name"] = user_info->name;

	auto server_name = ConfigMgr::Inst()["SelfServer"]["Name"];
	RedisMgr::GetInstance()->IncreaseCount(server_name);
	RedisMgr::GetInstance()->Set(USER_SESSION_PREFIX + uid_str, session->GetSessionId());
	UserMgr::GetInstance()->SetUserSession(uid, session);
}

void LogicSystem::SearchInfo(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);

	auto uid_str = root["uid"].asString();
	std::cout << "user SearchInfo uid is  " << uid_str << std::endl;

	Json::Value  rtvalue;

	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_SEARCH_USER_RSP);
	});

	bool b_digit = isPureDigit(uid_str);
	if (b_digit) {
		GetUserByUid(uid_str, rtvalue);
	}
	else {
		GetUserByName(uid_str, rtvalue);
	}
}

bool LogicSystem::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo) {
	std::string info_str = "";
	bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
	if (b_base) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);
		userinfo->uid = root["uid"].asInt();
		userinfo->name = root["name"].asString();
		userinfo->pwd = root["pwd"].asString();
		userinfo->email = root["email"].asString();
		userinfo->nick = root["nick"].asString();
		userinfo->desc = root["desc"].asString();
		userinfo->sex = root["sex"].asInt();
		userinfo->icon = root["icon"].asString();
		std::cout << "user login uid is  " << userinfo->uid << " name  is "
			<< userinfo->name << " pwd is " << userinfo->pwd << " email is " << userinfo->email << std::endl;
	}
	else {
		std::shared_ptr<UserInfo> user_info = nullptr;
		user_info = MysqlMgr::GetInstance()->GetUser(uid);
		if (user_info == nullptr) {
			return false;
		}

		userinfo = user_info;

		Json::Value redis_root;
		redis_root["uid"] = uid;
		redis_root["pwd"] = userinfo->pwd;
		redis_root["name"] = userinfo->name;
		redis_root["email"] = userinfo->email;
		redis_root["nick"] = userinfo->nick;
		redis_root["desc"] = userinfo->desc;
		redis_root["sex"] = userinfo->sex;
		redis_root["icon"] = userinfo->icon;
		RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());
	}

	return true;
}

void LogicSystem::AddFriendApply(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);

	auto uid = root["uid"].asInt();
	auto applyname = root["applyname"].asString();
	auto bakname = root["bakname"].asString();
	auto touid = root["touid"].asInt();
	std::cout << "user login uid is  " << uid << " applyname  is "
		<< applyname << " bakname is " << bakname << " touid is " << touid << std::endl;

	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_ADD_FRIEND_RSP);
	});

	MysqlMgr::GetInstance()->AddFriendApply(uid, touid);

	auto to_str = std::to_string(touid);
	auto to_session = UserMgr::GetInstance()->GetSession(touid);
	if (to_session) {
		Json::Value  notify;
		notify["error"] = ErrorCodes::Success;
		notify["applyuid"] = uid;
		notify["name"] = applyname;
		notify["desc"] = bakname;
		std::string return_str = notify.toStyledString();
		to_session->Send(return_str, ID_NOTIFY_ADD_FRIEND_REQ);
	}
}

void LogicSystem::AuthFriendApply(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);

	auto uid = root["fromuid"].asInt();
	auto touid = root["touid"].asInt();
	auto back_name = root["back"].asString();
	std::cout << "from " << uid << " auth friend to " << touid << std::endl;

	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	auto user_info = std::make_shared<UserInfo>();

	std::string base_key = USER_BASE_INFO + std::to_string(touid);
	bool b_info = GetBaseInfo(base_key, touid, user_info);
	if (!b_info) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_AUTH_FRIEND_RSP);
		return;
	}

	MysqlMgr::GetInstance()->AuthFriendApply(uid, touid);
	MysqlMgr::GetInstance()->AddFriend(uid, touid, back_name);

	rtvalue["name"] = user_info->name;
	rtvalue["nick"] = user_info->nick;
	rtvalue["icon"] = user_info->icon;
	rtvalue["sex"] = user_info->sex;
	rtvalue["uid"] = touid;

	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_AUTH_FRIEND_RSP);
	});

	auto to_session = UserMgr::GetInstance()->GetSession(touid);
	if (to_session) {
		Json::Value  notify;
		notify["error"] = ErrorCodes::Success;
		notify["fromuid"] = uid;
		notify["touid"] = touid;
		std::string base_key = USER_BASE_INFO + std::to_string(uid);
		auto user_info = std::make_shared<UserInfo>();
		bool b_info = GetBaseInfo(base_key, uid, user_info);
		if (b_info) {
			notify["name"] = user_info->name;
			notify["nick"] = user_info->nick;
			notify["icon"] = user_info->icon;
			notify["sex"] = user_info->sex;
		}

		std::string return_str = notify.toStyledString();
		to_session->Send(return_str, ID_NOTIFY_AUTH_FRIEND_REQ);
	}
}

void LogicSystem::DealChatTextMsg(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);

	auto uid = root["fromuid"].asInt();
	auto touid = root["touid"].asInt();

	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["fromuid"] = uid;
	rtvalue["touid"] = touid;

	rtvalue["text_array"] = root["text_array"];

	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_TEXT_CHAT_MSG_RSP);
	});

	auto to_session = UserMgr::GetInstance()->GetSession(touid);
	if (to_session) {
		std::string return_str = rtvalue.toStyledString();
		to_session->Send(return_str, ID_NOTIFY_TEXT_CHAT_MSG_REQ);
	}
	else {
		auto server_rsp = StatusGrpcClient::GetInstance()->GetChatServer(touid);
		if (server_rsp.error() != ErrorCodes::Success) {
			return;
		}

		TextChatMsgReq req;
		req.set_fromuid(uid);
		req.set_touid(touid);

		const auto& text_array = root["text_array"];
		if (text_array.isArray()) {
			for (const auto& item : text_array) {
				TextChatData* msg = req.add_textmsgs();
				msg->set_msgid(item["msgid"].asString());
				msg->set_msgcontent(item["content"].asString());
			}
		}

		auto server_key = server_rsp.host() + ":" + server_rsp.port();
		auto reply = ChatGrpcClient::GetInstance()->NotifyTextChatMsg(server_key, req, rtvalue);
	}
}

void LogicSystem::HeartBeatHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
	Json::Value root;
	root["error"] = ErrorCodes::Success;
	std::string return_str = root.toStyledString();
	session->Send(return_str, ID_HEARTBEAT_RSP);
}

bool LogicSystem::isPureDigit(const std::string& str) {
	for (char c : str) {
		if (!std::isdigit(c)) {
			return false;
		}
	}
	return true;
}

void LogicSystem::GetUserByUid(std::string uid_str, Json::Value& rtvalue) {
	rtvalue["error"] = ErrorCodes::Success;
	int uid = std::stoi(uid_str);
	std::vector<std::shared_ptr<UserInfo>> user_info_vec;
	auto user_info = MysqlMgr::GetInstance()->GetUser(uid);
	if (user_info == nullptr) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	Json::Value user_info_json;
	user_info_json["uid"] = user_info->uid;
	user_info_json["name"] = user_info->name;
	user_info_json["nick"] = user_info->nick;
	user_info_json["desc"] = user_info->desc;
	user_info_json["sex"] = user_info->sex;
	user_info_json["icon"] = user_info->icon;
	rtvalue["user_info"] = user_info_json;
}

void LogicSystem::GetUserByName(std::string name, Json::Value& rtvalue) {
	rtvalue["error"] = ErrorCodes::Success;
	auto user_info = MysqlMgr::GetInstance()->GetUser(name);
	if (user_info == nullptr) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	Json::Value user_info_json;
	user_info_json["uid"] = user_info->uid;
	user_info_json["name"] = user_info->name;
	user_info_json["nick"] = user_info->nick;
	user_info_json["desc"] = user_info->desc;
	user_info_json["sex"] = user_info->sex;
	user_info_json["icon"] = user_info->icon;
	rtvalue["user_info"] = user_info_json;
}

void LogicSystem::SetServer(std::shared_ptr<CServer> server) {
	_server = server;
}
