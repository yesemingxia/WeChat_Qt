# WeChatServer Gate 服务器逻辑说明

**用途**  
该文档用于直观说明 `WeChatServer` 的 Gate 服务器做什么、请求如何流转、有哪些接口，以及它与 Redis/MySQL/gRPC 的关系。

![alt text](cuiruonilan.png)
---

## 一句话概览
这是一个基于 Boost.Asio/Beast 的 HTTP Gate Server，负责接收客户端 HTTP 请求，转发到本地业务逻辑与后端服务（Redis/MySQL/gRPC），并返回 JSON 响应。

## 核心组件
- `WeChatServer.cpp`: 入口与启动逻辑，读取 `config.ini`，启动 `CServer` 并监听端口
- `CServer.cpp/.h`: TCP acceptor，接受连接并创建 `HttpConnection`
- `HttpConnection.cpp/.h`: 解析 HTTP 请求，调用 `LogicSystem` 路由处理，写回响应
- `LogicSystem.cpp/.h`: 业务路由与处理函数注册（GET/POST）
- `RedisMgr.*`: 用于验证码缓存校验
- `MysqlMgr.*`: 用户注册、登录、重置密码相关数据持久化
- `VerifyGrpcClient.*`: 获取验证码的 gRPC 客户端
- `StatusGrpcClient.*`: 获取可用 ChatServer 的 gRPC 客户端

## 请求处理流程（HTTP）
- `CServer` 监听端口，异步 accept 新连接
- 每个连接由 `HttpConnection` 管理
- `HttpConnection` 读取请求后
- GET：解析 URL 参数并调用 `LogicSystem::HandleGet`
- POST：直接将请求体交给 `LogicSystem::HandlePost`
- 若路由不存在返回 `404 url not found`
- 正常返回 `200` 且 `Server: GateServer`

## 路由与业务逻辑

### GET
- `/get_test`
- 读取 query 参数并回写到响应体，用于测试

### POST
- `/get_varifycode`
- 输入：`{ "email": "xx@xx.com" }`
- 调用 `VerifyGrpcClient::GetVerifycode(email)`
- 返回：`error` + `email`

- `/user_register`
- 输入：`user`, `email`, `passwd`, `confirm`, `varifycode`
- 校验
- `passwd == confirm`
- Redis 中 `code_{email}` 是否存在且匹配
- MySQL 注册（用户/邮箱是否存在）
- 返回：`error` + `uid` + 用户信息

- `/reset_pwd`
- 输入：`user`, `email`, `passwd`, `varifycode`
- 校验
- Redis 验证码存在且匹配
- MySQL `user/email` 是否匹配
- 更新密码并返回 `error` + 用户信息

- `/user_login`
- 输入：`user`, `passwd`
- 校验
- MySQL 用户名/密码
- gRPC 获取 ChatServer（host/port/token）
- 返回：`error` + `uid` + `token` + `host` + `port`

## 配置
- `config.ini`
- `GateServer.Port`: HTTP 监听端口

## 错误码（ErrorCodes）
- `0` Success
- `1001` Json 解析错误
- `1002` RPC 请求错误
- `1003` 验证码过期
- `1004` 验证码错误
- `1005` 用户已存在
- `1006` 密码不一致
- `1007` 邮箱不匹配
- `1008` 更新密码失败
- `1009` 密码无效
- `1010` 获取 ChatServer 失败

## 依赖库
- Boost.Asio / Boost.Beast
- JsonCpp
- gRPC C++（客户端）
- hiredis
- MySQL Connector/C++

## 运行与操作
- 确保 Redis 与 MySQL 先启动
- 确保 VarifyServer 与 StatusServer 已启动
- 配置 `config.ini` 中的端口与依赖地址
- 启动 GateServer，监听 `GateServer.Port`

## 相关文件索引
- `WeChatServer.cpp`
- `CServer.cpp`
- `HttpConnection.cpp`
- `LogicSystem.cpp`
- `RedisMgr.cpp`
- `MysqlMgr.cpp`
- `VerifyGrpcClient.cpp`
- `StatusGrpcClient.cpp`
- `config.ini`

---

# VarifyServer（验证码服务）

**用途**  
提供 gRPC 接口生成并发送邮箱验证码，依赖 Redis 缓存验证码并设置过期时间。


## 核心逻辑
- 入口：`server.js`
- gRPC 方法：`GetVarifyCode`
- 读取 Redis 的 `code_{email}`
- 不存在则生成 4 位验证码（UUID 截断）并写入 Redis（TTL 600 秒）
- 通过 `nodemailer` 发送邮件
- 返回 `email` + `error`

## 依赖组件
- Redis：`redis.js`（ioredis 客户端）
- 邮件发送：`email.js`（SMTP 163 邮箱）
- 配置：`config.json` + `config.js`

## gRPC 监听
- `0.0.0.0:50051`

## 错误码
- `0` Success
- `1` RedisErr
- `2` Exception

## 依赖库
- Node.js
- `@grpc/grpc-js`
- `nodemailer`
- `ioredis`
- `uuid`

## 运行与操作
- 安装依赖：`npm install`
- 配置 `config.json` 中的邮箱、Redis 信息
- 启动服务：`node server.js`

## 相关文件索引
- `D:\vs.c++\VarifyServer\server.js`
- `D:\vs.c++\VarifyServer\redis.js`
- `D:\vs.c++\VarifyServer\email.js`
- `D:\vs.c++\VarifyServer\config.js`
- `D:\vs.c++\VarifyServer\config.json`

---

# ChatServer1（聊天服务）

**用途**  
负责 TCP 长连接聊天、消息路由、好友申请/认证、文本消息转发，并提供 gRPC 给其他 ChatServer 做跨服消息通知。


## 核心组件
- `ChatServer.cpp`: 启动 TCP 服务 + gRPC 服务
- `CServer.cpp/.h`: 接收 TCP 连接并管理会话集合
- `CSession.cpp/.h`: 消息协议解析与发送（头 4 字节：`msg_id` + `msg_len`）
- `LogicSystem.cpp/.h`: 消息分发与业务处理队列
- `ChatServiceImpl.cpp/.h`: gRPC 服务实现（跨服通知）
- `StatusGrpcClient.cpp/.h`: 与 StatusServer 通信（登录校验/分配 ChatServer）
- `RedisMgr.*`: 会话缓存、分布式锁、在线统计
- `MysqlMgr.*`: 用户信息、好友关系

## 处理流程（TCP）
- `CServer` 监听端口，建立 `CSession`
- `CSession` 读取消息头（`msg_id` + `msg_len`），再读取 body
- `LogicSystem` 将消息投递到内部队列，由工作线程回调处理
- 需要跨服转发时通过 `ChatGrpcClient` 发送到目标 ChatServer

## 主要消息 ID（来自 `const.h`）
- `1005` 登录请求 `MSG_CHAT_LOGIN`
- `1007` 搜索用户 `ID_SEARCH_USER_REQ`
- `1009` 添加好友 `ID_ADD_FRIEND_REQ`
- `1013` 好友认证 `ID_AUTH_FRIEND_REQ`
- `1017` 文本消息 `ID_TEXT_CHAT_MSG_REQ`
- `1023` 心跳 `ID_HEART_BEAT_REQ`

## gRPC 服务（跨服通知）
- `NotifyAddFriend`
- `NotifyAuthFriend`
- `NotifyTextChatMsg`
- `NotifyKickUser`

## 依赖外部服务
- StatusServer：校验 token、获取 ChatServer 地址
- Redis：会话、锁、在线统计、用户缓存
- MySQL：用户资料、好友关系

## 配置
- `config.ini`
- `SelfServer.Port` / `SelfServer.RPCPort`
- `StatusServer.Host/Port`
- `Redis`、`Mysql`

## 依赖库
- Boost.Asio
- gRPC C++
- JsonCpp
- hiredis
- MySQL Connector/C++

## 运行与操作
- 确保 Redis 与 MySQL 先启动
- 确保 StatusServer 已启动
- 配置 `config.ini` 中端口与依赖地址
- 启动 ChatServer，监听 `SelfServer.Port` 与 `SelfServer.RPCPort`

## 相关文件索引
- `D:\vs.c++\ChatServer1\ChatServer1\ChatServer.cpp`
- `D:\vs.c++\ChatServer1\ChatServer1\CServer.cpp`
- `D:\vs.c++\ChatServer1\ChatServer1\CSession.cpp`
- `D:\vs.c++\ChatServer1\ChatServer1\LogicSystem.cpp`
- `D:\vs.c++\ChatServer1\ChatServer1\ChatServiceImpl.cpp`
- `D:\vs.c++\ChatServer1\ChatServer1\StatusGrpcClient.cpp`
- `D:\vs.c++\ChatServer1\ChatServer1\config.ini`

---

# StatusServer（状态服务）

**用途**  
提供 gRPC 服务，用于分配 ChatServer 和校验登录 token。


## 核心逻辑
- `GetChatServer`
- 从配置的 ChatServer 列表中轮询选取
- 生成 token（UUID），写入 Redis：`utoken_{uid}`
- `Login`
- 校验 Redis 中 token 是否与请求一致

## gRPC 监听
- `config.ini` 中的 `StatusServer.Host/Port`

## 依赖组件
- Redis：存储 token
- ConfigMgr：读取 ChatServer 列表

## 依赖库
- gRPC C++
- hiredis
- Boost.Asio

## 运行与操作
- 确保 Redis 先启动
- 配置 `config.ini` 中端口与 Redis 地址
- 启动 StatusServer，监听 `StatusServer.Host/Port`

## 相关文件索引
- `D:\vs.c++\StatusServer\StatusServer\StatusServer.cpp`
- `D:\vs.c++\StatusServer\StatusServer\StatusServiceImpl.cpp`
- `D:\vs.c++\StatusServer\StatusServer\config.ini`

---

# WeChat Qt 客户端（桌面）

**用途**  
Qt 客户端 UI，负责用户注册/登录/重置密码，并与 ChatServer 建立 TCP 聊天连接。


## 启动流程
- `main.cpp` 读取 `config.ini` 中 `GateServer.host/port`，组装 `gate_url_prefix`
- 加载 QSS 样式并启动 `MainWindow`

## HTTP 逻辑（GateServer）
- `HttpMgr` 统一发送 POST 请求
- 注册
- `/get_varifycode`
- `/user_register`
- 重置密码
- `/get_varifycode`
- `/reset_pwd`
- 登录
- `/user_login`

## TCP 逻辑（ChatServer）
- `TcpMgr` 负责连接、收发与协议解析
- 登录成功后发送 `ID_CHAT_LOGIN`（携带 `uid` + `token`）
- 后续聊天消息按 `msg_id` 分发

## 依赖库
- Qt 6
- Qt Network
- Qt Widgets

## 运行与操作
- 配置 `config.ini` 中的 GateServer 地址
- 启动客户端并进行注册/登录
- 登录成功后建立 TCP 连接并进入聊天

## 相关文件索引
- `D:\c++project\Wecahtc++Qt\WeChat\main.cpp`
- `D:\c++project\Wecahtc++Qt\WeChat\httpmgr.cpp`
- `D:\c++project\Wecahtc++Qt\WeChat\tcpmgr.cpp`
- `D:\c++project\Wecahtc++Qt\WeChat\logindialog.cpp`
- `D:\c++project\Wecahtc++Qt\WeChat\registerdialog.cpp`
- `D:\c++project\Wecahtc++Qt\WeChat\resetdialog.cpp`
- `D:\c++project\Wecahtc++Qt\WeChat\config.ini`
