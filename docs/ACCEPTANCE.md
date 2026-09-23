# 验收矩阵

> 实机更新：ARM64 worker/Sunshine 已构建；T6 真实 1080p60 HEVC/H264 各 60 秒及独立解码通过；Moonlight 已配对。客户端硬解长测和 USB 尚未完成。见 [T6 验收记录](../results/20260923-t6/STATUS.md)。

> 2026-09-23 本地更新：完整 Sunshine 与真实 MPP worker 已在 Linux amd64 编译链接通过；25 原生、69 Python/联合、25 sanitizer 测试通过。目标 ARM64/P1/P2/P3 仍未测试。最新结果见 [构建记录](../results/20260923-build/STATUS.md)。下文原交接记录保留供追溯。

状态必须为：通过 / 失败 / 未测试 / 部分完成。每次结果绑定具体硬件、commit、二进制/动态库、客户端和证据目录。原记录不是新验收。

## 本次云端

| 检查 | 状态 | 证据 / 限定 |
|---|---|---|
| 架构入口、固定版本、许可清单 | 部分完成 | ADR/SOURCES，源码可读；未全量安全审计或全链路验证 |
| 自有核心+V4L2 probe构建 | 通过 | results/cloud/configure.log、build.log；x86_64 Linux |
| worker CLI独立编译 | 通过 | results/cloud/worker-cli-compile.log；未链接encoder_mpp.cpp |
| 原生25项 | 通过 | results/cloud/ctest.log |
| Python+原生输入联合+工具68项 | 通过 | results/cloud/python-tests.log；fake USB backend |
| ASan/UBSan原生25项 | 通过 | results/cloud/asan-tests.log |
| 完整Sunshine套补丁/编译链接 | 未测试 | 容器无法解析GitHub，仅连接器可读；不是已成功的patch |
| 真实SDK的encoder_mpp.cpp编译链接 | 未测试 | 容器无MPP SDK/板卡 |
| T6只读基线 | 未测试 | 用户尚未提供当前设备结果 |
| 60秒真实HEVC/Main8/H264编码与独立解码 | 未测试 | 不采用旧H264结果填表 |
| 1080p60原版Moonlight配对/硬解/10分钟 | 未测试 | 客户端资料与设备均未取得 |
| 实际USB相对输入与断连释放 | 未测试 | 旧记录绝对模式是P3阻碍，不属于新已修复结果 |
| 物理端到端/高分辨率/长稳 | 未测试 | 没有测量数据 |

## 本地待填环境

- 板型/内存/镜像/内核/散热：未确认。
- HDMI节点/源尺寸cadence/plane/stride/sizeimage/color range/transfer：未确认。
- MPP头版本/运行动态库路径与SHA256：未确认。
- 新Sunshine binary SHA256/子模块版本/补丁hash：未确认。
- kvmd版本/socket鉴权/mouse.absolute/既有gadget所有权：未确认。
- Moonlight系统/version/GPU/decoder/屏幕刷新率/网络：未确认。
- 采集与输入独占授权范围/窗口/旧服务恢复方法：未确认。

## 本地阶段记录模板

| 时间/阶段 | 环境ID | 检查 | 状态 | 证据路径/hash | 未完成/回滚 |
|---|---|---|---|---|---|
| 待执行 | 待填 | P0-build | 未测试 | — | — |
| 待执行 | 待填 | H0/P1-HEVC/P1-H264 | 未测试 | — | — |
| 待执行 | 待填 | P2-video-only | 未测试 | — | — |
| 待执行 | 待填 | P3-USB | 未测试 | — | — |

一次fail不能改成“部分通过”掩盖致命问题。软件解码是独立验证工具，不证明客户端硬解；MPP黑帧成功不证明HDMI；client连接成功不证明尺寸、编码或输入正确。
