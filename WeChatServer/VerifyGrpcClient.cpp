#include "VerifyGrpcClient.h"
#include "ConfigMgr.h"
GetVarifyRsp VerifyGrpcClient::GetVerifycode(std::string email)
{
	ClientContext context;
	GetVarifyRsp reply;
	GetVarifyReq request;
	request.set_email(email);
	auto stub = _pool->getConnection();
	Status status = stub->GetVarifyCode(&context, request, &reply);
	if (status.ok()) {
		_pool->returnConnection(std::move(stub));
		return reply;
	}
	else {
		_pool->returnConnection(std::move(stub));
		reply.set_error(ErrorCodes::RPCFailed);
		return reply;
	}
}

VerifyGrpcClient::VerifyGrpcClient()
{
	auto& gCfgMgr = ConfigMgr::GetInstance();
	std::string host = gCfgMgr["Varifyserver"]["Host"];
	std::string port = gCfgMgr["Varifyserver"]["Port"];
	_pool.reset(new RPConPool(5, host, port));
}

RPConPool::RPConPool(std::size_t poolSize, std::string host, std::string port):
_poolSize(poolSize),_host(host),_port(port)
{
	for (std::size_t i = 0; i < poolSize; i++) {
		std::shared_ptr<Channel> channel = grpc::CreateChannel(host +":"+port,
			grpc::InsecureChannelCredentials());
		_connections.push(VarifyService::NewStub(channel));
	}

}

RPConPool::~RPConPool()
{
	std::lock_guard<std::mutex> lock(_mutex);
	Close();
	while (!_connections.empty()) {
		_connections.pop();
	}

}


void RPConPool::Close() {
	b_stop = true;
	_cond.notify_all();
}

std::unique_ptr<VarifyService::Stub> RPConPool::getConnection()
{
	std::unique_lock<std::mutex> lock(_mutex);
	_cond.wait(lock, [this]() {
		if (b_stop) {
			return true;
		}
		return !_connections.empty();
		});
		
	if (b_stop) {
		return nullptr;
	}

	auto context =std::move(_connections.front());
	_connections.pop();
	return context;
}

void RPConPool::returnConnection(std::unique_ptr<VarifyService::Stub> context)
{
	std::lock_guard<std::mutex>lock(_mutex);
	if (b_stop) {
		return;
	}
	
	_connections.push(std::move(context));
	_cond.notify_one();
}

