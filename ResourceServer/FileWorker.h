#pragma once
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <json/value.h>
#include <functional>
#include "const.h"
#include <functional>
#include <unordered_map>

class CSession;

// 文件传输任务：一次投递给 FileWorker 的分片上传/发送请求
struct FileTask
{
	FileTask(std::shared_ptr<CSession> session, MSG_IDS msg_id, int uid, std::string path, std::string name,
		int seq, int total_size, int trans_size, int last,
		std::string file_data,
		std::function<void(const Json::Value&)> callback, int chat_msg_id = 0,
		int sender = 0, int receiver = 0) :_session(session), _msg_id(msg_id), _uid(uid),
		_seq(seq), _path(path), _name(name), _total_size(total_size),
		_trans_size(trans_size), _last(last), _file_data(file_data), _callback(callback), _chat_msg_id(chat_msg_id),
		_sender(sender), _receiver(receiver)
	{
	}

	~FileTask() {}
	std::shared_ptr<CSession> _session;	// 发起该任务的会话，处理完后通过它回包
	MSG_IDS _msg_id;					// 消息id，FileWorker 据此在 _handlers 中选对应的处理函数
	int _uid;							// 用户id
	int _seq;							// 分片序号，用于按序写入和断点续传
	std::string _path;					// 文件存储路径
	std::string _name;					// 文件名
	int _total_size;					// 文件总大小
	int _trans_size;					// 已传输大小
	int _last;							// 是否为最后一个分片，1 表示传输结束
	std::string _file_data;				// 本分片的文件数据
	std::function<void(const Json::Value&)>  _callback;  //添加回调函数
	int _chat_msg_id;					// 关联的聊天消息id（聊天图片场景使用）
	int _sender;						// 发送者id（聊天图片场景使用）
	int _receiver;						// 接收者id（聊天图片场景使用）
	int _thread_id;						// 处理该任务的工作线程id

};

// 下载任务：一次投递给 DownloadWorker 的图片/文件下载请求，_seq 用于断点续传
struct DownloadTask {
	DownloadTask(std::shared_ptr<CSession> session, int uid, std::string name,
		int seq, std::string file_path,
		std::function<void(const Json::Value&)> callback) :_session(session), _uid(uid),
		_seq(seq), _name(name), _file_path(file_path), _callback(callback)
	{
	}
	~DownloadTask() {}
	std::shared_ptr<CSession> _session;
	int _uid;
	int _seq;
	std::string _name;
	std::string _file_path;
	std::function<void(const Json::Value&)>  _callback;  //添加回调函数
};

// 文件传输工作线程：单线程消费任务队列，按 _msg_id 分发到注册的 handler，
// 避免在多 IO 线程里直接做落盘等耗时操作，同时保证同一时刻只有一个文件任务在执行
class FileWorker
{
public:
	FileWorker();
	~FileWorker();
	void RegisterHandlers();							// 注册各消息id对应的处理函数
	void PostTask(std::shared_ptr<FileTask> task);		// 投递任务到队列并唤醒工作线程
private:
	void task_callback(std::shared_ptr<FileTask>);		// 工作线程主循环：取任务并执行
	std::unordered_map<MSG_IDS, std::function<void(std::shared_ptr<FileTask>)> > _handlers;	// 消息id -> 处理函数
	std::thread _work_thread;			// 工作线程
	std::queue<std::function<void()>> _task_que;	// 任务队列
	std::atomic<bool> _b_stop;			// 停止标志，析构时置位以退出线程
	std::mutex  _mtx;					// 保护任务队列
	std::condition_variable _cv;		// 队列非空时通知工作线程
};


// 下载工作线程：独立于 FileWorker，专门处理图片/文件下载，避免下载耗时阻塞上传任务
class DownloadWorker {
public:
	DownloadWorker();
	~DownloadWorker();
	void PostTask(std::shared_ptr<DownloadTask> task);	// 投递下载任务
private:
	void task_callback(std::shared_ptr<DownloadTask>);	// 工作线程主循环
	std::thread _work_thread;				// 工作线程
	std::queue<std::shared_ptr<DownloadTask>> _task_que;	// 下载任务队列
	std::atomic<bool> _b_stop;				// 停止标志
	std::mutex  _mtx;						// 保护任务队列
	std::condition_variable _cv;			// 条件变量
};
