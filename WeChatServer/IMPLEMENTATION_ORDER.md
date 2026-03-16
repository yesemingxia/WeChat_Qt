项目实现顺序与依赖说明

说明：以下顺序按“可复用基础设施 -> 业务基础能力 -> 对外接口 -> 入口程序”排列。每条包含“文件职责”和“依赖关系”，用于明确实现先后与调用链。

1. `const.h` - 统一基础类型、错误码、公共包含、UserInfo 结构与常量。依赖：无。
2. `Singleton.h` - 通用单例模板，提供全局实例管理。依赖：`<memory> <mutex>`。
3. `Defer.h` - RAII 清理工具，保证资源释放。依赖：`<functional>`。
4. `ConfigMgr.h` + `ConfigMgr.cpp` - 读取 `config.ini` 的配置管理器，提供按 section/key 获取配置。依赖：`const.h`，`boost::filesystem`，`boost::property_tree`。
5. `config.ini` - 运行时配置项（端口、Redis/MySQL/Grpc 地址等）。依赖：由 `ConfigMgr` 加载。
6. `message.proto` - gRPC/消息协议定义，后续生成 C++ 代码。依赖：无。
7. `message.pb.h` + `message.pb.cc` - 由 `message.proto` 生成的 Protobuf 数据结构代码，不手写。依赖：`message.proto`。
8. `message.grpc.pb.h` + `message.grpc.pb.cc` - 由 `message.proto` 生成的 gRPC Stub 代码，不手写。依赖：`message.proto`。
9. `AsioIOServicePool.h` + `AsioIOServicePool.cpp` - 多 `io_context` 线程池与 round-robin 调度。依赖：`Singleton.h`，`boost::asio`。
10. `RedisConPool.h` + `RedisConPool.cpp` - Redis 连接池封装（连接/认证/归还）。依赖：`const.h`，hiredis。
11. `RedisMgr.h` + `RedisMgr.cpp` - Redis 高层操作（KV、List、Hash 等），对业务提供统一接口。依赖：`Singleton.h`，`ConfigMgr`，`RedisConPool`。
12. `MysqlDao.h` + `MysqlDao.cpp` - MySQL 数据访问层，包含连接池与 SQL/存储过程调用。依赖：`ConfigMgr`，`const.h`，`Defer.h`，mysqlx。
13. `MysqlMgr.h` + `MysqlMgr.cpp` - MySQL 业务聚合层，封装对 `MysqlDao` 的调用。依赖：`Singleton.h`，`MysqlDao`，`const.h`。
14. `VerifyGrpcClient.h` + `VerifyGrpcClient.cpp` - 验证码服务 gRPC 客户端（连接池 + 调用）。依赖：`Singleton.h`，`ConfigMgr`，`message.grpc.pb.*`。
15. `StatusGrpcClient.h` + `StatusGrpcClient.cpp` - 状态服务 gRPC 客户端（连接池 + 调用）。依赖：`Singleton.h`，`ConfigMgr`，`message.grpc.pb.*`。
16. `LogicSystem.h` + `LogicSystem.cpp` - HTTP 路由注册与业务处理（注册、登录、验证码、状态获取等）。依赖：`HttpConnection`，`RedisMgr`，`MysqlMgr`，`VerifyGrpcClient`，`StatusGrpcClient`，`const.h`。
17. `HttpConnection.h` + `HttpConnection.cpp` - HTTP 连接生命周期管理、请求解析、路由分发、响应输出。依赖：`LogicSystem`，`const.h`，`boost::beast`。
18. `CServer.h` + `CServer.cpp` - 监听端口、accept 新连接、创建 `HttpConnection`。依赖：`AsioIOServicePool`，`HttpConnection`。
19. `WeChatServer.cpp` - 进程入口：读取配置、创建服务器、启动 IO 事件循环。依赖：`ConfigMgr`，`CServer`，`boost::asio`。

备注
1. `WeChatServer.vcxproj` 和 `WeChatServer.vcxproj.filters` 为工程配置文件，不属于运行期逻辑实现，但需包含必要依赖库与生成文件。
2. 生成文件 `message.pb.*` 和 `message.grpc.pb.*` 不应手写，确保由 `message.proto` 生成并加入工程。
