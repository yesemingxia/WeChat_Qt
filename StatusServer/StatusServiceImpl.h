#pragma once
#ifdef Status
#undef Status
#endif
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include <vector>
#include <string>
#include <atomic>
struct ChatServer {
    std::string host;
    std::string port;
};
class StatusServiceImpl final : public message::StatusService::Service
{
public:
    StatusServiceImpl();
    grpc::Status GetChatServer(grpc::ServerContext* context, const message::GetChatServerReq* request,
        message::GetChatServerRsp* reply) override;
    grpc::Status Login(grpc::ServerContext* context, const message::LoginReq* request,
        message::LoginRsp* reply) override;

    std::vector<ChatServer> _servers;
    std::atomic<size_t> _server_index;
};
