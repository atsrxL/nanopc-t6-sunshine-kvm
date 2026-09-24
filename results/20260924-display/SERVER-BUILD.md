# Serverinfo display metadata — ARM64 构建验证

2026-09-24，父代理冻结源码后执行。仅新增构建验证脚本与结果；未修改业务/client，未访问或部署 T6，未操作 VM200/Windows clone。

## 产物

- 本地授权交付：`private/results/local/display-follow/rkmoon-kvm`
- VM301 持久产物：`/root/rkmoon-artifacts/20260924-display/rkmoon-kvm`
- 大小：126974152 bytes；ELF AArch64，含调试信息。
- SHA256：`47cd6a1f5e0732d9a1cc7c61df757ba6da11285e0e0cb7acab2afadbdebdf150`
- assets 与许可证保存在远端同目录；部署由父代理负责。

## 构建与验证

MS-A2 事先在线，VM301 事先停止，本任务启动后证明 QGA 命令完成。所有编译/测试在 VM301 x86_64 + ARM64 Docker/QEMU 执行，不是 T6 原生测试。复用既有 ARM64 镜像和 build/1440p90 依赖缓存，保持容器 `/work` 路径，无依赖下载。

固定 Sunshine `63d35f702ee9e362e43263742981836ec0710384`。更新前逐字节校验14个上游修改文件等于前版生成结果，并核对自有 payload 文件集合/内容及未知未跟踪文件。全部通过后才在远端任务副本加入 display header 与两处注入。tracked overlay SHA256：`0d10dec74ae945dba55662f512eb50e4ddbfea8142b0e778c77bcedd2f595c5c`。

| 检查 | 状态 | 证据/限制 |
|---|---|---|
| Sunshine ARM64 全部目标编译链接 | 通过 | `cmake --build build/1440p90/sunshine --target sunshine -j 8`，exit0 |
| 原生测试 | 通过 | 31/31，含 Linux `display_metadata` |
| Python/联合/overlay/认证测试 | 通过 | 94项，92通过，2项原内核fixture条件跳过 |
| 生产binary HTTP metadata | 通过 | 非root，合成probe，无设备/媒体；Version=1、Status=unavailable、Width/Height/FpsX100=0，字段精确一次 |
| 密码及运行回归 | 通过 | 正确密码serverinfo/applist成功；缺失/错误密码拒绝；pair关闭；重启身份稳定；仅47989/48010；无PEM；root/相对配置/缺资产拒绝 |
| 进程释放 | 通过 | 合成server退出63ms、exit0，私有状态清理 |
| ARM64动态依赖 | 通过 | 容器ldd无缺失；不能代替T6环境核验 |
| 新版T6真实模式/媒体/client自适应 | 未测试 | 父代理负责；父代理另报O_RDONLY+QUERY_DV_TIMINGS成功，不冒充本次实测 |

首次HTTP测试因测试输出目录属主错误，尚未启动server就失败；保留 `http-attempt1-permission.log`，修正任务测试目录属主后完整重跑通过。采集ELF信息时镜像缺少file命令，改用现有readelf，无新增系统依赖。

完整日志和冻结源码hash：远端 `/root/rkmoon-artifacts/20260924-display/`；本地脱敏副本 `private/results/local/display-follow/{build,offline,overlay,http-check,root-refusal}.log` 及 `source-sha256.txt`、`elf-header.txt`、`ldd.txt`。不以模拟probe或QEMU结果声称HDMI/USB/客户端验收。

任务创建的远端构建副本、导入镜像、传输包及临时HTTP服务已清理；既有本地build/1440p90及镜像保留。产物/日志持久保留。本地仅按父代理明确授权交付binary和小型日志，无新本地构建/下载依赖。VM301安排2026-09-24 11:58:18 CST优雅关机，MS-A2保持在线；宿主取消命令：`systemctl stop rkmoon-display-vm301-shutdown-20260924.timer`。
