# 构建、硬件前置与固定依赖

> 2026-09-23 本地更新：完整 Sunshine 与真实 MPP worker 已在 Linux amd64 编译链接通过；25 原生、69 Python/联合、25 sanitizer 测试通过。目标 ARM64/P1/P2/P3 仍未测试。最新结果见 [构建记录](../results/20260923-build/STATUS.md)。下文原交接记录保留供追溯。

## 0. 环境

独立组件要求 Linux、CMake≥3.20、C++17、Python≥3.10、pthread、Linux UAPI。专用 Sunshine 固定源码要求 **CMake≥3.24、C++23**，以及其自身图形/FFmpeg/Boost/OpenSSL/Opus/前端等依赖。不要把系统旧编译器一定可用作为假设。[S1/S2]

本次云端：x86_64、GNU g++14.2.0、CMake3.31.6；没有MPP SDK/板卡。SDK源码和Sunshine完整构建未通过。以下是可执行构建流程，不是已在目标板成功的记录。

先只读记录本地 `uname -a`、`g++ --version`、`cmake --version`、`python3 --version`、`ldd --version` 和实际安装包版本。不得自动换发行版/内核/软件源。GCC/Clang/libstdc++缺C++23能力时列出可选的私有构建工具链，不擅自apt升级整机。

## 1. 本包离线构建

```bash
./tools/build.sh offline
# 额外内存检查；没有硬件依赖：
cmake -S . -B build/asan -DRKMOON_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/asan -j2
ctest --test-dir build/asan --output-on-failure
```

默认 `RKMOON_MPP=OFF` **不会生成假的rkmoon-worker**。它只构建核心库、V4L2只读探测程序和测试。不能拿这个结果称“编码器已构建”。

## 2. 下载固定源码

```bash
python3 tools/fetch_sources.py
```

只新建vendor/mpp、vendor/sunshine；遇已有目录拒绝，不reset、不清工作树。Git子模块跟随固定Sunshine gitlink。上游 CMake CPM/package-lock、npm/OS依赖仍需记录，不保证所有网络下载永不变化或逐字节可重现。

## 3. MPP 私有构建与原生worker

```bash
./tools/build.sh mpp
# 二进制默认在 build/native/rkmoon-worker
# 首先审阅实际加载的是私有版本，不替换旧KVM的系统libmpp：
ldd build/native/rkmoon-worker
```

私有prefix为`local/mpp`，不运行ldconfig、不覆盖/usr。`mpp` CMake选项/输出路径和SDK运行内核兼容性要在第一次本地构建核实。发现`find_path`/动态库目录不一致时明确传入`-DMPP_INCLUDE_DIR=... -DMPP_LIBRARY=...`，不要混用不同SDK头和系统库。

MPP的动态库路径若未由build rpath解决，只给本实例设置私有LD_LIBRARY_PATH；不要全局写`/etc/ld.so.conf`。特别检查旧KVM仍加载原来的库。

## 4. 生成并应用固定 Sunshine 模块补丁（最小服务分支）

```bash
python3 tools/apply_sunshine.py vendor/sunshine --patch-output /tmp/rkmoon-sunshine.patch
# 默认不修改。审阅全部diff：
python3 tools/apply_sunshine.py vendor/sunshine --apply
./tools/build.sh sunshine
```

补丁要求固定HEAD、干净工作树、唯一源码锚点。应用一次后重复执行会因脏树拒绝，这是保护，不应绕过。完整上游下载/打补丁/链接在本次云端没有执行成功。若固定版本签名、queue类型、目标名或依赖造成失败，先记录准确错误并修小范围适配/测试，再继续。

最小服务分支产物是 `build/sunshine/rkmoon-kvm`，构建不需要 WebUI/npm，不启动上游桌面入口，不监听 WebUI 管理端口；仍需要固定 Sunshine 子模块、Linux 开发包与原 GameStream 网络协议模块。`libasound2-dev` 是新增的专用音频开发依赖；构建配置关闭桌面/DRM/VAAPI/Vulkan/CUDA 等采集编码路线，不会增加 CPU 视频编码兜底。二进制旁只打包 `assets/apps.json`（自有固定 HDMI/no-command JSON）与 `assets/box.png`（固定 Sunshine 上游图标，随包保留上游 LICENSE/NOTICE），不打包上游包含 Steam/xrandr 命令的 Linux apps.json，也不打包 Web 树。main 要求绝对配置路径和用户自有0700 HOME/XDG_CONFIG_HOME，通过 `/proc/self/exe` 定位并核验两个资产后切cwd到binary目录；缺资产/目录权限/路径错误拒绝启动。私有state和日志继续由绝对XDG路径解析，overlay阻止已安装Sunshine的自动state迁移。不运行上游 `cmake --install`；本包从独立 build 目录运行，运行前 `tools/run.py` 核验专用 marker。实际编译/运行证据以 `docs/ACCEPTANCE.md` 单列为准。

## 5. 设备权限

专用Sunshine不以root或filecap运行。根据实际设备，仅授予运行用户需要的video/render/MPP/dma_heap访问权限和kvmd私有socket访问；组名不是通用事实。`/dev/hidg*`不由本包直接打开；既有kvmd保留gadget所有权。权限变更单列授权/回滚，不运行“chmod 666全部设备”。

## 6. P1与P2先决条件

拿到独占采集窗口后，先确认HDMI真实1080p60、单planeNV12/BGR24、source color明晰。首次只读probe不启动stream；`--probe`真实MPP能力测试会调用编码硬件但不读HDMI，也需要硬件访问授权。

```bash
build/native/rkmoon-worker --probe
# 授权后真实HDMI，明确允许像素拷贝（不是软件编码）：
python3 tools/p1_capture.py --worker build/native/rkmoon-worker \
  --device /dev/videoN --codec hevc --seconds 60 --result results/local/p1-hevc \
  --allow-copy --ack-capture-ownership
# 核对源range/transfer后填写；下面以BT709 limited为示例，而不是对实机的断言。
python3 tools/validate_capture.py results/local/p1-hevc/capture.hevc \
  results/local/p1-hevc/frames.csv --codec hevc \
  --expected-range tv --expected-transfer bt709 \
  --output results/local/p1-hevc/validation.json
```

随后以新结果目录测试H264。独立解码可在PC进行；解码使用CPU不等于生产视频编码走CPU。不能把 `worker --probe` 的合成黑帧统计当成真实 HDMI验收。
