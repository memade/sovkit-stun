# 独立导出验证（2026-09-10）

对应首轮独立源码与开发预览；不是稳定版、公网部署或穿透成功声明。

## 环境与结果

- macOS 26.6.2，arm64；AppleClang 21.0.0；CMake 4.3.2；Python 3.14.4。
- 公共依赖 libuv 1.52.1，复用本机已有 vcpkg 安装，不编译/链接主工程 SDK。
- Release 和 Debug + ASan/UBSan 均完成独立编译与测试。
- CTest：codec、source_manifest、udp_service、installed_sdk、package_checks 五组全部通过；
  UDP 组含 11 项测试；发布前打包组增至 9 项，补充完整 libuv 许可与版本匹配拒绝检查。
- codec 覆盖长度、来源、映射、属性边界及 20,000 组确定性随机报文。
  不是完整 fuzz campaign，也不代表完整 RFC 兼容性验证。
- UDP 测试覆盖双端口 Binding/FINGERPRINT、异常/超长包丢弃、不回显可选负载、
  每来源限速及恢复、默认关闭的回探扩展、来源/监听端口绑定、8 次回探预算、
  30 秒凭据过期、限时停止、SIGINT/SIGTERM、端口占用回滚、配置拒绝和日志隐私。
- 本轮 ASan/UBSan 测试未报告错误；外部预编译 libuv 本身没有重新插桩。
- 5 项导出清单哈希及 Endpoint 提取结果与本机源工程核对通过。
- 安装式 SDK 在临时前缀安装、迁移到含空格路径，再由独立 CMake 宿主编译并运行通过；
  安装配置不携带原构建目录/原安装前缀。
- 打包检查覆盖白名单、路径/符号链接/明显凭据拒绝、缓存混入拒绝、归档摘要、篡改拒绝、
  相同内容归档一致、不覆盖旧包及正式发布声明拒绝。不是完整安全审计。

## Linux ARM64 实机补验

2026-09-10，在 OrangePi Zero2 W 上独立执行，不复用 macOS 二进制或主工程 SDK：

- Debian 12（bookworm）、aarch64、内核 6.1.31-sun50iw9，约 1 GB 内存。
- GCC/G++ 12.2.0、CMake 3.25.1、Ninja 1.11.1、Python 3.11.2；系统 pkg-config 提供 libuv 1.44.2。
- 白名单源码包 SHA-256：a5e4a1aab6774d9dcaec9fab8c7fb9f47d366c7286a7bdfc0b73a53f6b8a33fc；
  上传后校验归档及包内逐文件摘要，再单任务原生构建 Release。
- CTest 5/5 通过，47.33 秒；UDP 11 项、打包 8 项、codec 和安装迁移 SDK 均通过。
- 只在普通用户私有目录编译，安装式 SDK 测试使用临时前缀；无系统 STUN 服务部署。
- 首次预检曾因根分区只读/EXT4 错误中止；经设备所有者授权恢复后重新预检，
  启动 fsck 已执行，根分区读写、超级块 clean，测试期没有发现新的 EXT4/I/O 错误。
  这不等于 SD 卡硬件或长期存储可靠性认证。

以上摘要标识 Linux 测试时的源码快照；后续只调整发布文档及打包许可门禁，未改原生实现、
CMake 接入或 UDP/codec/SDK 测试。新增第 9 项许可打包检查在 macOS 执行，没有追加 Linux 验收。
设备地址、账号和原始系统日志不进入公开源码包。

## 重现

以下 LIBUV_PREFIX 应指向任意提供 libuvConfig.cmake 的公共 libuv 安装前缀；
使用系统 pkg-config 的 libuv 时，省略 CMAKE_PREFIX_PATH 参数即可。

~~~sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$LIBUV_PREFIX"
cmake --build build
ctest --test-dir build --output-on-failure -V

cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DSTUN_SANITIZERS=ON -DCMAKE_PREFIX_PATH="$LIBUV_PREFIX"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure -V

python3 tools/verify_import.py
cmake --install build --prefix "$PWD/build/stage"
otool -L build/sovkit-stun
~~~

实际测试日志保留在本机 build/Testing/Temporary/LastTest.log 和
build-asan/Testing/Temporary/LastTest.log；它们和构建产物不纳入源码仓库。
隔离安装只写 build/stage，不更改系统配置或启动服务。

## 尚未验证

Linux 仅完成上述 ARM64 实机 Release / 回环 / 安装式 SDK 验证；尚未跑 Linux ASan，
没有验证 systemd 沙箱、Linux x86_64 或其它发行版。没有生成 Linux 发布包、
安装 STUN 系统服务、改防火墙或访问公网 STUN。代码发布与验证是分开的操作，
GitHub Release 不会把这些未验收项变成已验收。
本轮没有启动 Docker/Colima 虚拟机。macOS 预览包未公证，最低 macOS 13 编译目标没有旧系统实测。

尚未做全局多来源压测、来源表/凭据表容量压测、长期运行、跨网络实测，
也未验证 Windows/iOS/Android。不能把现有主工程的历史结果移作本仓库本轮验收结果。
代码公开由仓库所有者授权，源码范围与许可边界见 PROVENANCE.md。
