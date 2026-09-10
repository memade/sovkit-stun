# 首轮导出清单

源工程：SovKit，基线提交 c4cb820f8a1aec22a374bd60ce50abc065bd1be5。
新仓库基于已有初始化提交 2c87ee09127643a53b3f6c2c369a3f3a3a832c5b，
没有复制源仓库 .git，没有合并或推送源仓库历史。

## 白名单

| 源工程路径 | 本仓库路径 | 处理 |
| --- | --- | --- |
| 3rdparty/libnet/src/libnet_stun_server.cc | src/libnet_stun_server.cc | 原字节保留，含默认关闭的诊断扩展 |
| 3rdparty/libnet/include/libnet_stun_server.h | include/libnet_stun_server.h | 原字节保留 |
| 3rdparty/libnet/include/libnet_uv.h | include/libnet_uv.h | 仅 AddressFamily 与 Endpoint 声明，精简 include |
| 3rdparty/libnet/src/libnet_uv.cc | src/endpoint.cc | 仅 Endpoint 方法体，保留方法实现 |
| 3rdparty/libnet/LICENSE | LICENSE | 原字节保留 MIT 许可与 memade 版权声明 |

其余 CMake、命令行宿主、配置、部署模板、测试和本文档均为本次独立项目新增。
原初始化 README/.gitignore 在现有内容上补充，未重建仓库。

没有导出：主工程 SDK、客户端、sovkitd 控制协议、身份与加密存储、消息/文件业务、
网络共享实现、私密部署记录、日志、实际用户数据、证书/密钥和构建缓存。
不直接复制现有运维文档，避免混入真实地址或服务器内部路径。

## 校验与后续同步

精确 SHA-256 及提取方式见 [export-manifest.json](export-manifest.json)。
校验只读，不自动写入、导出、提交或推送：

~~~sh
# 独立仓库即可验证导入内容；无需主工程
python3 tools/verify_import.py

# 维护者可额外核对本机源工程及 Endpoint 提取结果
python3 tools/verify_import.py --source-root /path/to/SovKit
~~~

这不是一个已经改造完成的共享依赖架构。当前主工程仍以内置 libnet 为准；
公开拆分先固定一个可核对快照，避免改动原产品。后续协议修复必须明确同步到两处，
并经过差异审阅、更新基线和哈希、重跑两端测试；校验失败不应靠直接改哈希掩盖。
尚未将公开仓库作为主工程子模块/下载依赖，也没有建立自动反向同步。

## 许可边界与发布门槛

导出部分原本位于带 MIT LICENSE 的 libnet 子目录，因此保留该许可，不将整个主工程
根目录的许可套用到本导出，不迁入其它第三方组件。新增独立项目代码也使用 MIT。
libuv 是外部构建依赖，其二进制再分发许可必须按实际使用版本随包附带，见 [NOTICE](../NOTICE)。

本清单证明复制范围和文件一致性，不能单凭版权注释证明完整代码权属。
仓库所有者负责确认这些代码可独立开源、未受前雇主协议或第三方限制；发布授权不能代替法律权属证明。
本轮未引用 BroSDK 的代码、网页或资源。

导出及打包工具不会自动提交、推送、设置公开可见性、发布 Release 或更新官网。
发布由维护者在授权后单独执行，公开范围只限本仓库白名单，不含主工程历史。
