#include "StatusServiceImpl.h"
#include "ConfigMgr.h"
#include "const.h"
#include "RedisMgr.h"
#include <climits>
#include <boost/uuid.hpp>

std::string generate_unique_string() {
	// 创建UUID对象
	boost::uuids::uuid uuid = boost::uuids::random_generator()();

	// 将UUID转换为字符串
	std::string unique_string = to_string(uuid);

	return unique_string;
}

Status StatusServiceImpl::GetChatServer(ServerContext* context, const GetChatServerReq* request, GetChatServerRsp* reply)
{
	//注意：_server_mtx 由 getChatServer() 内部加锁保护 _servers，
	//这里不能重复加锁，否则同一个非递归 mutex 会被锁两次，导致死锁
	std::string prefix("cuiruonilan status server has received :  ");
	const auto& server = getChatServer();
	if (server.host.empty() || server.port.empty()) {
		reply->set_error(ErrorCodes::RPCFailed);
		return Status::OK;
	}
	reply->set_host(server.host);
	reply->set_port(server.port);
	reply->set_error(ErrorCodes::Success);
	reply->set_token(generate_unique_string());
	insertToken(request->uid(), reply->token());
	return Status::OK;
	
}

StatusServiceImpl::StatusServiceImpl()
{
	auto& cfg = ConfigMgr::GetInstance();
	auto server_list = cfg["chatservers"]["Name"];

	std::vector<std::string> words;

	std::stringstream ss(server_list);
	std::string word;

	while (std::getline(ss, word, ',')) {
		words.push_back(word);
	}

	for (auto& word : words) {
		if (cfg[word]["Name"].empty()) {
			continue;
		}

		ChatServer server;
		server.port = cfg[word]["Port"];
		server.host = cfg[word]["Host"];
		server.name = cfg[word]["Name"];
		_servers[server.name] = server;
	}

}

ChatServer StatusServiceImpl::getChatServer() {
	std::lock_guard<std::mutex> guard(_server_mtx);
	if (_servers.empty()) {
		return ChatServer();
	}
	auto minServer = _servers.begin()->second;

	//先取第一个服务器（minServer）的在线数，
	//否则它的 con_count 一直是构造函数里的 0，比较时永远换不掉它，负载均衡失效
	auto min_count_str = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, minServer.name);
	if (min_count_str.empty()) {
		//不存在则默认设置为最大
		minServer.con_count = INT_MAX;
	}
	else {
		minServer.con_count = std::stoi(min_count_str);
	}


	// 使用范围基于for循环
	for (auto& server : _servers) {

		if (server.second.name == minServer.name) {
			continue;
		}

		auto count_str = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, server.second.name);
		if (count_str.empty()) {
			server.second.con_count = INT_MAX;
		}
		else {
			server.second.con_count = std::stoi(count_str);
		}

		if (server.second.con_count < minServer.con_count) {
			minServer = server.second;
		}
	}

	return minServer;
}

Status StatusServiceImpl::Login(ServerContext* context, const LoginReq* request, LoginRsp* reply)
{
	auto uid = request->uid();
	auto token = request->token();

	std::string uid_str = std::to_string(uid);
	std::string token_key = USERTOKENPREFIX + uid_str;
	std::string token_value = "";
	bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
	if (!success) {
		reply->set_error(ErrorCodes::UidInvalid);
		return Status::OK;
	}
	
	if (token_value != token) {
		reply->set_error(ErrorCodes::TokenInvalid);
		return Status::OK;
	}
	reply->set_error(ErrorCodes::Success);
	reply->set_uid(uid);
	reply->set_token(token);
	return Status::OK;
}

void StatusServiceImpl::insertToken(int uid, std::string token)
{
	std::string uid_str = std::to_string(uid);
	std::string token_key = USERTOKENPREFIX + uid_str;
	RedisMgr::GetInstance()->Set(token_key, token);
}
