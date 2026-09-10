# Linux 手动部署模板

先在目标 Linux 环境构建并通过测试，再考虑安装。以下命令供管理员审核后手动执行，
不是构建过程的一部分。不要覆盖正在运行的 SovKit 节点服务或其配置。

## 安装程序

默认前缀 /usr/local：

~~~sh
sudo cmake --install build
~~~

示例会放入 /usr/local/share/sovkit-stun/examples，不会自动复制到系统配置目录。
同时安装静态库、头文件和 lib/cmake/SovkitStun 下的接入配置；SDK 消费方另需公共 libuv。
如设置了其它安装前缀，必须相应修改服务模板中的 ExecStart。

## 配置

仅在 /etc/sovkit-stun/server.conf 不存在时，新建目录并复制 packaging/server.conf。
文件使用 root 所有、0644 权限；没有密码或展开语法。不要覆盖已有配置。

| 键 | 默认 | 允许值 |
| --- | --- | --- |
| bind_address | 127.0.0.1 | 数值 IPv4；0.0.0.0 需显式配置 |
| port_a | 3478 | 1024..65535 |
| port_b | 3479 | 1024..65535，不能与 A 相同 |
| duration_seconds | 0 | 0..86400，0 表示等待停止 |
| filtering_diagnostics | false | true / false |

UTF-8/ASCII key=value 文本，支持空行和整行 # 注释；不支持行内注释、
变量替换、include、引号或热重载。最多 16 KiB。未知/重复键、空值和异常数字均拒绝启动。
修改配置后需要重启服务。

该进程没有持久应用状态，不需要创建客户端目录、身份文件、SQLite、缓存或专用日志目录。
/etc/sovkit-stun 仅属于这个独立服务，既不替换也不迁移已有 com.skstu.sovkit 目录。
日志写 journal；轮转/落盘/保留时长遵循主机 journald 策略，不能由应用声称“永不落盘”。

## systemd

模板使用 DynamicUser，不申请特权端口能力，不允许写系统或用户目录。
先确认没有同名 sovkit-stun.service，也没有其它服务占用两个 UDP 端口；
如果已有主工程 sovkitd 的 STUN 单元，保持其文件不动，另选单元名和不冲突端口。

审核配置后，将模板安装为 /etc/systemd/system/sovkit-stun.service，再手动执行：

~~~sh
sudo systemctl daemon-reload
sudo systemctl start sovkit-stun.service
systemctl status sovkit-stun.service
journalctl -u sovkit-stun.service -n 20 --no-pager
~~~

不默认开机自启。先用自有客户端和明确授权的网络验证，再决定是否 enable。
开放公网还需另行配置云安全组/主机策略；此文不执行、不提供自动放行脚本。
停止并保留安装文件：sudo systemctl stop sovkit-stun.service。

## 二进制发布检查

1. 在对应 Linux 架构构建，运行 codec/UDP/配置/退出测试；记录编译器和 libuv 版本。
2. 检查 ldd 依赖、最低系统版本；静态链接 libuv 不代表 libc 等依赖也已静态链接。
3. 附程序版本、SHA-256、LICENSE、NOTICE 及所链接 libuv 版本的完整第三方许可。
4. 在隔离安装目录检查内容；不要把构建缓存、私钥、主工程 SDK 或运行日志打入归档。
5. 代码权属及发布范围经仓库所有者确认后，才提交发布标签/Release。

当前仓库不含自动上传、自动打标签、自动部署或获取发布凭据的工作流。
