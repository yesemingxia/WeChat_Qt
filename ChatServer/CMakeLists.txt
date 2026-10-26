# ============================================================
# ChatServer（微信聊天服务端：TCP 长连接 + gRPC 跨服通知）
#
# 目录结构：
#   include/  头文件（含 protoc 生成的头文件）
#   src/      源文件（含 protoc 生成的 message.pb.cc / message.grpc.pb.cc）
#   proto/    message.proto 原始协议文件
#   config/   运行配置 config.ini（构建后自动复制到 exe 目录）
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

project(ChatServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)      # 源码含中文注释，避免乱码 / C4819
    add_link_options(/IGNORE:4221)   # 忽略空编译单元的 LNK4221 警告
endif()

# ---------------- 第三方依赖 ----------------
# grpc / protobuf（vcpkg 提供）
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

# hiredis（vcpkg 提供）
find_package(hiredis CONFIG REQUIRED)

# MySQL Connector/C++（vcpkg 的 unofficial-mysql-connector-cpp，提供 mysqlcppconnx）
find_package(unofficial-mysql-connector-cpp CONFIG REQUIRED)

# Boost 1.88.0（本机自行编译，位于 d:/boost_1_88_0）
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
# 注意：message.pb.cc / message.grpc.pb.cc 是 proto/message.proto 的预生成代码，
#      随仓库一起提交；修改 proto 后需用 protoc / grpc_cpp_plugin 重新生成到 src/ 与 include/。
add_executable(chat_server
    src/AsioIOServicePool.cpp
    src/ChatGrpcClient.cpp
    src/ChatServer.cpp
    src/ChatServiceImpl.cpp
    src/ConfigMgr.cpp
    src/CServer.cpp
    src/CSession.cpp
    src/DistLock.cpp
    src/LogicSystem.cpp
    src/MsgNode.cpp
    src/MysqlDao.cpp
    src/MysqlMgr.cpp
    src/RedisMgr.cpp
    src/StatusGrpcClient.cpp
    src/UserMgr.cpp
    src/utils.cpp
    src/message.pb.cc
    src/message.grpc.pb.cc
)

target_include_directories(chat_server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${JSONCPP_INCLUDE_DIR}"
)

target_compile_definitions(chat_server PRIVATE
    BOOST_SYSTEM_NO_LIB
    BOOST_FILESYSTEM_NO_LIB
    BOOST_ATOMIC_NO_LIB
    ABSL_CONSUME_DLL
    PROTOBUF_USE_DLLS
)

target_link_libraries(chat_server PRIVATE
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
add_custom_command(TARGET chat_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:chat_server> $<TARGET_FILE_DIR:chat_server>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${JSONCPP_DLL}" $<TARGET_FILE_DIR:chat_server>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/config/config.ini" $<TARGET_FILE_DIR:chat_server>
    COMMENT "复制运行所需 DLL 与 config.ini 到输出目录"
    COMMAND_EXPAND_LISTS
)

# VS 调试运行时的工作目录（保证能找到 config.ini）
set_target_properties(chat_server PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:chat_server>"
)