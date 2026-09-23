# 未完成事项（不得从验收表隐藏）

> 2026-09-23 本地更新：完整 Sunshine 与真实 MPP worker 已在 Linux amd64 编译链接通过；25 原生、69 Python/联合、25 sanitizer 测试通过。目标 ARM64/P1/P2/P3 仍未测试。最新结果见 [构建记录](../results/20260923-build/STATUS.md)。下文原交接记录保留供追溯。

## 阻止宣称可部署的门

- 完整固定Sunshine上应用补丁、真实依赖编译链接；云端只有小型锚点fixture测试。
- encoder_mpp.cpp对固定MPP SDK的编译/链接/硬件运行。须核对profile/level/VUI、非阻塞packet返回、PTS/EOI与DMA导入生命周期。
- 当前设备/客户端基线、采集接管授权、真实1080p60的60秒双codec码流与独立解码、原版Moonlight10分钟闭环。
- 现有相对USB HID是否存在。旧记录为绝对模式；本包不会自动修gadget。

## 本地代码/协议重点复核

- 上游无桌面启动仍有图形库/安全初始化；不假设启用专用capture就能删掉所有依赖。
- Moonlight encoderCscMode 与源range/transfer、编码VUI、客户端渲染的相互作用必须验证。当前encoder颜色从实际V4L2信号派生；不能只验证服务端字段而跳过客户端灰阶/色彩检查。
- 上游发送线程/UDP缓冲不属于本包新增两帧队列。需受控拥塞下验证frame age、FEC、IDR请求和停止后的旧包/输入任务清理。
- kvmd逐事件HTTP路径的吞吐和最后一公里USB送达未测试；未知键、键盘布局/扩展flags仍有限制。
- MPP低延迟设置是否实际禁用重排需要完整解码和时序证据，而不是相信配置键成功。

## 明确后续范围

自动动态码率尚未接Sunshine控制逻辑；worker只有显式降码率原语。没有RGA、NV16/NV24/多独立plane、绝对鼠标、音频采集/静音Opus、HDR/AV1/手柄或高分辨率实机验收。P4/P5的物理延迟/长稳只提供设计与采样流程，不提供虚构结果。

所有问题依次在CODEX_START.md的阶段门解决；遇需要系统包/权限/EDID/USB变更，先取得对应授权，不用桌面捕获或软件编码伪装通过。
