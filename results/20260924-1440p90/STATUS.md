# 2560×1440 90Hz 前置检查

> 后续最新：重读sshh后T6登录恢复，HEVC/H264各60s真实1440p90 DMA-BUF编码及独立解码通过，89.998/89.982fps，0/1raw skip。见[编码证据](ENCODE.md)。原EDID/1080p60和原服务已恢复，Windows新包继续构建，端到端90fps仍未测。下文保留先前阶段。

> 最新结论：NVIDIA NVAPI自定义1440p90试用成功，T6锁定2560×1440、89.9935Hz；完整V4L2采集30秒通过（2695帧、timestamp89.9981fps、零缺失/错误）。server实验开关与正确编码等级、client实验预设已实现；真实MPP90fps编码和客户端串流仍未测试，默认保持60Hz。T6/MS-A2 SSH认证阻碍后续硬件测试和Windows构建。见 [采集与阻碍](CAPTURE.md)；下文保留历史失败及修复证据。

2026-09-24，用户授权验证T6能否接收2K90，并在可行后完成server/client支持。仅访问授权T6，未访问或控制HDMI源电脑。

| 项目 | 状态 | 实际证据/限制 |
|---|---|---|
| SSH只读访问 | 通过 | NanoPC-T6 LTS，6.1.115-vendor-rk35xx，at用户 |
| 接收端驱动能力查询 | 通过 | VIDIOC_DV_TIMINGS_CAP：640..4096 × 480..2160，像素时钟20..600MHz，standards=1，capabilities=3；范围不等于每个时序实测支持 |
| 现有EDID读取 | 通过 | 2×128字节，各块校验和0；详细时序1080p60、1440p59.9506、1600p59.9716；无1440p90详细时序 |
| 当前HDMI信号 | 失败 | 现有只读probe返回 QUERY_DV_TIMINGS: No locks available；无法取得锁定时序 |
| 真实1440p90采集/MPP/客户端 | 未测试 | 等待用户确认当前HDMI源并恢复输出；不将范围能力视为通过 |

EDID SHA256：629fd2a4bb8644ef4639d8f373cde28032c97d977826cff63353c0561d7e286c。只读ioctl和原始结果保留private/results/local/1440p90。未修改EDID、内核、显示模式、USB、系统包或服务；未执行STREAMON。

发现rkmoon-minimal-live.service为active，但本次检查无worker占用采集节点；原服务保持运行。此前查询的旧t6-kvmd/t6-kvmd-vnc/t6-kvm-panel为inactive。不得将这组旧服务状态误称为所有KVM实例已停止。

当前正式代码仍只允许约60Hz，未放宽验收门。下一步为获得真实HDMI源、核对源可选时序；若需添加EDID自定义90Hz时序，先准备精确变更及回滚并确认授权，再按P0/P1/P2推进。

## HDMI源接管前检查

用户指定192.168.123.123（私有记录nuc9/Windows），授权必要调整。SSH22可达，但返回主机密钥与历史known_hosts不一致，已暂停认证并请求确认；AMT常用16992/16993连接超时，私有记录未提供独立AMT端点。未绕过SSH验证、未改主机密钥记录、未重启源或修改显示模式。

## NUC9身份确认与恢复尝试

用户确认新SSH指纹后，采用任务独立known_hosts完成登录（主机NUC9、at，高完整性管理员会话）。查询Intel UHD630驱动31.0.101.2141，当前1024×768@60，控制台at session1。仅Generic Non-PnP Monitor为present；注册表有RKP3588历史记录，但不能当作当前连接。执行pnputil扫描和控制台DisplaySwitch /extend；临时计划任务LastTaskResult=0，已删除。T6随后仍QUERY_DV_TIMINGS无锁。请求用户核对物理HDMI-IN连接及转接方式。没有重启、改EDID/驱动或开启视频流；未放开正式90Hz支持。

## 用户授权重启源设备后的复查

用户说明NUC9重启可能更换SSH密钥，并持续授权信任该指定设备的重启后密钥。执行NUC9 shutdown.exe /r /t 10成功，观察SSH暂时离线后恢复，查询LastBootUpTime为2026-09-24 03:09:26.5 CST。本次ED25519指纹未变。恢复后仍Intel UHD630、1024×768@60、Generic Monitor present；T6 QUERY_DV_TIMINGS仍无锁。rkmoon-minimal-live保持active。VIDIOC_ENUMINPUT返回hdmirx/status0/capabilities2，该状态不能取代实际时序锁定。没有重复重启、开启争用采集或修改EDID。下一步仍需核对HDMI物理连接/源输出端口。

## T6重启验证

用户明确要求重启T6。systemctl reboot成功，boot_id由537e3977-cdc1-4365-92b4-8487b7d47ab6变为15e06209-ca73-4430-bd23-f879afbc3aff。重启后HDMI仍无锁。恢复重启前服务状态：rkmoon-input与rkmoon-minimal-live active；旧t6-kvmd/t6-kvmd-vnc/t6-kvm-panel inactive（面板重启自动启动后已停止恢复）。临时live unit重建时补回独立LD_LIBRARY_PATH，恢复at对mpp_service/system-uncached的临时rw ACL，保留既有t6-kvm ACL。变更前ACL保存/root/agent.backup/rkmoon-20260924-reboot-acl.txt。普通用户H264/HEVC合成MPP probe再次通过codec_mask3。未部署新HTTP版、未修改EDID或启用真实采集。重启未解决HDMI信号阻碍。

## 输入信号恢复

用户要求复查后，T6只读probe成功：1920×1080，59.9968Hz，BGR24单plane，stride5760，sizeimage6220800；rkmoon-minimal-live/rkmoon-input均active。与此同时指定源NUC9的CIM仍返回1024×768@60/Generic Monitor，WMI显示模式查询不支持；两端报告不一致，需确认当前接线源或用控制台DisplayConfig精确核对，不能将该1080p60信号认定为已验证NUC9的1440p90输出。

## 当前源更正为Znas直通虚拟机

用户明确当前输入来自Znas虚拟机。宿主192.168.123.10只读检查：Tiny11_clone运行、PCI 01:00.0 RTX5060Ti直通，Guest Agent ping通过。guest-exec确认Znas-vm、RTX5060Ti输出1920×1080@60，活动显示器RK-UHD/RKP3588，与T6输入一致。不再操作NUC9。WmiMonitorListedSupportedSourceModes报告1440p约59.95Hz/241.5MHz，没有1440p90。存在SudoMaker虚拟显示适配器但本次验证目标是物理RTX→T6。下一步须测试自定义90Hz时序，必要时有界变更EDID并保留回滚。

## 授权临时EDID试验及回滚

用户“开始做吧”授权后，原256字节EDID备份到T6 `/root/agent.backup/rkmoon-1440p90-edid-original.bin`。将第二详细时序改为2560×1440、总时序2720×1481、362.55MHz（90.000298Hz），垂直上限改90Hz，保持1080p60及CTA其他模式；逐块重算校验并读回一致。第一试验SHA256 `7eebdfcf5e2887e8f2292ca55739b56c0bd2cca4cd9d8568fe314fd3d9b8dcd6`。WMI读取到该时序，但交互式用户会话的EnumDisplaySettingsEx未枚举1440p90。

原HDMI VSDB最大TMDS为340MHz，小于试验像素时钟；第二次临时提高至370MHz并重算CTA校验，读回SHA256 `9f98dbadc7d4cf75cee484d560db588b7264b012a26e8bb0515d403d2ec18d82`。此声明仅为受控试验，不证明硬件吞吐。Windows仍未枚举该模式；用当前DEVMODE明确指定2560×1440、32bpp、90Hz并执行CDS_TEST，返回DISP_CHANGE_BADMODE(-2)，没有调用实际切换。不能据此判断T6硬件必然不支持90Hz，也不能声称TMDS字段是唯一原因。

停止本阶段推进并恢复原EDID，读回SHA256 `629fd2a4bb8644ef4639d8f373cde28032c97d977826cff63353c0561d7e286c`、两块校验和0。恢复后T6探测1920×1080、59.9952Hz、BGR24、stride5760、sizeimage6220800，rkmoon-minimal-live/rkmoon-input均active。没有STREAMON、编码或90Hz客户端测试；没有部署HTTP版或修改正式帧率门。Guest临时任务每次运行后删除，最终输出与清理结果保留private/results/local/1440p90/final-mode-result.txt。下一步需独立核查NVIDIA自定义时序接受条件或更合适的HDMI EDID描述；先获得实际90Hz输入，再推进采集/MPP吞吐与代码支持。
