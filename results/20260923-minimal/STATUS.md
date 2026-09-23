# 最小服务端 P0 构建交付 — 2026-09-23

## 已交父会话的冻结产物

- 实际编译源码：`912613c8edd804cb5fdbc96d5445712c7fd2dab1`。
- Sunshine：`63d35f702ee9e362e43263742981836ec0710384`；overlay SHA256：`86316b5e8e20317a0aabc33a28d2751654d7d7ad973b00497e2c8b03b766af58`。
- `rkmoon-kvm` SHA256：`8ff1e6ff20beaeb9c7726e42482f4ce3a5206a764ea9b88607c15e8ff96f1cb8`。
- VM301 `/root/rkmoon-artifacts/20260923-minimal/runtime.tgz`：79,698,084 bytes；SHA256 `2a42c0968633fee31a3b2e879131e42621eab61fb4ce9df57f7dc5e7b75effd3`。
- 包根：`rkmoon-kvm`、`assets/{apps.json,box.png}`、`lib/`（70个私有动态库）、`tools/`、`python/rkmoon_hid/`、`config/example.json`、`licenses/`、`manifest.json`。**host-only**，使用原实测独立 MPP worker及其私有 MPP 库；不含密钥、证书、真实媒体或 Web 树。
- `assets/apps.json` 为自有固定HDMI/no-command默认值，SHA256 `990fde05f1e16b1ded8b3d8b121c86a9524ce32a76bfac511a9bb3b4f27c1042`。
- `assets/box.png` 来自固定Sunshine `src_assets/common/assets/box.png`，SHA256 `d9164ebd069b5f735eb8efc557801778498da37f572ef70e3d35604739e6c613`；保留上游LICENSE/NOTICE。
- 源码及文件清单见 [runtime-manifest.json](runtime-manifest.json)。`3da8050` 的无Web启动文案修正**未编入此冻结binary**；该版本的startup测试脚本用于验收912613c，不更换binary来源。

环境：MS-A2 VM301 Debian **x86_64 guest**，`debian:trixie-slim` arm64 Docker/QEMU，GCC14。不是T6原生构建/测试。所有构建日志与大型产物在上述远端artifact目录；未改T6、Windows、旧KVM或USB模式。

## 结果

| 检查 | 状态 | 证据/限制 |
|---|---|---|
| ARM64 QEMU完整配置、编译、链接 | 通过 | `build.log`有`BUILD_OK`、`build.exit=0`，ELF AArch64；最终源码912613c |
| ARM64离线核心/联合 | 通过 | 912613c：25原生+87 Python/工具/联合，`offline-final.log` |
| 独立admin Unix socket fixture | 通过 | `admin-fixture.log`，分包、短块读取、多行/512-byte上限、取消；不是实际配对 |
| HTTP/RTSP/control/ping敏感日志fixture | 通过 | 合成C++执行修改后的HTTP/RTSP完整日志函数、control/ping日志语句；不含真实密钥 |
| 解密输入日志入口guard | 通过 | 合成sink验证真实patched入口提前返回；不是整个输入解密/parser回归 |
| ALSA null +真实Opus | 通过 | `audio-fixture-final.log`：5/10/20ms各25包，6000/12000/24000帧读取与重开；不是硬件 |
| 实际生产audio_capture loop，合成注入 | 通过 | 见下节，真实对象+ALSA null，注入stale/xrun；不能替代HDMI设备验证 |
| 非root、不同cwd、绝对配置/缺资产拒绝 | 通过 | `startup-smoke.log`；独立容器、probe-only模拟worker，没有真实硬件 |
| 无Web启动监听 | 通过 | **启动阶段**实际进程socket仅TCP47984/47989/48010，没有47990；没有建立音视频会话，不宣称UDP验收 |
| 同UID admin LIST碎片、资源退出 | 通过 | 实际Unix socket；SIGTERM 216ms、exit0、socket清除；模拟state/keys已删除 |
| root拒绝 | 通过 | 隔离容器uid0启动exit1，未开设备；`root-refusal.log` |
| 真实新binary HDMI视频、HDMI声音与音画同步 | 未测试 | 父会话负责后续联调；旧main视频结果不迁移 |
| 新客户端真实USB/异常释放/长稳 | 未测试 | 没有本分支的新设备测试 |

## 生产音频对象测试的准确边界

测试代码commit `f01fde9`，链接已构建的b76e93a生产audio_capture对象，不改生产路径；ALSA capture使用`null`。链接器 `--wrap=snd_pcm_avail_update`：第6次返回48000模拟过时ring、第12次返回-EPIPE模拟xrun，其余返回0（null没有真实硬件时钟）；readi/Opus编解码使用真实库。另以不存在的硬件名称验证首次无源静音。

- 5/10/20ms正常模式在约2.9s内正确解码570/291/146包；每种均注入一次stale、一次xrun、两次open恢复。
- 首次无源、高质量512kbps在约450ms内91/46/24包静音；停止0–2ms。标准源使用96kbps。
- 断言：包可由真实Opus解码为准确帧数；数量处于宽松的QEMU节拍界限；注入状态处理后恢复采样调用；shutdown<1s。没有跨机时钟/口型同步、真实设备xrun/ALSA权限/首次缺源后物理插入的证明。
- 证据：`audio-loop-build.log`、`audio-loop.exit=0`、`audio-loop/object-source-sha256.txt`。
- 测试对象与最终912613c对象hash**相同**：`064f9677d48d2ead329649d015cdafa00785269a9904c40139702525b6e53a3f`。
- audio.cpp SHA256：`3d063f8bd64e9631592648ba60c6510172dc05a2fc31a6be77a6ff7f61357ab5`；audio_pcm.hpp：`fec84028bad979cd863ce2660c88c5cfb4c35e0c82c1322f470bc04f7f0e0ebb`。

## 构建失败与处理

完整记录保留远端`build-attempt*.log/exit`：GLAD缺jinja2（仅容器补依赖）；关闭所有桌面backend触发上游CMake门（仅专用RKMOON_MINIMAL_BUILD允许外部HDMI）；ALSA target_link签名与上游plain不符（改同签名）；int error遮蔽Sunshine logger（改opus_status）；从-j2到-j4前正常SIGINT并确认旧编译子进程退出。未跳过认证、错误检查或加入软件视频编码。

资产审计发现原绝对构建asset路径错误，冻结前修复为相邻`assets/`、绝对config与私有0700 HOME/XDG必需、禁止旧Sunshine配置迁移；核对`path_f()`会把relative state/credentials/log/cert路径归到绝对appdata，不写binary目录。上游构建暂存目录仍会拷贝额外静态资源，**打包使用显式两文件白名单**，最终包不含web/shaders/Desktop/Steam图标或命令JSON。

## 最小性与待部署范围

已删除Web管理server/npm目标、上游main/UPnP target，专用入口不启动mDNS/应用清单加载。保留成熟GameStream配对/认证、RTSP/RTP/FEC/加密输入，固定HDMI appid1；仍编译链接通用process/video/display/Linux platform依赖，ldd仍含Pulse/X11/VA相关库，**不是只有传输库的最终极小体积**。

声音显式`hw:`/`plughw:`设备，后者允许ALSA转换；无默认麦克风回传。父会话只读发现T6 HDMI card可能没有PCM capture节点，尚未部署/解决，不能用null通过替代真实HDMI音频。

交付时无编译/测试进程，容器只剩sleep。根据父会话指令保留`rkmoon-minimal-server-build`容器与`/root/rkmoon-minimal-server-build`暂存树等待取包；收到确认后仅清这两项，保留所有artifact。父会话负责释放VM301窗口及安排延时优雅关机，不关MS-A2宿主。
