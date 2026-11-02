#include "LogicSystem.h"
#include <csignal>
#include <thread>
#include <mutex>
#include "AsioIOServicePool.h"
#include "CServer.h"
#include "ConfigMgr.h"
#include <boost/filesystem.hpp>

using namespace std;
bool bstop = false;
std::condition_variable cond_quit;
std::mutex mutex_quit;

int main()
{
	// TODO: 工程里没有 config.ini；运行前要补 [SelfServer]/[Mysql]/[Redis]/[chatserver1]/[chatserver2]/[Output]/[Static]
	// TODO: 没有注册 Redis count（InitCount/IncreaseCount），ResourceServer 目前不参与 StatusServer 的负载均衡
	auto& cfg = ConfigMgr::Inst();
	auto server_name = cfg["SelfServer"]["Name"];

	std::shared_ptr<AsioIOServicePool> pool = nullptr;
	try {
		pool = AsioIOServicePool::GetInstance();

		boost::asio::io_context  io_context;
		boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
		signals.async_wait([&io_context, pool](auto, auto) {
			io_context.stop();
			pool->Stop();
			});
		auto port_str = cfg["SelfServer"]["Port"];
		CServer s(io_context, atoi(port_str.c_str()));
		io_context.run();
	}
	catch (std::exception& e) {
		if (pool) {
			pool->Stop();
		}
		std::cerr << "Exception: " << e.what() << endl;
	}

}
