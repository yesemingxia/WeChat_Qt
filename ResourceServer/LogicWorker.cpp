#include "LogicWorker.h"
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include "FileSystem.h"
#include "CSession.h"
#include "LogicSystem.h"
#include "ConfigMgr.h"
#include "RedisMgr.h"
#include "MysqlMgr.h"


namespace {

	int64_t JsonInt64(const Json::Value& root, const char* key, int64_t def = 0) {
		const Json::Value& v = root[key];
		try {
			if (v.isString()) return std::stoll(v.asString());
			if (v.isInt())    return v.asInt();
			if (v.isUInt())   return static_cast<int64_t>(v.asUInt());
			if (v.isDouble()) return static_cast<int64_t>(v.asDouble());
		}
		catch (const std::exception&) {
			// 非数字字符串按缺省值处理，不把异常抛到工作线程
		}
		return def;
	}

	bool CheckUserToken(int uid, const std::string& token) {
		if (uid <= 0 || token.empty()) {
			return false;
		}
		std::string key = USERTOKENPREFIX + std::to_string(uid);
		std::string value;
		if (!RedisMgr::GetInstance()->Get(key, value)) {
			return false;
		}
		return value == token;
	}

	bool SafeFileName(const Json::Value& root, std::string& name) {
		name = boost::filesystem::path(root["name"].asString()).filename().string();
		if (name.empty() || name == "." || name == "..") {
			return false;
		}
		static const std::string allowed =
			"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-";
		return name.find_first_not_of(allowed) == std::string::npos;
	}

} // namespace


// TODO: 还缺 1031/1033/1005/1037/1041/1043/1045/1047 的 handler；
//       当前 RegisterCallBacks 只注册了 ID_TEST_MSG_REQ 和 ID_UPLOAD_FILE_REQ
LogicWorker::LogicWorker()
{
	RegisterCallBacks();
	/*_work_thread = std::thread([this]() {
		while (!_b_stop) {
			std::unique_lock<std::mutex> lock(_mtx);
			_cv.wait(lock,[this]() {
				if (_b_stop) {
					return true;
				}

				if (_task_que.empty()) {
					return false;
				}

				return true;

				});

			if (_b_stop) {
				return;
			}

			auto task = _task_que.front();
			task_callback(task);
			_task_que.pop();

		}
		});*/
		
	_work_thread = std::thread([this]() {
		while (true) {
			std::shared_ptr<LogicNode> task;
			{
				std::unique_lock<std::mutex> lock(_mtx);
				_cv.wait(lock, [this] { return _b_stop || !_task_que.empty(); });
				if (_b_stop) {
					return;
				}
				task = std::move(_task_que.front());
				_task_que.pop();          // 先出队：任务抛异常也不会卡在队首
			}
			try {
				task_callback(task);
			}
			catch (const std::exception& e) {
				std::cerr << "logic task failed, msg_id=" << task->_recvnode->_msg_id
					<< ", err=" << e.what() << std::endl;
			}
			catch (...) {
				std::cerr << "logic task failed: unknown exception" << std::endl;
			}
		}
		});


}

LogicWorker::~LogicWorker()
{
	_b_stop = true;
	_cv.notify_one();
	_work_thread.join();
}

void LogicWorker::PostTask(std::shared_ptr<LogicNode> task) {
	std::lock_guard<std::mutex> lock(_mtx);
	_task_que.push(task);
	_cv.notify_one();
}

void LogicWorker::RegisterCallBacks() {
	// 测试消息：把客户端发来的 data 原样回显，用于验证 TCP/JSON 收发链路
	_fun_callbacks[ID_TEST_MSG_REQ] = [this](shared_ptr<CSession> session, const short& msg_id,
		const string& msg_data) {
			Json::Reader reader;
			Json::Value root;
			reader.parse(msg_data, root);
			auto data = root["data"].asString();
			std::cout << "recv test data is  " << data << std::endl;

			Json::Value rtvalue;
			Defer defer([this,&rtvalue,session]() {
				std::string return_str = rtvalue.toStyledString();
				session->Send(return_str, ID_TEST_MSG_RSP);
				});
			rtvalue["error"] = ErrorCodes::Success;
			rtvalue["data"] = data;

		};

	
	// 普通文件上传：用 md5 在 Redis 维护分片进度，再把分片投给 FileWorker 落盘，写完回调回包
	_fun_callbacks[ID_UPLOAD_FILE_REQ] = [this](shared_ptr<CSession> session, const short& msg_id,
		const string& msg_data) {
			Json::Reader reader;
			Json::Value root;
			reader.parse(msg_data, root);
			auto md5 = root["md5"].asString();
			auto seq = root["seq"].asInt();
			auto name = root["name"].asString();
			auto total_size = root["total_size"].asInt();
			auto trans_size = root["trans_size"].asInt();
			auto last = root["last"].asInt();
			auto file_data = root["data"].asString();
			auto file_path = ConfigMgr::Inst().GetFileOutPath();
			auto uid = root["uid"].asInt();

			//转化为字符串
			auto uid_str = std::to_string(uid);
			auto file_path_str = (file_path / uid_str / name).string();
			Json::Value rtvalue;

			/*	Defer defer([this, &rtvalue, session]() {
					std::string return_str = rtvalue.toStyledString();
					session->Send(return_str, ID_TEST_MSG_RSP);
					});*/

			auto callback = [=](const Json::Value& result) {

				//在异步任务完成后调用
				Json::Value res = result;
				res["error"] = ErrorCodes::Success;
				res["total_size"] = total_size;
				res["seq"] = seq;
				res["name"] = name;
				res["trans_size"] = trans_size;
				res["last"] = last;
				res["md5"] = md5;
				res["uid"] = uid;

				std::string return_str = res.toStyledString();
				session->Send(return_str, ID_UPLOAD_FILE_RSP);
				};
			//使用哈希 对字符串哈希
			std::hash<std::string> hash_fn;
			std::size_t hash_value = hash_fn(name);

			int index = hash_value % FILE_WORKER_COUNT;
			std::cout << "Hash value: " << hash_value << std::endl;

			if (seq == 1) {
				//构造数据存储
				auto file_info = std::make_shared<FileInfo>();
				file_info->_file_path_str = file_path_str;
				file_info->_name = name;
				file_info->_seq = seq;
				file_info->_total_size = total_size;
				file_info->_trans_size = trans_size;
				//todo... 后期改为redis,以及mysql 持久化存储
				bool success = RedisMgr::GetInstance()->SetFileInfo(md5, file_info);
				if (!success) {
					rtvalue["error"] = ErrorCodes::FileSaveRedisFailed;
					std::string return_str = rtvalue.toStyledString();
					session->Send(return_str, ID_UPLOAD_HEAD_ICON_RSP);
					return;
				}
			}
			else {
				auto file_info = RedisMgr::GetInstance()->GetFileInfo(md5);
				if (file_info == nullptr) {
					rtvalue["error"] = ErrorCodes::FileNotExists;
					std::string return_str = rtvalue.toStyledString();
					session->Send(return_str, ID_UPLOAD_FILE_RSP);
					return;
				}
				file_info->_seq = seq;
				file_info->_trans_size = trans_size;
				bool success = RedisMgr::GetInstance()->SetFileInfo(md5, file_info);
				if (!success) {
					rtvalue["error"] = ErrorCodes::FileSaveRedisFailed;
					std::string return_str = rtvalue.toStyledString();
					session->Send(return_str, ID_UPLOAD_FILE_RSP);
					return;
				}
			}

			FileSystem::GetInstance()->PostMsgToQue(
				std::make_shared<FileTask>(session, ID_UPLOAD_FILE_REQ, uid, file_path_str, name, seq, total_size,
					trans_size, last, file_data, callback),
				index
			);

		};

	// 上传进度同步：按 md5 查 Redis 中的 FileInfo，返回 total_size/seq/trans_size 供客户端断点续传
	/*_fun_callbacks[ID_SYNC_FILE_REQ] = [this](shared_ptr<CSession> session, const short& msg_id,
		const string& msg_data) {
			Json::Reader reader;
			Json::Value root;
			reader.parse(msg_data, root);

			Json::Value rtvalue;
			Defer defer([this,session,&rtvalue]() {
				std::string return_str = rtvalue.toStyledString();
				session->Send(return_str, ID_SYNC_FILE_REQ);
				});

			auto md5 = root["md5"].asString();

			auto file = LogicSystem::GetInstance()->GetFileInfo(md5);

			if (file == nullptr) {
				rtvalue["error"] = ErrorCodes::FileNotExists;
				return;
			}


			rtvalue["error"] = ErrorCodes::Success;
			rtvalue["total_size"] = std::to_string(file->_total_size);
			rtvalue["seq"] = file->_seq;
			rtvalue["name"] = file->_name;
			rtvalue["trans_size"] = std::to_string(file->_trans_size);
			rtvalue["md5"] = md5;
		};*/

	_fun_callbacks[ID_SYNC_FILE_REQ] = [this](shared_ptr<CSession> session, const short&,
		const string& msg_data) {
			Json::Reader reader;
			Json::Value root;
			reader.parse(msg_data, root);

			auto name = root["name"].asString();
			auto uid = root["uid"].asInt();
			auto token = root["token"].asString();

			Json::Value rtvalue;
			if (!CheckUserToken(uid, token)) {
				rtvalue["error"] = ErrorCodes::TokenInvalid;
				rtvalue["name"] = name;
			}
			else {
				auto file = RedisMgr::GetInstance()->GetFileInfo(name);
				if (file == nullptr) {
					rtvalue["error"] = ErrorCodes::FileNotExists;
					rtvalue["name"] = name;
				}
				else {
					rtvalue["error"] = ErrorCodes::Success;
					rtvalue["name"] = name;
					rtvalue["seq"] = file->_seq;
					rtvalue["trans_size"] = std::to_string(file->_trans_size);
					rtvalue["total_size"] = std::to_string(file->_total_size);
					rtvalue["md5"] = root["md5"].asString();
				}
			}
			session->Send(rtvalue.toStyledString(), ID_SYNC_FILE_RSP);
		};


	// 头像上传：首片校验 token，分片落盘完成后更新用户头像字段并刷新 Redis 用户信息缓存
	_fun_callbacks[ID_UPLOAD_HEAD_ICON_REQ] = [this](shared_ptr<CSession> session, const short& msg_id,
		const string& msg_data) {
			Json::Reader reader;
			Json::Value root;
			reader.parse(msg_data, root);

			auto md5 = root["md5"].asString();
			auto seq = root["seq"].asInt();
			auto name = root["name"].asString();
			auto total_size = root["total_size"].asInt();
			auto trans_size = root["trans_size"].asInt();
			auto last = root["last"].asInt();
			auto file_data = root["data"].asString();
			auto uid = root["uid"].asInt();
			auto token = root["token"].asString();
			auto last_seq = root["last_seq"].asInt();
			//转化为字符串
			auto uid_str = std::to_string(uid);

			auto file_path = ConfigMgr::Inst().GetFileOutPath();
			auto file_path_str = (file_path / uid_str / name).string();

			Json::Value rtvalue;
			
			auto callback = [=](const Json::Value& result) {

				// 在异步任务完成后调用
				Json::Value rtvalue = result;
				rtvalue["total_size"] = total_size;
				rtvalue["seq"] = seq;
				rtvalue["name"] = name;
				rtvalue["trans_size"] = trans_size;
				rtvalue["last"] = last;
				rtvalue["md5"] = md5;
				rtvalue["uid"] = uid;
				rtvalue["last_seq"] = last_seq;
				std::string return_str = rtvalue.toStyledString();
				session->Send(return_str, ID_UPLOAD_HEAD_ICON_RSP);
				};

			//第一个包校验一下token是否合理
			if (seq == 1) {
				//从redis获取用户token是否正确
				std::string uid_str = std::to_string(uid);
				std::string token_key = USERTOKENPREFIX + uid_str;
				std::string token_value = "";
				bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
				if (!success) {
					rtvalue["error"] = ErrorCodes::UidInvalid;
					std::string return_str = rtvalue.toStyledString();
					session->Send(return_str, ID_UPLOAD_HEAD_ICON_RSP);
					return;
				}

				if (token_value != token) {
					rtvalue["error"] = ErrorCodes::TokenInvalid;
					std::string return_str = rtvalue.toStyledString();
					session->Send(return_str, ID_UPLOAD_HEAD_ICON_RSP);
					return;
				}
			}

			// 使用 std::hash 对字符串进行哈希
			std::hash<std::string> hash_fn;
			size_t hash_value = hash_fn(name); // 生成哈希值
			int index = hash_value % FILE_WORKER_COUNT;
			std::cout << "Hash value: " << hash_value << std::endl;

			//第一个包
			if (seq == 1) {
				//构造数据存储
				auto file_info = std::make_shared<FileInfo>();
				file_info->_file_path_str = file_path_str;
				file_info->_name = name;
				file_info->_seq = seq;
				file_info->_total_size = total_size;
				file_info->_trans_size = trans_size;
				//LogicSystem::GetInstance()->AddMD5File(md5, file_info);
				//改为用redis存储
				bool success = RedisMgr::GetInstance()->SetFileInfo(name, file_info);
				if (!success) {
					rtvalue["error"] = ErrorCodes::FileSaveRedisFailed;
					std::string return_str = rtvalue.toStyledString();
					session->Send(return_str, ID_UPLOAD_HEAD_ICON_RSP);
					return;
				}
			}
			else {
				//auto file_info = LogicSystem::GetInstance()->GetFileInfo(md5);
				//改为从redis中加载
				auto file_info = RedisMgr::GetInstance()->GetFileInfo(name);
				if (file_info == nullptr) {
					rtvalue["error"] = ErrorCodes::FileNotExists;
					std::string return_str = rtvalue.toStyledString();
					session->Send(return_str, ID_UPLOAD_HEAD_ICON_RSP);
					return;
				}
				file_info->_seq = seq;
				file_info->_trans_size = trans_size;
				bool success = RedisMgr::GetInstance()->SetFileInfo(name, file_info);
				if (!success) {
					rtvalue["error"] = ErrorCodes::FileSaveRedisFailed;
					std::string return_str = rtvalue.toStyledString();
					session->Send(return_str, ID_UPLOAD_HEAD_ICON_RSP);
					return;
				}
			}


			FileSystem::GetInstance()->PostMsgToQue(
				std::make_shared<FileTask>(session, ID_UPLOAD_HEAD_ICON_REQ, uid, file_path_str, name, seq, total_size,
					trans_size, last, file_data, callback),
				index
			);

		};

		// 文件下载：首片校验 token，按文件名哈希选 DownloadWorker，逐片读取文件并 base64 回包
		_fun_callbacks[ID_DOWN_LOAD_FILE_REQ] = [this](shared_ptr<CSession> session, const short& msg_id,
			const string& msg_data) {
				Json::Reader reader;
				Json::Value  root;
				reader.parse(msg_data, root);

				auto seq = root["seq"].asInt();
				auto name = root["name"].asString();
				auto uid = root["uid"].asInt();
				auto token = root["token"].asString();
				auto client_path = root["client_path"].asString();
				auto req_type = root["req_type"].asString();
				//转化为字符串
				auto uid_str = std::to_string(uid);

				auto file_path = ConfigMgr::Inst().GetFileOutPath();
				auto file_path_str = (file_path / uid_str / name).string();
				Json::Value  rtvalue;
				auto callback = [=](const Json::Value& result) {

					// 在异步任务完成后调用
					Json::Value rtvalue = result;
					rtvalue["client_path"] = client_path;
					rtvalue["name"] = name;
					rtvalue["req_type"] = req_type;
					std::string return_str = rtvalue.toStyledString();
					session->Send(return_str, ID_DOWN_LOAD_FILE_RSP);
					};

				//第一个包校验一下token是否合理
				if (seq == 1) {
					//从redis获取用户token是否正确
					std::string uid_str = std::to_string(uid);
					std::string token_key = USERTOKENPREFIX + uid_str;
					std::string token_value = "";
					bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
					if (!success) {
						rtvalue["error"] = ErrorCodes::UidInvalid;
						std::string return_str = rtvalue.toStyledString();
						session->Send(return_str, ID_DOWN_LOAD_FILE_RSP);
						return;
					}

					if (token_value != token) {
						rtvalue["error"] = ErrorCodes::TokenInvalid;
						std::string return_str = rtvalue.toStyledString();
						session->Send(return_str, ID_DOWN_LOAD_FILE_RSP);
						return;
					}
				}

				// 使用 std::hash 对字符串进行哈希
				std::hash<std::string> hash_fn;
				size_t hash_value = hash_fn(name); // 生成哈希值
				int index = hash_value % FILE_WORKER_COUNT;
				std::cout << "Hash value: " << hash_value << std::endl;

				FileSystem::GetInstance()->PostDownloadTaskToQue(
					std::make_shared<DownloadTask>(session, uid, name, seq, file_path_str, callback),
					index
				);


			};

		_fun_callbacks[ID_IMG_CHAT_UPLOAD_REQ] = [this](shared_ptr<CSession> session, const short& msg_id,
			const string& msg_data) {
				Json::Reader reader;
				Json::Value root;
				reader.parse(msg_data, root);
				auto md5 = root["md5"].asString();
				auto seq = root["seq"].asInt();
				auto name = root["name"].asString();
				auto total_size_str = root["total_size"].asString();
				auto trans_size_str = root["trans_size"].asString();
				/*int64_t total_size = std::stoll(total_size_str);
				int64_t trans_size = std::stoll(trans_size_str);*/
				int64_t total_size = JsonInt64(root, "total_size");
				int64_t trans_size = JsonInt64(root, "trans_size");
				auto last = root["last"].asInt();
				auto file_data = root["data"].asString();
				auto file_path = ConfigMgr::Inst().GetFileOutPath();
				auto uid = root["uid"].asInt();
				auto sender = root["sender"].asInt();
				auto receiver = root["receiver"].asInt();
				auto message_id = root["message_id"].asInt();
				//转化为字符串
				auto uid_str = std::to_string(uid);
				auto file_path_str = (file_path / uid_str / name).string();
				Json::Value  rtvalue;

				auto callback = [=](const Json::Value& result) {

					// 在异步任务完成后调用
					Json::Value rtvalue = result;
					rtvalue["error"] = ErrorCodes::Success;
					rtvalue["total_size"] = std::to_string(total_size);
					rtvalue["seq"] = seq;
					rtvalue["name"] = name;
					rtvalue["trans_size"] = std::to_string(trans_size);
					rtvalue["last"] = last;
					rtvalue["md5"] = md5;
					rtvalue["uid"] = uid;
					rtvalue["sender"] = sender;
					rtvalue["receiver"] = receiver;
					std::string return_str = rtvalue.toStyledString();
					session->Send(return_str, ID_IMG_CHAT_UPLOAD_RSP);
					};

				// 使用 std::hash 对字符串进行哈希
				std::hash<std::string> hash_fn;
				size_t hash_value = hash_fn(name); // 生成哈希值
				int index = hash_value % FILE_WORKER_COUNT;
				std::cout << "Hash value: " << hash_value << std::endl;

				//第一个包
				if (seq == 1) {
					//构造数据存储
					auto file_info = std::make_shared<FileInfo>();
					file_info->_file_path_str = file_path_str;
					file_info->_name = name;
					file_info->_seq = seq;
					file_info->_total_size = total_size;
					file_info->_trans_size = trans_size;
					bool success = RedisMgr::GetInstance()->SetFileInfo(name, file_info);
					if (!success) {
						rtvalue["error"] = ErrorCodes::FileSaveRedisFailed;
						std::string return_str = rtvalue.toStyledString();
						session->Send(return_str, ID_IMG_CHAT_UPLOAD_RSP);
						return;
					}
				}
				else {
					auto file_info = RedisMgr::GetInstance()->GetFileInfo(name);
					if (file_info == nullptr) {
						rtvalue["error"] = ErrorCodes::FileNotExists;
						std::string return_str = rtvalue.toStyledString();
						session->Send(return_str, ID_IMG_CHAT_UPLOAD_RSP);
						return;
					}
					file_info->_seq++;
					file_info->_trans_size = trans_size;
					bool success = RedisMgr::GetInstance()->SetFileInfo(name, file_info);
					if (!success) {
						rtvalue["error"] = ErrorCodes::FileSaveRedisFailed;
						std::string return_str = rtvalue.toStyledString();
						session->Send(return_str, ID_IMG_CHAT_UPLOAD_RSP);
						return;
					}
				}


				FileSystem::GetInstance()->PostMsgToQue(
					std::make_shared<FileTask>(session, ID_IMG_CHAT_UPLOAD_REQ, uid, file_path_str, name, seq, total_size,
						trans_size, last, file_data, callback, message_id, sender, receiver),
					index
				);
			};

		// 文件信息同步：与聊天图片上传走相同的落盘+状态更新+通知流程（客户端重连后补齐分片）
		_fun_callbacks[ID_FILE_INFO_SYNC_REQ] = [this](shared_ptr<CSession> session, const short& msg_id,
			const string& msg_data) {
				Json::Reader reader;
				Json::Value root;
				reader.parse(msg_data, root);
				auto md5 = root["md5"].asString();
				auto seq = root["seq"].asInt();
				auto name = root["name"].asString();
				/*auto total_size_str = root["total_size"].asString();
				auto trans_size_str = root["trans_size"].asString();
				auto total_size = std::stoll(total_size_str);
				auto trans_size = std::stoll(trans_size_str);*/
				// 客户端续传只发 name/uid/token；stoll("") 会抛异常带走进程，先按“无进度”回包
				int64_t total_size = 0;
				int64_t trans_size = 0;
				try {
					total_size = std::stoll(root["total_size"].asString());
					trans_size = std::stoll(root["trans_size"].asString());
				}
				catch (const std::exception&) {
					Json::Value rt;
					rt["error"] = ErrorCodes::FileNotExists;   // 1012：客户端会从第 1 块重来
					rt["name"] = name; 
					session->Send(rt.toStyledString(), ID_FILE_INFO_SYNC_RSP);
					return;
				}
				auto last = root["last"].asInt();
				auto file_data = root["data"].asString();
				auto file_path = ConfigMgr::Inst().GetFileOutPath();
				auto uid = root["uid"].asInt();
				auto message_id = root["message_id"].asInt();
				auto sender = root["sender"].asInt();
				auto receiver = root["receiver"].asInt();
				//转化为字符串
				auto uid_str = std::to_string(uid);
				auto file_path_str = (file_path / uid_str / name).string();
				Json::Value  rtvalue;

				auto callback = [=](const Json::Value& result) {

					// 在异步任务完成后调用
					Json::Value rtvalue = result;
					rtvalue["error"] = ErrorCodes::Success;
					rtvalue["seq"] = seq;
					rtvalue["name"] = name;
					rtvalue["last"] = last;
					rtvalue["md5"] = md5;
					rtvalue["uid"] = uid;
					rtvalue["sender"] = sender;
					rtvalue["receiver"] = receiver;
					std::string return_str = rtvalue.toStyledString();
					session->Send(return_str, ID_FILE_INFO_SYNC_RSP);
					};

				// 使用 std::hash 对字符串进行哈希
				std::hash<std::string> hash_fn;
				size_t hash_value = hash_fn(name); // 生成哈希值
				int index = hash_value % FILE_WORKER_COUNT;
				std::cout << "Hash value: " << hash_value << std::endl;

				//第一个包
				if (seq == 1) {
					//构造数据存储
					auto file_info = std::make_shared<FileInfo>();
					file_info->_file_path_str = file_path_str;
					file_info->_name = name;
					file_info->_seq = seq;
					file_info->_total_size = total_size;
					file_info->_trans_size = trans_size;
					bool success = RedisMgr::GetInstance()->SetFileInfo(name, file_info);
					if (!success) {
						rtvalue["error"] = ErrorCodes::FileSaveRedisFailed;
						std::string return_str = rtvalue.toStyledString();
						session->Send(return_str, ID_FILE_INFO_SYNC_RSP);
						return;
					}
				}
				else {
					auto file_info = RedisMgr::GetInstance()->GetFileInfo(name);
					if (file_info == nullptr) {
						rtvalue["error"] = ErrorCodes::FileNotExists;
						std::string return_str = rtvalue.toStyledString();
						session->Send(return_str, ID_FILE_INFO_SYNC_RSP);
						return;
					}
					file_info->_seq = seq;
					file_info->_trans_size = trans_size;
					bool success = RedisMgr::GetInstance()->SetFileInfo(name, file_info);
					if (!success) {
						rtvalue["error"] = ErrorCodes::FileSaveRedisFailed;
						std::string return_str = rtvalue.toStyledString();
						session->Send(return_str, ID_FILE_INFO_SYNC_RSP);
						return;
					}
				}


				FileSystem::GetInstance()->PostMsgToQue(
					std::make_shared<FileTask>(session, ID_FILE_INFO_SYNC_REQ, uid, file_path_str, name, seq, total_size,
						trans_size, last, file_data, callback, message_id, sender, receiver),
					index
				);
			};

		// 聊天图片续传：从 Redis 恢复上次进度继续落盘，完成后同样更新状态并通知接收方
		_fun_callbacks[ID_IMG_CHAT_CONTINUE_UPLOAD_REQ] = [this](shared_ptr<CSession> session, const short& msg_id,
			const string& msg_data) {
				Json::Reader reader;
				Json::Value root;
				reader.parse(msg_data, root);
				auto md5 = root["md5"].asString();
				auto seq = root["seq"].asInt();
				auto name = root["name"].asString();
				auto total_size = root["total_size"].asInt();
				auto trans_size = root["trans_size"].asInt();
				auto last = root["last"].asInt();
				auto file_data = root["data"].asString();
				auto file_path = ConfigMgr::Inst().GetFileOutPath();
				auto uid = root["uid"].asInt();
				auto message_id = root["message_id"].asInt();
				auto sender = root["sender"].asInt();
				auto receiver = root["receiver"].asInt();
				//转化为字符串
				auto uid_str = std::to_string(uid);
				auto file_path_str = (file_path / uid_str / name).string();
				Json::Value  rtvalue;

				auto callback = [=](const Json::Value& result) {

					// 在异步任务完成后调用
					Json::Value rtvalue = result;
					rtvalue["error"] = ErrorCodes::Success;
					rtvalue["total_size"] = total_size;
					rtvalue["seq"] = seq;
					rtvalue["name"] = name;
					rtvalue["trans_size"] = trans_size;
					rtvalue["last"] = last;
					rtvalue["md5"] = md5;
					rtvalue["uid"] = uid;
					rtvalue["sender"] = sender;
					rtvalue["receiver"] = receiver;
					std::string return_str = rtvalue.toStyledString();
					session->Send(return_str, ID_IMG_CHAT_CONTINUE_UPLOAD_RSP);
					};

				// 使用 std::hash 对字符串进行哈希
				std::hash<std::string> hash_fn;
				size_t hash_value = hash_fn(name); // 生成哈希值
				int index = hash_value % FILE_WORKER_COUNT;
				std::cout << "Hash value: " << hash_value << std::endl;

				//第一个包
				if (seq == 1) {
					//构造数据存储
					auto file_info = std::make_shared<FileInfo>();
					file_info->_file_path_str = file_path_str;
					file_info->_name = name;
					file_info->_seq = seq;
					file_info->_total_size = total_size;
					file_info->_trans_size = trans_size;
					bool success = RedisMgr::GetInstance()->SetFileInfo(name, file_info);
					if (!success) {
						rtvalue["error"] = ErrorCodes::FileSaveRedisFailed;
						std::string return_str = rtvalue.toStyledString();
						session->Send(return_str, ID_IMG_CHAT_CONTINUE_UPLOAD_RSP);
						return;
					}
				}
				else {
					auto file_info = RedisMgr::GetInstance()->GetFileInfo(name);
					if (file_info == nullptr) {
						rtvalue["error"] = ErrorCodes::FileNotExists;
						std::string return_str = rtvalue.toStyledString();
						session->Send(return_str, ID_IMG_CHAT_CONTINUE_UPLOAD_RSP);
						return;
					}
					file_info->_seq = seq;
					file_info->_trans_size = trans_size;
					bool success = RedisMgr::GetInstance()->SetFileInfo(name, file_info);
					if (!success) {
						rtvalue["error"] = ErrorCodes::FileSaveRedisFailed;
						std::string return_str = rtvalue.toStyledString();
						session->Send(return_str, ID_IMG_CHAT_CONTINUE_UPLOAD_RSP);
						return;
					}
				}


				FileSystem::GetInstance()->PostMsgToQue(
					std::make_shared<FileTask>(session, ID_IMG_CHAT_CONTINUE_UPLOAD_REQ, uid, file_path_str, name, seq, total_size,
						trans_size, last, file_data, callback, message_id, sender, receiver),
					index
				);
			};

		// 聊天图片下载信息同步：按 message_id 查库，返回图片文件名/大小/收发双方等下载所需信息
		_fun_callbacks[ID_IMG_CHAT_DOWN_INFO_SYNC_REQ] = [this](std::shared_ptr<CSession> session, const short& msg_id,
			const string& msg_data) {
				Json::Reader reader;
				Json::Value root;
				reader.parse(msg_data, root);
				auto message_id = root["message_id"].asInt();
				auto chat_msg = MysqlMgr::GetInstance()->GetChatMsgById(message_id);
				if (chat_msg == nullptr) {
					Json::Value rtvalue;
					rtvalue["error"] = ErrorCodes::MsgIdErr;
					return;
				}

				// 资源文件路径
				auto file_dir = ConfigMgr::Inst().GetFileOutPath();
				//该消息是接收方客户端发送过来的,服务器将资源存储在发送方的文件夹中
				auto uid_str = std::to_string(chat_msg->sender_id);
				auto file_path = (file_dir / uid_str / chat_msg->content);
				/*boost::uintmax_t file_size = boost::filesystem::file_size(file_path);*/
				boost::system::error_code ec;
				boost::uintmax_t file_size = boost::filesystem::file_size(file_path, ec);
				if (ec) {
					Json::Value rtvalue;
					rtvalue["error"] = ErrorCodes::FileNotExists;
					rtvalue["message_id"] = message_id;
					session->Send(rtvalue.toStyledString(), ID_IMG_CHAT_DOWN_INFO_SYNC_RSP);
					return;
				}

				// 在异步任务完成后调用
				Json::Value rtvalue;
				rtvalue["error"] = ErrorCodes::Success;
				rtvalue["message_id"] = chat_msg->message_id;
				rtvalue["thread_id"] = chat_msg->thread_id;
				rtvalue["sender_id"] = chat_msg->sender_id;
				rtvalue["recv_id"] = chat_msg->recv_id;
				rtvalue["name"] = chat_msg->content;
				rtvalue["msg_type"] = chat_msg->msg_type;
				rtvalue["status"] = chat_msg->status;
				rtvalue["total_size"] = std::to_string(file_size);
				std::string return_str = rtvalue.toStyledString();
				session->Send(return_str, ID_IMG_CHAT_DOWN_INFO_SYNC_RSP);
			};

		// 聊天图片下载：首片校验 token，按文件名哈希选 DownloadWorker，逐片读取并回包
		_fun_callbacks[ID_IMG_CHAT_DOWN_REQ] = [this](std::shared_ptr<CSession> session, const short& msg_req_id,
			const string& msg_data) {

				Json::Reader reader;
				Json::Value root;
				reader.parse(msg_data, root);

				auto seq = root["seq"].asInt();
				auto name = root["name"].asString();
				auto total_size_str = root["total_size"].asString();
				auto trans_size_str = root["trans_size"].asString();
				auto file_path = ConfigMgr::Inst().GetFileOutPath();
				auto message_id = root["message_id"].asInt();
				auto sender = root["sender_id"].asInt();
				auto receiver = root["receiver_id"].asInt();
				auto token = root["token"].asString();
				auto uid = root["uid"].asInt();

				auto callback = [=](const Json::Value& result) {
					// 在异步任务完成后调用
					Json::Value rtvalue = result;
					rtvalue["error"] = ErrorCodes::Success;
					rtvalue["name"] = name;
					rtvalue["sender_id"] = sender;
					rtvalue["receiver_id"] = receiver;
					std::string return_str = rtvalue.toStyledString();
					session->Send(return_str, ID_IMG_CHAT_DOWN_RSP);
					};

				// 使用 std::hash 对字符串进行哈希
				std::hash<std::string> hash_fn;
				size_t hash_value = hash_fn(name); // 生成哈希值
				int index = hash_value % DOWN_LOAD_WORKER_COUNT;
				std::cout << "Hash value: " << hash_value << std::endl;


				//第一个包校验一下token是否合理
				if (seq == 1) {
					//从redis获取用户token是否正确
					std::string uid_str = std::to_string(uid);
					std::string token_key = USERTOKENPREFIX + uid_str;
					std::string token_value = "";
					bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
					Json::Value  rtvalue;
					if (!success) {
						rtvalue["error"] = ErrorCodes::UidInvalid;
						std::string return_str = rtvalue.toStyledString();
						session->Send(return_str, ID_IMG_CHAT_DOWN_RSP);
						return;
					}

					if (token_value != token) {
						rtvalue["error"] = ErrorCodes::TokenInvalid;
						std::string return_str = rtvalue.toStyledString();
						session->Send(return_str, ID_IMG_CHAT_DOWN_RSP);
						return;
					}
				}

				auto sender_str = std::to_string(sender);
				//转化为字符串
				auto uid_str = std::to_string(uid);
				auto file_path_str = (file_path / sender_str / name).string();

				auto down_load_task = std::make_shared<DownloadTask>(session, uid, name, seq, file_path_str, callback);

				FileSystem::GetInstance()->PostDownloadTaskToQue(down_load_task, index);
			};
}

void LogicWorker::task_callback(std::shared_ptr<LogicNode>task){
	cout << "recv_msg id  is " << task->_recvnode->_msg_id << endl;
	auto call_back_iter = _fun_callbacks.find(task->_recvnode->_msg_id);
	if (call_back_iter == _fun_callbacks.end()) {
		return;
	}
	call_back_iter->second(task->_session, task->_recvnode->_msg_id,
		std::string(task->_recvnode->_data, task->_recvnode->_cur_len));

}