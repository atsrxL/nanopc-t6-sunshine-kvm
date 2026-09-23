# 给本地 Codex 的执行入口

> 实机更新：ARM64 worker/Sunshine 已构建；T6 真实 1080p60 HEVC/H264 各 60 秒及独立解码通过；原版 Moonlight HEVC 硬解超过 11 分钟、H264 重连通过。USB 尚未完成。见 [T6 验收记录](results/20260923-t6/STATUS.md)。

> 2026-09-23 本地更新：完整 Sunshine 与真实 MPP worker 已在 Linux amd64 编译链接通过；25 原生、69 Python/联合、25 sanitizer 测试通过。目标 ARM64/P1/P2/P3 仍未测试。最新结果见 [构建记录](results/20260923-build/STATUS.md)。下文原交接记录保留供追溯。

你接手的是 `rk3588-moonlight-kvm`，**已有真实源码，但并非已通过实机验收**。不要重新从零写计划；先验证并修复本包。

## 第一轮：不用接管设备

在本包目录读 `AGENTS.md`、`HANDOFF.md`、`docs/ADR-001-architecture.md`、`docs/BUILD.md`。

```bash
./tools/build.sh offline
python3 tools/fetch_sources.py
python3 tools/apply_sunshine.py vendor/sunshine \
  --patch-output /tmp/rkmoon-sunshine-review.patch
# 审阅 diff 和下列入口后，只向独立 checkout 应用：
python3 tools/apply_sunshine.py vendor/sunshine --apply
./tools/build.sh mpp
./tools/build.sh sunshine
```

MPP 与 Sunshine 的完整构建在云端 **未完成**。优先解决真实 SDK API、固定 Sunshine 接口、CMake 依赖问题。不要通过 mock 头文件、删除错误检查、root、软件编码、关闭认证“修到成功”。完整编译日志保存 `results/local/<date>-build/`，记录实际二进制 SHA256 和动态库路径。

重点审查：`video::capture()` 在 `chosen_encoder` 解引用前截流；`probe_encoders()` 不回退；`create_runtime()` 不建立本机 uinput；RTSP 独占检查在上游认证/加密检查后、分配输入前；包队列不清除已编码参考帧。

## 第二轮：只读实机基线

用户旧项目：`https://github.com/atsrxL/nanopc-t6-kvm`；旧 Mac 路径仅供参考，关联 ID 不代表访问授权。使用现有获准连接，**不从交付文档提取/传播凭据**。

```bash
python3 tools/probe.py --video /dev/videoN --output /tmp/rkmoon-baseline.json
```

把板型/系统/内核/MPP 动态库、视频节点、实际 native timings/format/stride/plane/range/transfer、温度、占用者、kvmd mouse.absolute 和客户端型号版本/硬解能力填入 `docs/ACCEPTANCE.md`。尚无客户端数据时不要虚构。

确认是否已有用户授权的采集窗口；有授权再进行 P1。不要让两套程序争抢同一个 V4L2 节点。旧记录的 4K60/H264 不证明新 HEVC 路径。

## 第三轮：按阶段实测

先按 `docs/BUILD.md` / `TESTING.md` 跑 60 秒 P1，显式选择像素路径并独立解码。再使用 **input.enabled=false** 的专用 Sunshine 跑原版 Moonlight 配对/HEVC/H264视频闭环。P3 前必须确认相对 HID 已存在、kvmd API 鉴权正确且用户授权独占。

遇到绝对鼠标、EDID 不匹配、CMA 不足、权限不足、需要新工具链/OS 包时，先列出最小变更及回滚。不要自动改旧服务、gadget、内核或系统包。

## 交付回报

更新 `docs/ACCEPTANCE.md` 的状态、环境、证据路径。分别报告源码可编译、真实 HDMI 可编码、原版客户端能硬解、目标电脑真实收到输入，不能合称一个“完成”。提供 git diff、未完成问题、下一步及脱敏产物 SHA256。
