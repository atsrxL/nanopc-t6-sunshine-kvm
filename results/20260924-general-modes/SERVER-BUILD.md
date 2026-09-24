# 通用源模式服务端构建

2026-09-24，ADR-009。服务端范围：偶数64..3840 × 64..2160，fps_x100=100..12010，吞吐≤3840×2160×6010。Sunshine非1080p尺寸仍需high-resolution许可。旧1440p90标志兼容解析但不再限制模式。

本次只访问MS-A2/VM301，没有T6/Znas操作，没有client改动，没有commit。父代理EDID文件及其他代理修改保留。

构建环境为VM301 x86_64 + ARM64 Docker/QEMU；MS-A2原已在线，VM301原停止，本任务启动。镜像与依赖缓存复用既有输入，源码补丁、编译、完整测试均在远端。固定Sunshine 63d35f702ee9e362e43263742981836ec0710384，MPP 0986d01294d5c2449c14cf13af9b740368c33967。独立固定checkout应用overlay，不覆盖旧树未知编辑。

- worker ARM64完整链接通过，提前交付供父代理实验：private/results/local/general-modes/rkmoon-worker。
- worker SHA256 be9c807c7154724fc5b17f3b06fa5597a75eb1f8ecb49f8edbbe430560e7665a。
- NEEDED：librockchip_mpp.so.1、libstdc++.so.6、libgcc_s.so.1、libc.so.6；容器ldd全部解析，包括传递libm和ARM64 loader。RUNPATH含构建目录；部署需正确SONAME的目标MPP库/显式库路径，不能由容器ldd推导T6依赖通过。
- Linux原生31/31通过；113项Python/联合中111通过、2内核fixture条件跳过。
- 新增覆盖自定义尺寸、1..120.10fps、4K60.10吞吐边界及越界、4K1fps AVC≥5.1、120fps AVC/HEVC等级、59.94/119.88/75.5整数映射及不匹配不覆盖。
- 首轮display测试缺initializer_list头导致编译失败，已保留日志；仅补头后worker及全部离线测试重跑通过。

完整证据在VM301 /root/rkmoon-artifacts/20260924-general/。实际HDMI、BetterDisplay自定义模式、客户端性能由父代理另测；本记录不将离线测试当作硬件通过。1fps低帧率时限风险见ADR-009，未实测证明。
