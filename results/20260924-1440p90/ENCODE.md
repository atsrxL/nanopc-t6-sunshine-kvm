# 1440p90真实MPP编码与独立解码

2026-09-24。按用户纠正重新读取sshh后，T6 SSH登录恢复；此前认证失败不能据此认定设备改密。worker SHA256为9158a60d319e28b818e900c5d7bfc7336a2109cddb607091023944f824269a3e，既有固定MPP0986d01。独立目录/home/at/rkmoon-1440p90-validation，at普通用户运行，无root/setcap，无现有服务替换。

实机加载MPP库SHA256 `3123715c4e284b1f851f78184ac505b90091461f70b6a999ae0a3704d27b82db`，路径 `/home/at/rkmoon-20260923/local/mpp/lib/librockchip_mpp.so.1`；不要与容器新构建库hash混淆。

NVAPI临时1440p90输入，worker请求2560×1440/9000，GOP90，25Mbps，60秒，显式实验授权与采集所有权；未使用allow-copy，两种编码均实际报告dmabuf-import。采集格式BGR24、stride7680、size11059200，源锁定89.9935Hz。先前合成MPP探测H264/HEVC codec_mask3通过，和以下真实HDMI测试分开。

| 项目 | HEVC | H.264 |
|---|---|---|
| 编码/独立解码帧数 | 5394 | 5394 |
| dequeue跨度计算fps | 89.99825 | 89.98159 |
| raw skipped | 0 | 1 |
| dequeue→完整AU P50 | 6.827ms | 6.479ms |
| P95 | 7.025ms | 6.672ms |
| P99 | 7.159ms | 6.839ms |
| 最大 | 13.280ms | 21.483ms |
| Profile/Level | Main/5.1 | Constrained Baseline/5.2 |

本地已有ffprobe逐帧计数及ffmpeg -xerror完整独立解码通过，无诊断；码流2560×1440/yuv420p/无B帧/limited BT709/sRGB transfer符合源与编码预期。tools/validate_capture.py全部提供的检查通过，含持续时长、5%帧率容差、CSV顺序、首帧IDR、码流帧数。30秒主动IDR与GOP周期见JSON。见[HEVC结果](hevc90-validation.json)、[H264结果](h26490-validation.json)。原始bin/CSV保留私有目录及T6独立测试目录。

限制：源画面较静态，码率未达到配置上限；没有复杂动态画质/长稳/温控压力证明。延迟是dequeue→AU，不包含HDMI前端、网络和客户端显示。H264一帧raw skip如实保留，不能宣称零丢帧。未测试完整Sunshine/Windows90fps流、声音、USB。原服务未替换。NV试用自动恢复后已移除临时Windows任务/脚本并恢复原EDID。

## 1080p60回归

恢复原EDID后，同一worker执行真实HEVC1080p60/20Mbps/GOP60/60秒，显式allow-copy（1080高度不满足直接导入对齐），3593帧、59.99909fps、raw skipped0。完整独立解码和所有提供检查通过，见[60Hz回归](hevc60-regression-validation.json)。结束后采集节点无占用；原rkmoon-minimal-live/rkmoon-input保持active，未部署替换服务端。
