# 开发预览与发布门禁

本仓库独立承载 STUN 源码，不是 Nearvia 客户端或私有 SovKit 全量源码的发布镜像。
当前版本线 0.1.0 仅提供开发预览；打包命令只产生本地 development-preview，不能自动升级为 stable。
首个预览标签为 v0.1.0-preview.1；公开状态与附件以 GitHub Releases 为准。
manifest 的 published: false 记录归档生成时尚未上传，不是实时发布状态；
人工审核后可将原字节附件发布为 GitHub prerelease，不改成稳定版、不修改已上传附件。

## 本地源码包

~~~sh
python3 tools/package_preview.py --source-only
~~~

只复制 packaging/source-files.txt 白名单中的普通文件；拒绝目录穿越、符号链接、
重复/缺失条目及明显凭据内容。该检查不能代替人工权属/保密审阅。
不会带入 .git、未列出的文件、构建缓存、部署记录或完整主工程。
先核验 5 项原始导入，再生成 manifest.json 与 SHA256SUMS。
构建/测试后再次要求源码文件集合与摘要完全不变；新增缓存或源码变动即拒绝生成最终源码包。
失败时仅保留工作目录作诊断，不能把其中的部分产物作为通过验收的候选。

归档固定条目顺序、权限、时间戳、uid/gid 及 gzip 头；相同源码内容生成相同源码归档。
这不保证跨工具链编译得到字节相同的二进制。
每次输出到新建 .build/packages/preview-<随机后缀>/，不覆盖旧包。

## macOS 本机程序 + 静态库 SDK 预览

~~~sh
python3 tools/package_preview.py \
  --libuv-prefix /path/to/public-libuv-prefix \
  --libuv-license packaging/licenses/libuv-1.52.1.txt
~~~

从同一白名单快照重新编译 Release、运行完整 CTest，再安装到隔离 stage。
不复用未知来源的旧二进制，不跳过测试；需要静态 libuv，检测到非系统 dylib 依赖则拒绝打包。
当前二进制打包只接受已审阅许可集的 libuv 1.52.1；普通源码构建仍支持 libuv ≥ 1.44。
许可集保留 LICENSE、LICENSE-extra、inet.c 的 ISC、tree.h 的 BSD 及 heap-inl.h/queue.h 的 ISC 原始声明；
只传主许可证或其它版本会拒绝打包。更新依赖须重新核对该版本的完整声明，而非只改版本号。
同时生成对应源码包，两个包以 source_inventory_sha256 关联，不用旧 Git 提交冒充当前源码。

| 内容 | 位置 |
| --- | --- |
| 命令行程序 | bin/sovkit-stun |
| 静态库 / 公共头文件 | lib/libsovkit-stun.a、include/sovkit-stun/ |
| CMake 接入配置 | lib/cmake/SovkitStun/ |
| 独立 SDK 示例 / 服务配置示例 | share/sovkit-stun/examples/ |
| 本项目与依赖许可 | share/sovkit-stun/ 及其 licenses/ |
| 版本、平台、源码摘要、依赖与验证边界 | manifest.json |
| 所有包内文件的摘要（不含摘要文件自身） | SHA256SUMS |

程序内静态链接 libuv；SDK 静态库没有把 libuv 合并进去，消费方仍需公共 libuv ≥ 1.44。
SDK 也不保证跨编译器/标准库的稳定 C++ ABI，必须匹配目标平台与架构。
最低 macOS 编译目标设为 13.0，不等于已在 macOS 13 实测；尚未进行 Developer ID 公证。
当前脚本不生成 Linux/Windows/iOS/Android 二进制，不将其它目标的结果冒作对应平台验收。

## 校验与接入

在包目录运行：

~~~sh
shasum -a 256 -c SHA256SUMS
cmake -S share/sovkit-stun/examples/sdk -B example-build \
  -DCMAKE_PREFIX_PATH="/path/to/unpacked-package;/path/to/public-libuv-prefix"
cmake --build example-build
./example-build/stun_host
~~~

示例不绑定网络端口、不读写数据，只核对 codec 与静态库生命周期。
真实 UDP 和启停行为由随源码附带的测试验证。完整日志保留在打包工作目录，不附入公共归档。
安装不会写 /etc、启用服务或改防火墙。

## 发布前逐项确认

1. 所有者授权公开并负责代码权属，核对 MIT 与依赖许可范围；打包工具不自动推送。
2. 确认候选源码与两个包的 source_inventory_sha256 一致，检查 SHA256SUMS 与归档清单。
3. 阅读 VALIDATION.md，逐项区分本机编译、安装式 SDK、Linux 实机、跨网络和长期运行。
4. 未验收平台不放二进制下载；模拟器、交叉编译、限时回环测试不替代对应实机验证。
5. 审核 README、CHANGELOG 与 Release 文案的版本、限制、签名和依赖说明。
6. 经明确批准后，才提交源码、创建标签并发布 GitHub Release；本脚本没有这些操作。

官网的 STUN 入口应链接本仓库和已审核的 Release，不链接一个尚不存在的安装包。
Nearvia 客户端与 SovKit SDK 的发行位置、签名和开源范围独立处理，不能把私有主工程推入这里。
