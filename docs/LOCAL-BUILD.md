# 隔离构建与本地验证

2026-09-23 已在 Debian 13 amd64 容器完整编译固定 MPP worker 与专用 Sunshine。结果见 [构建记录](../results/20260923-build/STATUS.md)。目标 ARM64 编译和硬件验收仍需独立执行。

```bash
docker build -f tools/Dockerfile.build -t rkmoon-build .
docker run --rm -it -v "$PWD:/work" -w /work rkmoon-build
tools/build.sh offline
python3 tools/fetch_sources.py
python3 tools/apply_sunshine.py vendor/sunshine --patch-output /tmp/review.patch
# 审阅 /tmp/review.patch 后应用到这个独立 checkout
python3 tools/apply_sunshine.py vendor/sunshine --apply
JOBS=8 tools/build.sh mpp
JOBS=8 tools/build.sh sunshine -DGLAD_SKIP_PIP_INSTALL=ON
```

挂载目录的属主需与容器构建用户一致；不要禁用整个系统的 Git safe.directory 检查。转移 checkout 时保留上游 `.git` 目录及每个子模块的 `.git` 指针，否则版本校验或 FFmpeg 发布标签解析会失败。不要将 vendor、私有配置或编译产物提交到 Git。

Dockerfile 安装工具仅发生在容器中。镜像标签及发行版包随时间变化，不能宣称逐字节可复现。生产 worker 始终使用 MPP；Sunshine 上游 FFmpeg 构建依赖不代表启用软件编码回退。不要给这个构建容器映射视频/HID 设备或 privileged 权限。

`tools/build.sh sunshine` 将资源目录编译为当前 build/sunshine/assets 的绝对路径，运行时须保留该目录。迁移到目标板时应在最终目录重新构建，不能仅复制 Sunshine 二进制。

## 下一阶段的具体操作范围

1. 在独立 ARM64 构建环境完成同样构建，保存二进制和动态库 SHA256。
2. 使用目标原生 probe 读取 /dev/video0 的 timings、format、stride，不启动 streaming。
3. 明确采集窗口和恢复方式；仅在授权窗口内停止占用视频节点的旧实例，测试完成后恢复并检查旧服务。
4. 根据目标私有 MPP 实际需要，为运行用户临时授予 /dev/mpp_service 和所需 DMA heap 的读写 ACL；保存原 ACL，测试后精确恢复。不得用 root 启动 Sunshine 或全局 chmod 666。
5. 先合成黑帧 MPP 探测，再真实 1080p60 HEVC/H264 各 60 秒、独立解码，再 video-only Moonlight。
6. USB 输入阶段单独确认 kvmd 相对鼠标、API 鉴权与独占授权。绝对 HID descriptor 变更须单独审核；未验证前保持 input.enabled=false。

权限/采集窗口尚未执行，当前仓库不是已通过实机验收的可部署发行版。
