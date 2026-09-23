# 许可证与可复现性边界

本包主要自有服务端源代码采用GPL-3.0-or-later，保留文件SPDX与GPL全文。独立 Linux 内核 HDMI RX 修复（`kernel/hdmirx-codec/`、`tools/prepare_hdmirx_codec.py`及其测试）另按文件标记采用 **GPL-2.0-only**，保持固定上游codec的GPL-2.0-only和版权；它不是链接进Sunshine的GPLv3模块。`capture.cpp`标明参考旧nanopc-t6-kvm native/capture.c的设计来源；没有复制旧项目配置、凭据或整套VNC架构。

固定Sunshine CMake元数据明确为GPL-3.0-only。把本包辅助代码合入并发布专用Sunshine时按GPLv3兼容的共同版本处理，不能把上游整个项目重新标成“-or-later”。MPP整体Apache-2.0、部分文件Apache-2.0 OR MIT；kvmd GPL-3.0-or-later。最终分发二进制必须包含对应版本完整源码/改动、适用版权/NOTICE以及上游第三方许可证，不能只附此小清单。这里不是替代逐文件审计或正式法律意见。

本ZIP不捆绑Sunshine/MPP/kvmd完整第三方树或厂商预编译库；提供固定URL/commit、可执行下载/补丁/构建流程。首次完整构建后归档源码与依赖锁文件、git submodule status、OS包/toolchain/npm版本及binary/library SHA256。所有运行凭据/配对状态独立存放，不构成“可复现构建输入”。

当前只锁定主源码与上游已锁依赖，不保证OS包仓库快照、全部传递依赖或位级可复现。下一步本地构建完成后补齐构建环境manifest，不能用本清单宣称本包包含一个已验证的ARM安装镜像。
