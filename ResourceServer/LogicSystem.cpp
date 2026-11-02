#include "LogicSystem.h"
#include "const.h"
#include "base64.h"
#include <fstream>
#include <boost/filesystem.hpp>
#include "ConfigMgr.h"
#include "FileSystem.h"

using namespace std;

// TODO: LogicWorker 已启动线程，但 RegisterCallBacks 只注册了 2 个 handler；补齐后消息才会被完整处理
LogicSystem::LogicSystem(){
	for (int i = 0; i < LOGIC_WORKER_COUNT; i++) {
		_workers.push_back(std::make_shared<LogicWorker>());
	}
}

LogicSystem::~LogicSystem(){

}

void LogicSystem::PostMsgToQue(shared_ptr < LogicNode> msg, int index) {
	_workers[index]->PostTask(msg);
}


// TODO: MD5 秒传/去重未接入：AddMD5File/GetFileInfo 没有调用点，FileInfo 也没有 md5 字段
void LogicSystem::AddMD5File(std::string md5, std::shared_ptr<FileInfo> fileinfo) {
	std::lock_guard<std::mutex> lock(_file_mtx);
	_map_md5_files[md5] = fileinfo;
}

std::shared_ptr<FileInfo> LogicSystem::GetFileInfo(std::string md5) {
	std::lock_guard<std::mutex> lock(_file_mtx);
	auto iter = _map_md5_files.find(md5);
	if (iter == _map_md5_files.end()) {
		return nullptr;
	}

	return iter->second;
}






