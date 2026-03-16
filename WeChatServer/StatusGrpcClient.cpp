#include "StatusGrpcClient.h"

GetChatServerRsp StatusGrpcClient::GetChatServer(int uid)
{
	ClientContext context;
	GetChatServerReq request;
	GetChatServerRsp reply;
	request.set_uid(uid);
	auto stub = pool_->getConnection();
	Status status = stub->GetChatServer(&context, request, &reply);
	Defer defer([&stub, this]() {
		pool_->returnConnection(std::move(stub));
		});
	if (status.ok()) {
		return reply;
	}
	else {
		reply.set_error(ErrorCodes::RPCFailed);
		return reply;
	}

}

StatusGrpcClient::StatusGrpcClient()
{
	auto& gCFgMgr = ConfigMgr::GetInstance();
	std::string host = gCFgMgr["StatusServer"]["Host"];
	std::string port = gCFgMgr["StatusServer"]["Port"];
	pool_.reset(new StatusConPool(5, host, port));

}
