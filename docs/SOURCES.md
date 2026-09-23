# 源码依据（固定版本，2026-09-23核查）

以下链接供接手者直接读真实接口。文档中的[Sx]指这些第一手来源，不指云端已做硬件验收。

**S1 — Sunshine发行版/安全基线**
https://github.com/LizardByte/Sunshine/releases/tag/v2026.914.233613
固定commit `63d35f702ee9e362e43263742981836ec0710384`。发布说明列出Linux安全修复GHSA-fp6g-27w5-489j。投入部署前仍需复核之后的新安全公告，不能把固定版本当作永远安全。

**S2 — Sunshine视频/队列/构建**
https://github.com/LizardByte/Sunshine/blob/63d35f702ee9e362e43263742981836ec0710384/src/video.h
https://github.com/LizardByte/Sunshine/blob/63d35f702ee9e362e43263742981836ec0710384/src/video.cpp
https://github.com/LizardByte/Sunshine/blob/63d35f702ee9e362e43263742981836ec0710384/src/thread_safe.h
https://github.com/LizardByte/Sunshine/blob/63d35f702ee9e362e43263742981836ec0710384/CMakeLists.txt
https://github.com/LizardByte/Sunshine/blob/63d35f702ee9e362e43263742981836ec0710384/cmake/prep/options.cmake

**S3 — Sunshine网络会话/音频**
https://github.com/LizardByte/Sunshine/blob/63d35f702ee9e362e43263742981836ec0710384/src/rtsp.cpp
https://github.com/LizardByte/Sunshine/blob/63d35f702ee9e362e43263742981836ec0710384/src/audio.cpp

**S4 — 当前平台输入与安全启动**
https://github.com/LizardByte/Sunshine/blob/63d35f702ee9e362e43263742981836ec0710384/src/platform/virtualhid_input.cpp
https://github.com/LizardByte/Sunshine/blob/63d35f702ee9e362e43263742981836ec0710384/src/input.cpp
https://github.com/LizardByte/Sunshine/blob/63d35f702ee9e362e43263742981836ec0710384/src/main.cpp

**S5 — Rockchip MPP实际配置项与接口**
https://github.com/rockchip-linux/mpp/blob/0986d01294d5c2449c14cf13af9b740368c33967/mpp/base/mpp_enc_cfg.c
https://github.com/rockchip-linux/mpp/blob/0986d01294d5c2449c14cf13af9b740368c33967/utils/mpi_enc_utils.c
https://github.com/rockchip-linux/mpp
核对了h265 profile/tier/level、prep/rc/low_delay等真实配置键，不以FFmpeg支持作为Sunshine已有MPP支持的证据。

**S6 — 用户旧仓库（仅参考）**
https://github.com/atsrxL/nanopc-t6-kvm/blob/6c381b30f8fc3b34a70a63840eb817f549e844b2/native/capture.c
https://github.com/atsrxL/nanopc-t6-kvm/blob/6c381b30f8fc3b34a70a63840eb817f549e844b2/results/20260923-live/STATUS.md
https://github.com/atsrxL/nanopc-t6-kvm/blob/6c381b30f8fc3b34a70a63840eb817f549e844b2/sources.lock.json
旧NV12/BGR DMA-BUF帧率与70.47ms/13.68ms记录，不是本包HEVC/Moonlight新测量；未复制敏感原日志。

**S7 — kvmd API与相对HID实际行为**
https://github.com/pikvm/kvmd/blob/78ff181e95b14327831441d58f2f7f4cb2181cde/kvmd/apps/kvmd/api/hid.py
https://github.com/pikvm/kvmd/blob/78ff181e95b14327831441d58f2f7f4cb2181cde/kvmd/plugins/hid/otg/__init__.py
https://github.com/pikvm/kvmd/blob/78ff181e95b14327831441d58f2f7f4cb2181cde/kvmd/plugins/hid/otg/mouse.py

**S8 — 备选实现（未采用/未对本硬件验证）**
https://github.com/games-on-whales/wolf
https://games-on-whales.github.io/wolf/stable/developer/how-it-works.html

最后仍需本地核对原版Moonlight客户端自己的版本、HEVC硬件解码与显示统计；服务器代码/网页不能提供该客户端的实际能力证明。
