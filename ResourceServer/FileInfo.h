#pragma once
#include <string>

// 文件传输信息，记录一次文件发送/接收的进度，支持断点续传
class FileInfo
{
public:
	FileInfo(int seq =0,std::string name ="" ,int64_t total_size = 0,
		int64_t trans_size =0,std::string file_path_str = "")
		:_seq(seq), _name(name), _total_size(total_size),
		_trans_size(trans_size), _file_path_str(file_path_str)
	{}



	int _seq;					//分片序号
	std::string _name;	
	int64_t _total_size;	
	int64_t _trans_size;	
	std::string _file_path_str; //存储路径


};

// 聊天图片信息，描述一张聊天图片属于哪条会话消息，用于资源服务器转发和客户端下载
class ChatImgInfo {
public:
	// sender_id: 发送者id  receiver_id: 接收者id  message_id: 消息id  img_name: 图片名
	ChatImgInfo(int sender_id, int receiver_id, int message_id, std::string img_name)
		:_sender_id(sender_id), _receiver_id(receiver_id), _message_id(message_id), _img_name(img_name) {
	}
	int _sender_id;		// 发送者用户id
	int _receiver_id;	// 接收者用户id
	int _message_id;	// 关联的聊天消息id
	std::string _img_name;	// 图片文件名
};
