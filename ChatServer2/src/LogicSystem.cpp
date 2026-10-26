#include "LogicSystem.h"
#include "StatusGrpcClient.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "UserMgr.h"
#include "ChatGrpcClient.h"
#include "CServer.h"
#include <cctype>
#include <chrono>
#include <ctime>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include"ChatServiceImpl.h"
#include "utils.h"

namespace {
bool TryGetInt(const Json::Value& root, const char* key, int& out) {
	if (!root.isMember(key)) {
		return false;
	}
	const auto& v = root[key];
	if (v.isInt()) {
		out = v.asInt();
		return true;
	}
	if (v.isUInt()) {
		out = static_cast<int>(v.asUInt());
		return true;
	}
	if (v.isString()) {
		try {
			out = std::stoi(v.asString());
			return true;
		}
		catch (...) {
			return false;
		}
	}
	return false;
}

bool TryGetString(const Json::Value& root, const char* key, std::string& out) {
	if (!root.isMember(key)) {
		return false;
	}
	const auto& v = root[key];
	if (v.isString()) {
		out = v.asString();
		return true;
	}
	if (v.isBool() || v.isInt() || v.isUInt() || v.isDouble()) {
		out = v.asString();
		return true;
	}
	return false;
}

bool TryGetBool(const Json::Value& root, const char* key, bool& out) {
	if (!root.isMember(key)) {
		return false;
	}
	const auto& v = root[key];
	if (v.isBool()) {
		out = v.asBool();
		return true;
	}
	if (v.isInt() || v.isUInt()) {
		out = v.asInt() != 0;
		return true;
	}
	if (v.isString()) {
		const auto s = v.asString();
		if (s == "1" || s == "true" || s == "TRUE") {
			out = true;
			return true;
		}
		if (s == "0" || s == "false" || s == "FALSE") {
			out = false;
			return true;
		}
	}
	return false;
}

//本处已由utils 所代替
//std::string getCurrentTimestamp() {
//	std::time_t now = std::time(nullptr);
//	std::tm local_tm{};
//	localtime_s(&local_tm, &now);
//	char buffer[20] = { 0 };
//	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local_tm);
//	return buffer;
//}
} // namespace

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

//此处单线程查表 可能发生错误没有错误处理 @crnl 
//有忙等嫌疑 待核实 @crnl
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

	_fun_callbacks[ID_LOAD_CHAT_THREAD_REQ] = std::bind(&LogicSystem::GetUserThreadsHandler, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
	
	_fun_callbacks[ID_CREATE_PRIVATE_CHAT_REQ] = std::bind(&LogicSystem::CreatePrivateChat, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
}

void LogicSystem::LoginHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
	Json::Reader reader;
	Json::Value root;
	Json::Value  rtvalue;
	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, MSG_CHAT_LOGIN_RSP);
		});

	if (!reader.parse(msg_data, root) || !root.isObject()) {
		rtvalue["error"] = ErrorCodes::Error_Json;
		return;
	}

	/*auto uid = root["uid"].asInt();
	auto token = root["token"].asString();
	std::cout << "user login uid is  " << uid << " user token  is " << token << std::endl;*/

	int uid = 0;
	std::string token;
	if (!TryGetInt(root, "uid", uid) || !TryGetString(root, "token", token)
		|| uid <= 0 || token.empty()) {
		rtvalue["error"] = ErrorCodes::Error_Json;
		return;
	}

	std::cout << "[LogicSystem] LoginHandler uid=" << uid << std::endl;

	std::string uid_str = std::to_string(uid);
	std::string token_key = USERTOKENPREFIX + uid_str;
	std::string token_value = "";
	bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
	if (!success) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	if (token_value != token) {
		rtvalue["error"] = ErrorCodes::TokenInvalid;
		return;
	}

	rtvalue["error"] = ErrorCodes::Success;

	std::string base_key = USER_BASE_INFO + uid_str;
	auto user_info = std::make_shared<UserInfo>();
	bool b_base = GetBaseInfo(base_key, uid, user_info);
	if (!b_base) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	rtvalue["uid"] = uid;
	rtvalue["pwd"] = user_info->pwd;
	rtvalue["name"] = user_info->name;
	rtvalue["email"] = user_info->email;
	rtvalue["nick"] = user_info->nick;
	rtvalue["desc"] = user_info->desc;
	rtvalue["sex"] = user_info->sex;
	rtvalue["icon"] = user_info->icon;

	std::vector<std::shared_ptr<ApplyInfo>> apply_list;
	auto b_apply = GetFriendApplyInfo(uid, apply_list);
	if (b_apply) {
		for (auto& apply : apply_list) {
			Json::Value obj;
			obj["name"] = apply->_name;
			obj["uid"] = apply->_uid;
			obj["icon"] = apply->_icon;
			obj["nick"] = apply->_nick;
			obj["sex"] = apply->_sex;
			obj["desc"] = apply->_desc;
			obj["status"] = apply->_status;
			rtvalue["apply_list"].append(obj);
		}
	}

	std::vector<std::shared_ptr<UserInfo>> friend_list;
	bool b_friend_list = GetFriendList(uid, friend_list);
	(void)b_friend_list;
	for (auto& friend_ele : friend_list) {
		Json::Value obj;
		obj["name"] = friend_ele->name;
		obj["uid"] = friend_ele->uid;
		obj["icon"] = friend_ele->icon;
		obj["nick"] = friend_ele->nick;
		obj["sex"] = friend_ele->sex;
		obj["desc"] = friend_ele->desc;
		obj["back"] = friend_ele->back;
		rtvalue["friend_list"].append(obj);
	}

	auto server_name = ConfigMgr::Inst().GetValue("SelfServer", "Name");
	{
		auto lock_key = LOCK_PREFIX + uid_str;
		auto identifier = RedisMgr::GetInstance()->acquireLock(lock_key, LOCK_TIME_OUT, ACQUIRE_TIME_OUT);
		Defer defer2([this, identifier, lock_key]() {
			RedisMgr::GetInstance()->releaseLock(lock_key, identifier);
		});

		std::string uid_ip_value = "";
		auto uid_ip_key = USERIPPREFIX + uid_str;
		bool b_ip = RedisMgr::GetInstance()->Get(uid_ip_key, uid_ip_value);
		if (b_ip) {
			auto& cfg = ConfigMgr::Inst();
			auto self_name = cfg["SelfServer"]["Name"];
			if (uid_ip_value == self_name) {
				auto old_session = UserMgr::GetInstance()->GetSession(uid);
				if (old_session) {
					old_session->NotifyOffline(uid);
					_server->ClearSession(old_session->GetSessionId());
				}
			}
			else {
				KickUserReq kick_req;
				kick_req.set_uid(uid);
				ChatGrpcClient::GetInstance()->NotifyKickUser(uid_ip_value, kick_req);
			}
		}

		session->SetUserId(uid);
		std::string ipkey = USERIPPREFIX + uid_str;
		RedisMgr::GetInstance()->Set(ipkey, server_name);
		UserMgr::GetInstance()->SetUserSession(uid, session);
		std::string uid_session_key = USER_SESSION_PREFIX + uid_str;
		RedisMgr::GetInstance()->Set(uid_session_key, session->GetSessionId());
	}

	RedisMgr::GetInstance()->Set(server_name,session->GetSessionId());
	return;
}

void LogicSystem::SearchInfo(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
	Json::Reader reader;
	Json::Value root;
	Json::Value  rtvalue;

	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_SEARCH_USER_RSP);
	});

	// @cuiruoni 搜索用户前先校验请求体，避免 JSON 非法时继续走后续逻辑
	if (!reader.parse(msg_data, root)) {
		rtvalue["error"] = ErrorCodes::Error_Json;
		return;
	}

	std::string uid_str;
	// @cuiruoni 搜索关键字不能为空，空字符串会导致拼出无效 redis key
	if (!TryGetString(root, "uid", uid_str) || uid_str.empty()) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	std::cout << "user SearchInfo uid is  " << uid_str << std::endl;

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
		if (!reader.parse(info_str, root)) {
			return false;
		}
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

bool LogicSystem::GetFriendApplyInfo(int uid, std::vector<std::shared_ptr<ApplyInfo>>& apply_list) {
	return MysqlMgr::GetInstance()->GetFriendApplyInfo(uid, apply_list);
}

bool LogicSystem::GetFriendList(int uid, std::vector<std::shared_ptr<UserInfo>>& friend_list) {
	return MysqlMgr::GetInstance()->GetFriendList(uid, friend_list);
}

void LogicSystem::AddFriendApply(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
	Json::Reader reader;
	Json::Value root;
	if (!reader.parse(msg_data, root)) {
		Json::Value rtvalue;
		rtvalue["error"] = ErrorCodes::Error_Json;
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_ADD_FRIEND_RSP);
		return;
	}

	int uid = 0;
	bool has_uid = TryGetInt(root, "uid", uid);
	if (!has_uid) {
		has_uid = TryGetInt(root, "applyuid", uid);
	}
	if (!has_uid) {
		has_uid = TryGetInt(root, "fromuid", uid);
	}

	std::string applyname;
	if (!TryGetString(root, "applyname", applyname)) {
		TryGetString(root, "name", applyname);
	}

	std::string bakname;
	if (!TryGetString(root, "bakname", bakname)) {
		if (!TryGetString(root, "desc", bakname)) {
			TryGetString(root, "back", bakname);
		}
	}

	int touid = 0;
	bool has_touid = TryGetInt(root, "touid", touid);

	std::cout << "user login uid is  " << uid << " applyname  is "
		<< applyname << " bakname is " << bakname << " touid is " << touid << std::endl;

	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_ADD_FRIEND_RSP);
	});

	if (!has_uid || !has_touid) {
		rtvalue["error"] = ErrorCodes::Error_Json;
		return;
	}

	if (uid <= 0 || touid <= 0 || uid == touid) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	// 1. 检查是否已经是好友
	if (MysqlMgr::GetInstance()->CheckFriend(uid, touid)) {
		rtvalue["error"] = ErrorCodes::FriendInvalid; // 已经是好友了
		return;
	}

	// 2. 检查是否有反向申请（B 已经申请了 A）
	int back_state = MysqlMgr::GetInstance()->GetApplyState(touid, uid);
	if (back_state == 0) {
		// 如果对方已经申请了自己，那么这次申请直接视为“同意”
		if (!MysqlMgr::GetInstance()->AuthFriendApply(uid, touid)) {
			rtvalue["error"] = ErrorCodes::RPCFailed;
			return;
		}

		std::vector<std::shared_ptr<message::AddFriendMsg>> chat_datas;
		if (!MysqlMgr::GetInstance()->AddFriend(uid, touid, bakname, chat_datas)) {
			rtvalue["error"] = ErrorCodes::RPCFailed;
			return;
		}

		rtvalue["chat_datas"] = Json::Value(Json::arrayValue);
		auto chat_time = getCurrentTimestamp();
		for (auto& chat_data : chat_datas) {
			Json::Value chat;
			chat["sender"] = chat_data->sender_id();
			chat["msg_id"] = chat_data->msg_id();
			chat["thread_id"] = chat_data->thread_id();
			chat["unique_id"] = chat_data->unique_id();
			chat["msg_content"] = chat_data->msgcontent();
			chat["chat_time"] = chat_time;
			chat["status"] = chat_data->status();
			rtvalue["chat_datas"].append(chat);
		}

		// --- 通知逻辑 ---
		// A 现在成了同意者(approver)，B 是原申请人(applicant)
		int approver = uid;
		int applicant = touid;

		auto to_session = UserMgr::GetInstance()->GetSession(applicant);
		if (to_session) {
			Json::Value  notify;
			notify["error"] = ErrorCodes::Success;
			notify["fromuid"] = approver;
			notify["touid"] = applicant;
			notify["chat_datas"] = rtvalue["chat_datas"];
			std::string base_key = USER_BASE_INFO + std::to_string(approver);
			auto user_info = std::make_shared<UserInfo>();
			if (GetBaseInfo(base_key, approver, user_info)) {
				notify["name"] = user_info->name;
				notify["nick"] = user_info->nick;
				notify["icon"] = user_info->icon;
				notify["sex"] = user_info->sex;
			}
			to_session->Send(notify.toStyledString(), ID_NOTIFY_AUTH_FRIEND_REQ);
		}
		else {
			// 如果不在本地，发 RPC 给对方所在的服务器
			auto server_rsp = StatusGrpcClient::GetInstance()->GetChatServer(applicant);
			if (static_cast<int>(server_rsp.error()) == ErrorCodes::Success) {
				AuthFriendReq auth_req;
				auth_req.set_fromuid(approver);
				auth_req.set_touid(applicant);
				for (auto& chat_data : chat_datas) {
					auth_req.add_textmsgs()->CopyFrom(*chat_data);
				}
				auto server_key = server_rsp.host() + ":" + server_rsp.port();
				ChatGrpcClient::GetInstance()->NotifyAuthFriend(server_key, auth_req);
			}
		}

		// 返回给当前用户 A 的回复包，需要包含 B 的信息，让 A 前端刷新好友列表
		std::string applicant_base_key = USER_BASE_INFO + std::to_string(applicant);
		auto applicant_info = std::make_shared<UserInfo>();
		if (GetBaseInfo(applicant_base_key, applicant, applicant_info)) {
			rtvalue["name"] = applicant_info->name;
			rtvalue["nick"] = applicant_info->nick;
			rtvalue["icon"] = applicant_info->icon;
			rtvalue["sex"] = applicant_info->sex;
		}
		rtvalue["uid"] = applicant;
		return;
	}

	// 3. 正常申请流程
	if (!MysqlMgr::GetInstance()->AddFriendApply(uid, touid)) {
		rtvalue["error"] = ErrorCodes::RPCFailed;
		return;
	}

	std::string base_key = USER_BASE_INFO + std::to_string(uid);
	auto apply_info = std::make_shared<UserInfo>();
	bool b_info = GetBaseInfo(base_key, uid, apply_info);

	auto to_str = std::to_string(touid);

	auto to_ip_key = USERIPPREFIX + to_str;
	std::string to_ip_value = "";
	bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);
	auto& cfg = ConfigMgr::Inst();
	auto self_name = cfg["SelfServer"]["Name"]; 

	if (!b_ip) {
		auto server_rsp = StatusGrpcClient::GetInstance()->GetChatServer(touid);
		if (server_rsp.error() != ErrorCodes::Success) {
			return;
		}
		to_ip_value = server_rsp.host() + ":" + server_rsp.port();
	}

	if (to_ip_value == self_name) {
		auto to_session = UserMgr::GetInstance()->GetSession(touid);
		if (to_session) {
			Json::Value  notify;
			notify["error"] = ErrorCodes::Success;
			notify["applyuid"] = uid;
			notify["name"] = applyname;
			notify["desc"] = bakname;
			if (b_info) {
				notify["icon"] = apply_info->icon;
				notify["sex"] = apply_info->sex;
				notify["nick"] = apply_info->nick;
			}
			std::string return_str = notify.toStyledString();
			to_session->Send(return_str, ID_NOTIFY_ADD_FRIEND_REQ);
		}
		return;
	}

	AddFriendReq add_req;
	add_req.set_applyuid(uid);
	add_req.set_touid(touid);
	add_req.set_name(applyname);
	add_req.set_desc(bakname);
	if (b_info) {
		add_req.set_icon(apply_info->icon);
		add_req.set_sex(apply_info->sex);
		add_req.set_nick(apply_info->nick);
	}

	std::string server_key = to_ip_value;
	if (to_ip_value != self_name) {
		auto host = cfg[to_ip_value]["Host"];
		auto port = cfg[to_ip_value]["Port"];
		if (!host.empty() && !port.empty()) {
			server_key = host + ":" + port;
		}
	}
	ChatGrpcClient::GetInstance()->NotifyAddFriend(server_key, add_req);
	
}

void LogicSystem::AuthFriendApply(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
	Json::Reader reader;
	Json::Value root;
	if (!reader.parse(msg_data, root)) {
		Json::Value  rtvalue;
		rtvalue["error"] = ErrorCodes::Error_Json;
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_AUTH_FRIEND_RSP);
		return;
	}

	int uid = 0;
	int touid = 0;
	std::string back_name;
	bool has_uid = TryGetInt(root, "fromuid", uid);
	bool has_touid = TryGetInt(root, "touid", touid);
	TryGetString(root, "back", back_name);
	bool agree = true;
	TryGetBool(root, "agree", agree);

	if (!has_uid || !has_touid) {
		Json::Value  rtvalue;
		rtvalue["error"] = ErrorCodes::Error_Json;
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_AUTH_FRIEND_RSP);
		return;
	}

	if (uid <= 0 || touid <= 0 || uid == touid) {
		Json::Value  rtvalue;
		rtvalue["error"] = ErrorCodes::UidInvalid;
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_AUTH_FRIEND_RSP);
		return;
	}

	int approver = touid;
	int applicant = uid;
	int session_uid = session->GetUserId();
	if (session_uid > 0) {
		if (session_uid == uid && session_uid != touid) {
			approver = uid;
			applicant = touid;
		}
		else if (session_uid == touid && session_uid != uid) {
			approver = touid;
			applicant = uid;
		}
	}

	std::cout << "approver " << approver << " auth friend applicant " << applicant
		<< " agree=" << (agree ? "true" : "false") << std::endl;

	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["agree"] = agree;

	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_AUTH_FRIEND_RSP);
	});
	
	auto user_info = std::make_shared<UserInfo>();
	std::string base_key = USER_BASE_INFO + std::to_string(applicant);
	bool b_info = GetBaseInfo(base_key, applicant, user_info);

	if (!agree) {
		if (!MysqlMgr::GetInstance()->RejectFriendApply(approver, applicant)) {
			rtvalue["error"] = ErrorCodes::RPCFailed;
			return;
		}
		auto to_session = UserMgr::GetInstance()->GetSession(applicant);
		if (to_session) {
			Json::Value  notify;
			notify["error"] = ErrorCodes::Success;
			notify["fromuid"] = approver;
			notify["touid"] = applicant;
			notify["agree"] = false;
			std::string return_str = notify.toStyledString();
			to_session->Send(return_str, ID_NOTIFY_AUTH_FRIEND_REQ);
		}
		else {
			auto server_rsp = StatusGrpcClient::GetInstance()->GetChatServer(applicant);
			if (server_rsp.error() != ErrorCodes::Success) {
				rtvalue["error"] = ErrorCodes::RPCFailed;
				return;
			}

			AuthFriendReq auth_req;
			auth_req.set_fromuid(approver);
			auth_req.set_touid(applicant);
			auto server_key = server_rsp.host() + ":" + server_rsp.port();
			auto auth_rsp = ChatGrpcClient::GetInstance()->NotifyAuthFriend(server_key, auth_req);
			if (auth_rsp.error() != ErrorCodes::Success) {
				rtvalue["error"] = auth_rsp.error();
			}
		}
		return;
	}

	if (!b_info) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	if (!MysqlMgr::GetInstance()->AuthFriendApply(approver, applicant)) {
		rtvalue["error"] = ErrorCodes::RPCFailed;
		return;
	}
	std::vector<std::shared_ptr<message::AddFriendMsg>> chat_datas;
	if (!MysqlMgr::GetInstance()->AddFriend(approver, applicant, back_name, chat_datas)) {
		rtvalue["error"] = ErrorCodes::RPCFailed;
		return;
	}

	rtvalue["name"] = user_info->name;
	rtvalue["nick"] = user_info->nick;
	rtvalue["icon"] = user_info->icon;
	rtvalue["sex"] = user_info->sex;
	rtvalue["uid"] = applicant;

	auto to_session = UserMgr::GetInstance()->GetSession(applicant);
	if (to_session) {
		Json::Value  notify;
		notify["error"] = ErrorCodes::Success;
		notify["fromuid"] = approver;
		notify["touid"] = applicant;
		std::string base_key = USER_BASE_INFO + std::to_string(approver);
		auto user_info = std::make_shared<UserInfo>();
		bool b_info = GetBaseInfo(base_key, approver, user_info);
		if (b_info) {
			notify["name"] = user_info->name;
			notify["nick"] = user_info->nick;
			notify["icon"] = user_info->icon;
			notify["sex"] = user_info->sex;
		}

		std::string return_str = notify.toStyledString();
		to_session->Send(return_str, ID_NOTIFY_AUTH_FRIEND_REQ);
		return;
	}

	auto server_rsp = StatusGrpcClient::GetInstance()->GetChatServer(applicant);
	if (server_rsp.error() != ErrorCodes::Success) {
		return;
	}

	AuthFriendReq auth_req;
	auth_req.set_fromuid(approver);
	auth_req.set_touid(applicant);

	auto server_key = server_rsp.host() + ":" + server_rsp.port();
	ChatGrpcClient::GetInstance()->NotifyAuthFriend(server_key, auth_req);
}

void LogicSystem::DealChatTextMsg(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
	Json::Reader reader;
	Json::Value root;
	if (!reader.parse(msg_data, root)) {
		Json::Value rtvalue;
		rtvalue["error"] = ErrorCodes::Error_Json;
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_TEXT_CHAT_MSG_RSP);
		return;
	}

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

	if (uid <= 0 || touid <= 0 || uid == touid) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	const auto& text_array = root["text_array"];
	if (!text_array.isArray()) {
		rtvalue["error"] = ErrorCodes::Error_Json;
		return;
	}

	auto to_session = UserMgr::GetInstance()->GetSession(touid);
	if (to_session) {
		std::string return_str = rtvalue.toStyledString();
		to_session->Send(return_str, ID_NOTIFY_TEXT_CHAT_MSG_REQ);
	}
	else {
		auto server_rsp = StatusGrpcClient::GetInstance()->GetChatServer(touid);
		if (server_rsp.error() != ErrorCodes::Success) {
			rtvalue["error"] = ErrorCodes::RPCFailed;
			return;
		}

		TextChatMsgReq req;
		req.set_fromuid(uid);
		req.set_touid(touid);

		if (text_array.isArray()) {
			for (const auto& item : text_array) {
				TextChatData* msg = req.add_textmsgs();
				int msg_id_value = 0;
				if (TryGetInt(item, "msgid", msg_id_value)) {
					msg->set_msg_id(msg_id_value);
				}
				msg->set_msgcontent(item["content"].asString());
			}
		}

		auto server_key = server_rsp.host() + ":" + server_rsp.port();
		auto reply = ChatGrpcClient::GetInstance()->NotifyTextChatMsg(server_key, req, rtvalue);
		if (reply.error() != ErrorCodes::Success) {
			rtvalue["error"] = reply.error();
		}
	}
}

void LogicSystem::HeartBeatHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
	/*Json::Value root;
	Json::Reader reader;
	Json::Value rtvalue;
	reader.parse(msg_data, root);
	auto uid = root["fromuid"].asInt();

	rtvalue["error"] = ErrorCodes::Success;
	Defer defer([this,&rtvalue,session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_HEARTBEAT_RSP);
		});*/
	Json::Reader reader;
	Json::Value root;
	if (!reader.parse(msg_data, root) || !root.isObject()) {
		Json::Value rtvalue;
		rtvalue["error"] = ErrorCodes::Error_Json;
		session->Send(rtvalue.toStyledString(), ID_HEARTBEAT_RSP);   // 失败也要回包，否则客户端心跳超时判断不了
		return;
	}

	int uid = 0;
	if (!TryGetInt(root, "fromuid", uid) || uid <= 0) {
		Json::Value rtvalue;
		rtvalue["error"] = ErrorCodes::UidInvalid;
		session->Send(rtvalue.toStyledString(), ID_HEARTBEAT_RSP);
		return;
	}

	Json::Value rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["fromuid"] = uid;          // 比直接回显 root 更可控，不会把脏字段原样反射回客户端
	session->Send(rtvalue.toStyledString(), ID_HEARTBEAT_RSP);
}

bool LogicSystem::isPureDigit(const std::string& str) {
	// @cuiruoni 空字符串不能视为合法数字，避免误走 uid 查询分支
	if (str.empty()) {
		return false;
	}
	for (char c : str) {
		if (!std::isdigit(static_cast<unsigned char>(c))) {
			return false;
		}
	}
	return true;
}

void LogicSystem::GetUserByUid(std::string uid_str, Json::Value& rtvalue) {
	rtvalue["error"] = ErrorCodes::Success;

	std::string base_key = USER_BASE_INFO + uid_str;

	//优先查redis中查询用户信息
	std::string info_str = "";
	bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
	if (b_base) {
		Json::Reader reader;
		Json::Value root;
		if (!reader.parse(info_str, root)) {
			rtvalue["error"] = ErrorCodes::Error_Json;
			return;
		}
		auto uid = root["uid"].asInt();
		auto name = root["name"].asString();
		auto pwd = root["pwd"].asString();
		auto email = root["email"].asString();
		auto nick = root["nick"].asString();
		auto desc = root["desc"].asString();
		auto sex = root["sex"].asInt();
		auto icon = root["icon"].asString();
		std::cout << "user  uid is  " << uid << " name  is "
			<< name << " pwd is " << pwd << " email is " << email << " icon is " << icon << std::endl;

		rtvalue["uid"] = uid;
		rtvalue["pwd"] = pwd;
		rtvalue["name"] = name;
		rtvalue["email"] = email;
		rtvalue["nick"] = nick;
		rtvalue["desc"] = desc;
		rtvalue["sex"] = sex;
		rtvalue["icon"] = icon;
		return;
	}

	int uid = 0;
	// @cuiruoni 兜底拦截非法 uid，避免空串或脏数据触发 stoi 异常
	try {
		uid = std::stoi(uid_str);
	}
	catch (...) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}
	//redis中没有则查询mysql
	//查询数据库
	std::shared_ptr<UserInfo> user_info = nullptr;
	user_info = MysqlMgr::GetInstance()->GetUser(uid);
	if (user_info == nullptr) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	//将数据库内容写入redis缓存
	Json::Value redis_root;
	redis_root["uid"] = user_info->uid;
	redis_root["pwd"] = user_info->pwd;
	redis_root["name"] = user_info->name;
	redis_root["email"] = user_info->email;
	redis_root["nick"] = user_info->nick;
	redis_root["desc"] = user_info->desc;
	redis_root["sex"] = user_info->sex;
	redis_root["icon"] = user_info->icon;

	RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());

	//返回数据
	rtvalue["uid"] = user_info->uid;
	rtvalue["pwd"] = user_info->pwd;
	rtvalue["name"] = user_info->name;
	rtvalue["email"] = user_info->email;
	rtvalue["nick"] = user_info->nick;
	rtvalue["desc"] = user_info->desc;
	rtvalue["sex"] = user_info->sex;
	rtvalue["icon"] = user_info->icon;

}

void LogicSystem::GetUserByName(std::string name, Json::Value& rtvalue) {
	rtvalue["error"] = ErrorCodes::Success;

	std::string base_key = NAME_INFO + name;

	//优先查redis中查询用户信息
	std::string info_str = "";
	bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
	if (b_base) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);
		auto uid = root["uid"].asInt();
		auto name = root["name"].asString();
		auto pwd = root["pwd"].asString();
		auto email = root["email"].asString();
		auto nick = root["nick"].asString();
		auto desc = root["desc"].asString();
		auto sex = root["sex"].asInt();
		std::cout << "user  uid is  " << uid << " name  is "
			<< name << " pwd is " << pwd << " email is " << email << std::endl;

		rtvalue["uid"] = uid;
		rtvalue["pwd"] = pwd;
		rtvalue["name"] = name;
		rtvalue["email"] = email;
		rtvalue["nick"] = nick;
		rtvalue["desc"] = desc;
		rtvalue["sex"] = sex;
		return;
	}

	//redis中没有则查询mysql
	//查询数据库
	std::shared_ptr<UserInfo> user_info = nullptr;
	user_info = MysqlMgr::GetInstance()->GetUser(name);
	if (user_info == nullptr) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	//将数据库内容写入redis缓存
	Json::Value redis_root;
	redis_root["uid"] = user_info->uid;
	redis_root["pwd"] = user_info->pwd;
	redis_root["name"] = user_info->name;
	redis_root["email"] = user_info->email;
	redis_root["nick"] = user_info->nick;
	redis_root["desc"] = user_info->desc;
	redis_root["sex"] = user_info->sex;

	RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());

	//返回数据
	rtvalue["uid"] = user_info->uid;
	rtvalue["pwd"] = user_info->pwd;
	rtvalue["name"] = user_info->name;
	rtvalue["email"] = user_info->email;
	rtvalue["nick"] = user_info->nick;
	rtvalue["desc"] = user_info->desc;
	rtvalue["sex"] = user_info->sex;

}

void LogicSystem::SetServer(std::shared_ptr<CServer> server) {
        _server = server;
}

void LogicSystem::GetUserThreadsHandler(std::shared_ptr<CSession> session,
	const short& msg_id, const std::string& msg_data)
{
	//从数据库获取chat_threads记录
	//Json::Reader reader;
	//Json::Value root;
	//reader.parse(msg_data, root);
	//auto uid = root["uid"].asInt();
	//int last_id = root["thread_id"].asInt();
	//std::cout << "get uid  threads  " << uid << std::endl;

	//Json::Value  rtvalue;
	//rtvalue["error"] = ErrorCodes::Success;
	//rtvalue["uid"] = uid;
	//Defer defer([this, &rtvalue, session]() {
	//	std::string return_str = rtvalue.toStyledString();
	//	session->Send(return_str, ID_LOAD_CHAT_THREAD_RSP);
	//	});

	//std::vector<std::shared_ptr<ChatThreadInfo>> threads;

	//int page_size = 10;
	//bool load_more = false;
	//int next_last_id = 0;
	//bool res = GetUserThreads(uid, last_id, page_size, threads, load_more, next_last_id);
	//if (!res) {
	//	rtvalue["error"] = ErrorCodes::UidInvalid;
	//	return;
	//}


	//rtvalue["load_more"] = load_more;
	//rtvalue["next_last_id"] = (int)next_last_id;
	////将threads数据写成json数组
	//for (auto& thread : threads) {
	//	Json::Value thread_value;
	//	thread_value["thread_id"] = int(thread->_thread_id);
	//	thread_value["type"] = thread->_type;
	//	thread_value["user1_id"] = thread->_user1_id;
	//	thread_value["user2_id"] = thread->_user2_id;
	//	rtvalue["threads"].append(thread_value);
	//}
	Json::Reader reader;
	Json::Value root;
	// @cuiruoni 先校验请求体；is_object 判断是防止包能解析成数组/标量时 operator[] 抛 LogicError
	if (!reader.parse(msg_data, root) || !root.isObject()) {
		Json::Value rtvalue;
		rtvalue["error"] = ErrorCodes::Error_Json;
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_LOAD_CHAT_THREAD_RSP);
		return;
	}

	int uid = 0;
	if (!TryGetInt(root, "uid", uid) || uid <= 0) {
		Json::Value rtvalue;
		rtvalue["error"] = ErrorCodes::UidInvalid;
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_LOAD_CHAT_THREAD_RSP);
		return;
	}

	int last_id = 0;
	TryGetInt(root, "thread_id", last_id);   // 分页游标可缺省，不判返回值
	std::cout << "get uid  threads  " << uid << std::endl;

	Json::Value  rtvalue;                       // 从这里往下就完全是你原来的代码了
	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["uid"] = uid;
	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_LOAD_CHAT_THREAD_RSP);
		});

	std::vector<std::shared_ptr<ChatThreadInfo>> threads;
	int page_size = 10;
	bool load_more = false;
	int next_last_id = 0;
	bool res = GetUserThreads(uid, last_id, page_size, threads, load_more, next_last_id);
	if (!res) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	rtvalue["load_more"] = load_more;
	rtvalue["next_last_id"] = (int)next_last_id;
	for (auto& thread : threads) {
		Json::Value thread_value;
		thread_value["thread_id"] = int(thread->_thread_id);
		thread_value["type"] = thread->_type;
		thread_value["user1_id"] = thread->_user1_id;
		thread_value["user2_id"] = thread->_user2_id;
		rtvalue["threads"].append(thread_value);
	}
}

bool LogicSystem::GetUserThreads(int64_t userId,
	int64_t lastId,
	int      pageSize,
	std::vector<std::shared_ptr<ChatThreadInfo>>& threads,
	bool& loadMore,
	int& nextLastId)
{
	return MysqlMgr::GetInstance()->GetUserThreads(userId, lastId, pageSize,
		threads, loadMore, nextLastId);
}

void LogicSystem::CreatePrivateChat(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data)
{
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);
	auto uid = root["uid"].asInt();
	auto other_id = root["other_id"].asInt();

	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["uid"] = uid;
	rtvalue["other_id"] = other_id;

	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_LOAD_CHAT_THREAD_RSP);
		});

	int thread_id = 0;
	bool res = MysqlMgr::GetInstance()->CreatePrivateChat(uid, other_id, thread_id);
	if (!res) {
		rtvalue["error"] = ErrorCodes::CREATE_CHAT_FAILED;
		return;
	}

	rtvalue["thread_id"] = thread_id;
}

