# ============================================================
# WeChatServer（Gate 网关服务：HTTP 请求转发 + 业务处理）
#
# 本机依赖（按原构建环境还原）：
#   vcpkg    d:/vcpkg-master   -> grpc、protobuf、hiredis、mysql-connector-cpp、openssl、zlib、re2、c-ares、abseil
#   Boost    d:/boost_1_88_0   -> system、filesystem（自行编译）
#   jsoncpp  d:/jsoncpp-master -> 自行编译（仅 Debug 版本，动态库）
#
# 配置示例：
#   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
#         -DCMAKE_TOOLCHAIN_FILE=d:/vcpkg-master/scripts/buildsystems/vcpkg.cmake
# ============================================================
cmake_minimum_required(VERSION 3.21)

# CMake 4 已移除 FindBoost 模块：置为新行为，find_package(Boost) 只走 Config 模式
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(WeChatServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)      # 源码含中文注释，避免乱码 / C4819
    add_link_options(/IGNORE:4221)   # 忽略空编译单元的 LNK4221 警告
endif()

# ---------------- 第三方依赖 ----------------
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)
find_package(unofficial-mysql-connector-cpp CONFIG REQUIRED)

if(NOT DEFINED Boost_DIR)
    set(Boost_DIR "d:/boost_1_88_0/stage/lib/cmake/Boost-1.88.0" CACHE PATH "Boost 1.88.0 的 CMake 配置目录")
endif()
find_package(Boost REQUIRED COMPONENTS system filesystem)

# jsoncpp（本机自行编译，无 CMake 配置文件，手动查找）
set(JSONCPP_ROOT "d:/jsoncpp-master" CACHE PATH "jsoncpp 源码目录")
find_path(JSONCPP_INCLUDE_DIR NAMES json/json.h HINTS "${JSONCPP_ROOT}/include" NO_DEFAULT_PATH)
find_library(JSONCPP_LIBRARY NAMES jsoncpp HINTS "${JSONCPP_ROOT}/visualpro/lib/Debug" NO_DEFAULT_PATH)
find_file(JSONCPP_DLL NAMES jsoncpp.dll HINTS "${JSONCPP_ROOT}/visualpro/bin/Debug" NO_DEFAULT_PATH)
if(NOT JSONCPP_INCLUDE_DIR OR NOT JSONCPP_LIBRARY OR NOT JSONCPP_DLL)
    message(FATAL_ERROR "未找到 jsoncpp，请通过 -DJSONCPP_ROOT=<jsoncpp 源码目录> 指定正确路径")
endif()

# ---------------- 可执行程序 ----------------
# 注意：message.pb.cc / message.grpc.pb.cc 为 message.proto 的预生成代码，随仓库一起提交
add_executable(wechat_server
    AsioIOServicePool.cpp
    CServer.cpp
    ConfigMgr.cpp
    HttpConnection.cpp
    LogicSystem.cpp
    MysqlDao.cpp
    MysqlMgr.cpp
    RedisConPool.cpp
    RedisMgr.cpp
    StatusGrpcClient.cpp
    VerifyGrpcClient.cpp
    WeChatServer.cpp
    message.pb.cc
    message.grpc.pb.cc
)

target_include_directories(wechat_server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${JSONCPP_INCLUDE_DIR}"
)

target_compile_definitions(wechat_server PRIVATE
    BOOST_SYSTEM_NO_LIB
    BOOST_FILESYSTEM_NO_LIB
    BOOST_ATOMIC_NO_LIB
    ABSL_CONSUME_DLL
    PROTOBUF_USE_DLLS
)

target_link_libraries(wechat_server PRIVATE
    gRPC::grpc++
    protobuf::libprotobuf
    hiredis::hiredis
    unofficial::mysql-connector-cpp::connector
    Boost::system
    Boost::filesystem
    "${JSONCPP_LIBRARY}"
)

# ---------------- 运行时部署 ----------------
# 程序通过当前目录读取 config.ini，构建后把配置与所需 DLL 复制到 exe 目录
add_custom_command(TARGET wechat_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:wechat_server> $<TARGET_FILE_DIR:wechat_server>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${JSONCPP_DLL}" $<TARGET_FILE_DIR:wechat_server>
    COMMENT "复制运行所需 DLL 到输出目录"
    COMMAND_EXPAND_LISTS
)

# config.ini 含数据库密码，不纳入仓库：存在时才复制（缺失时需自行放置到 exe 目录）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config.ini")
    add_custom_command(TARGET wechat_server POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/config.ini" $<TARGET_FILE_DIR:wechat_server>
        COMMENT "复制 config.ini 到输出目录"
    )
else()
    message(WARNING "未找到 WeChatServer/config.ini：运行时请自行放置到 exe 同目录")
endif()

# VS 调试运行时的工作目录（保证能找到 config.ini）
set_target_properties(wechat_server PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:wechat_server>"
)