#include "StatusServiceImpl.h"
#include "ConfigMgr.h"
#include "const.h"
#include "RedisMgr.h"
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
std::string generate_unique_string() {
    // 创建UUID对象
    boost::uuids::uuid uuid = boost::uuids::random_generator()();

    // 将UUID转换为字符串
    std::string unique_string = to_string(uuid);

    return unique_string;
}

grpc::Status StatusServiceImpl::GetChatServer(grpc::ServerContext* context, const message::GetChatServerReq* request, message::GetChatServerRsp* reply)
{
    std::string prefix("cuiruonilan status server has received :  ");
    if (_servers.empty()) {
        reply->set_error(ErrorCodes::RPCGetFailed);
        return grpc::Status::OK;
    }

    size_t index = _server_index.fetch_add(1, std::memory_order_relaxed) % _servers.size();
    auto& server = _servers[index];
    reply->set_host(server.host);
    reply->set_port(server.port);
    reply->set_error(ErrorCodes::Success);
    const std::string token = generate_unique_string();
    reply->set_token(token);

    // Cache token for Login verification
    const int uid = request->uid();
    if (uid > 0) {
        const std::string key = std::string(USERTOKENPREFIX) + std::to_string(uid);
        const bool ok = RedisMgr::GetInstance()->Set(key, token);
        if (!ok) {
            reply->set_error(ErrorCodes::RPCFailed);
        }
    }
    return grpc::Status::OK;
}

grpc::Status StatusServiceImpl::Login(grpc::ServerContext* context, const message::LoginReq* request, message::LoginRsp* reply)
{
    const int uid = request->uid();
    const std::string token = request->token();
    reply->set_uid(uid);
    reply->set_token(token);

    std::cout << "Status Login uid=" << uid << " token=" << token << std::endl;

    if (uid <= 0 || token.empty()) {
        reply->set_error(ErrorCodes::RPCFailed);
        return grpc::Status::OK;
    }

    const std::string key = std::string(USERTOKENPREFIX) + std::to_string(uid);
    std::string redis_token;
    const bool got = RedisMgr::GetInstance()->Get(key, redis_token);
    if (!got) {
        std::cout << "Status Login token not found in redis, key=" << key << std::endl;
        reply->set_error(ErrorCodes::RPCFailed);
        return grpc::Status::OK;
    }

    if (redis_token != token) {
        std::cout << "Status Login token mismatch, redis=" << redis_token << std::endl;
        reply->set_error(ErrorCodes::RPCFailed);
        return grpc::Status::OK;
    }

    reply->set_error(ErrorCodes::Success);
    return grpc::Status::OK;
}

StatusServiceImpl::StatusServiceImpl() : _server_index(0)
{
    auto& cfg = ConfigMgr::GetInstance();
    ChatServer server;
    server.port = cfg["ChatServer1"]["Port"];
    server.host = cfg["ChatServer1"]["Host"];
    _servers.push_back(server);
}
