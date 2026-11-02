#pragma once
#include "Singleton.h"
#include <memory>
#include "FileWorker.h"
#include <vector>


// TODO: FileSystem 还没实现：构造函数要创建 FILE_WORKER_COUNT 个 FileWorker + DOWN_LOAD_WORKER_COUNT 个 DownloadWorker，
//       并提供按文件名/uid 哈希的 PostTask；目前没有 GetInstance 调用，两个 worker 都不会启动
class FileSystem :public Singleton<FileSystem>
{
	friend class Singleton<FileSystem>;
public:
	~FileSystem();
	void PostMsgToQue(shared_ptr <FileTask> msg, int index);
	void PostDownloadTaskToQue(std::shared_ptr<DownloadTask> msg, int index);
private:
	FileSystem();
	std::vector<std::shared_ptr<FileWorker>>  _file_workers;
	std::vector<std::shared_ptr<DownloadWorker>> _down_load_worker;
};

