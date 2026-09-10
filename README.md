# sovkit-stun
A lightweight IPv4 UDP STUN Binding server from SovKit.

独立的 IPv4 UDP STUN Binding 服务。可以作为进程运行，也可以静态链接到 C++ 宿主。
只依赖 C++20 标准库和 libuv，不需要 SovKit SDK、Flutter、数据库或账号系统。

当前为 0.1.0 开发预览，不是稳定版；首个预览标签为 v0.1.0-preview.1。
下载与发布状态以 [GitHub Releases](https://github.com/memade/sovkit-stun/releases) 为准，
范围与使用限制见 [预览说明](docs/releases/v0.1.0-preview.1.md)。
协议实现来自 SovKit 的 libnet；导出边界见 [源码来源](docs/PROVENANCE.md)。

## 范围

| 提供 | 不提供 |
| --- | --- |
| IPv4 UDP Binding、XOR-MAPPED-ADDRESS、可选 FINGERPRINT | IPv6/TCP/TLS STUN、TURN、数据中继 |
| 两个固定监听端口、每来源及全局限速 | 完整 RFC 8489 服务、ICE agent、NAT 类型分类 |
| 显式开启的跨端口诊断扩展 | 通用 RFC 5780 CHANGE-REQUEST、任意目的地址回包 |
| 启停及汇总日志、内存状态 | 身份库、SQLite、文件/消息业务、网络共享 |

不支持的必需属性和异常报文会被丢弃，不返回完整 STUN 错误响应。
Binding 只能观测该请求的映射，不能保证两个客户端一定能直连。
协议和资源限制见 [PROTOCOL.md](docs/PROTOCOL.md)。

## 构建

依赖：CMake ≥ 3.20，支持 C++20 的编译器，libuv ≥ 1.44。
测试另需 Python ≥ 3.9，无第三方 Python 包。构建不自动下载依赖。

Ubuntu 24.04 / Debian 12：

~~~sh
sudo apt-get install build-essential cmake ninja-build pkg-config libuv1-dev python3
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
~~~

macOS（已安装 Homebrew）：

~~~sh
brew install cmake ninja pkg-config libuv
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
~~~

也支持提供 libuvConfig.cmake 的 libuv 安装，通过 -DCMAKE_PREFIX_PATH=/path/to/prefix
指定；这只是公共 libuv 依赖，不要求主工程存在。测试仅使用本机回环和临时端口，
包含一次真实 30 秒凭据过期等待。不启动系统服务或访问公网 STUN。

## 运行

~~~sh
# 默认本机双端口；Ctrl-C 退出
./build/sovkit-stun

# 独立回环验证，60 秒后退出
./build/sovkit-stun --bind 127.0.0.1 --ports 13478,13479 --duration 60

# 或使用配置文件；不能与其它配置选项混用
./build/sovkit-stun --config packaging/server.conf
~~~

--help 查看选项。绑定公共接口需显式指定地址，并先评估 UDP 反射风险；
仓库不提供默认开放公网的启动配置，不自动修改防火墙。

日志为逐行 JSON：启动信息、每 30 秒汇总、退出汇总；错误写 stderr。
不输出来源 IP、事务 ID、诊断凭据或逐包记录，不创建应用数据、缓存、数据库和日志文件。
前台由调用方处理 stdout/stderr；systemd 模板交给 journal，保留策略由系统管理员控制。
退出码：0 正常停止，1 启动/运行错误，2 配置错误。

## 作为静态库接入

将此仓库作为子目录构建，链接 SovkitStun::stun（产物 libsovkit-stun.a）：

~~~cmake
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(external/sovkit-stun)
target_link_libraries(my_host PRIVATE SovkitStun::stun)
~~~

~~~cpp
#include <libnet_stun_server.h>

libnet::StunServer server;
libnet::StunServerConfig config; // 默认仅本机、诊断关闭
const int result = server.Start(config); // 0 成功，否则 libuv 错误码
// 宿主处理 result，运行自己的业务；可调用 server.Snapshot() 获取快照。
server.Stop(); // 返回前关闭套接字并回收线程；析构也会 Stop。
~~~

示例中的 Start 与 Stop 之间由宿主维持生命周期。该库不是 libsovkit 的 C ABI，
也不应和主工程另一份 libnet 定义重复链接；现有 SovKit 继续使用其原内置实现。
此轮未改主工程的依赖关系、com.skstu.sovkit 或客户端运行目录。
低层库保留 diagnostics 内存观测能力；显式开启会收集来源 IP 和原始事务 ID，
宿主必须限制访问并在导出前脱敏。独立命令行程序不开放此开关。

## 部署与发布

Linux 服务模板及手动安装说明见 [DEPLOYMENT.md](docs/DEPLOYMENT.md)。
cmake --install 只安装程序、静态库、头文件、CMake 配置、许可和示例，不写 /etc，不启动/启用服务。
静态库支持源码子目录或安装式 CMake 包；不保证跨编译器/标准库的稳定 C++ ABI。

安装后可用 find_package 接入：

~~~cmake
find_package(SovkitStun 0.1.0 EXACT CONFIG REQUIRED)
target_link_libraries(my_host PRIVATE SovkitStun::stun)
~~~

消费方需要相同平台/架构的公共 libuv ≥ 1.44。独立示例位于 examples/sdk/，
安装后在 share/sovkit-stun/examples/sdk/；迁移安装目录后仍可构建，不依赖原源码目录。
本地源码包与 macOS 程序/静态库预览包见 [RELEASING.md](docs/RELEASING.md)。

macOS 与 Linux ARM64 实机的独立构建、回环和安装式 SDK 测试已通过；尚未部署 Linux 系统服务。
发布前仍需代码权属确认、许可证与二进制依赖清单检查、对应发行包审核。
回环测试不代表公网安全验收完成。Windows 不在本次验收范围。
本轮实际环境、测试覆盖和未验收项见 [VALIDATION.md](docs/VALIDATION.md)。

MIT，原有版权声明见 [LICENSE](LICENSE)；libuv 的再分发要求见 [NOTICE](NOTICE)。
