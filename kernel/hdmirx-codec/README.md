# RK3588 HDMI RX capture 修复（独立 GPL-2.0-only 模块）

## 根因与范围

T6运行内核 `6.1.115-vendor-rk35xx` / Armbian `26.5.1` 对应固定源
`armbian/linux-rockchip@95e85f6cb496c75807c5b16f158853578e7e7d1b`。
该源包含 `78c67d98f221895336d41d8799b38eff6b6b7b4e`：本意避免HDMI TX被PulseAudio打开capture、导致mono，但 `hdmi_codec_probe()` **未判断TX/RX**，把两个DAI的capture channels_min/max统一清零；原I2S capture为2..8，SPDIF为2..2。

只读实机事实：codec.5父设备 `fdee0000.hdmirx-controller` 的compatible为下面两项，CPU capture-only，card存在却无PCM；精确headers `.config` 与运行 `/proc/config.gz` 解压内容SHA相同。源码缺陷已确认；硬件因果及录音结果必须由父会话部署后另行确认，不能以模块编译代替。

`upstream-rx-capture.patch` 仅在明确RX parent保留capture，其他设备维持TX限制：

- `rockchip,rk3588-hdmirx-ctrler`
- `rockchip,hdmirx-ctrler`

临时外置模块额外在probe分配/注册前拒绝其他parent（`-ENODEV`），更名：

- 文件/模块：`rkmoon_hdmirx_codec.ko` / `rkmoon_hdmirx_codec`
- platform driver/alias：`rkmoon-hdmirx-codec`
- 保持pdev `hdmi-audio-codec.5.auto`、ASoC组件和DAI名字不变。
- 原始文件没有EXPORT_SYMBOL，生成模块也不新增导出；所有私有codec函数仍static，可与built-in原driver并存。
- 不触碰TX codec，不注册旧`platform:hdmi-audio-codec` alias，不自动加载/绑定，不修改内存或内核镜像。

## 生成与构建约束

```sh
# 只生成源码；输入必须为上述精确revision的原始hdmi-codec.c。
python3 tools/prepare_hdmirx_codec.py --source /path/to/pinned/hdmi-codec.c --output /empty/module-dir
make -C /path/to/exact/headers ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  CC=aarch64-linux-gnu-gcc HOSTCC=gcc M=/absolute/module-dir -j2 modules
```

- headers精确包：`linux-headers-vendor-rk35xx=26.5.1`；禁止误用candidate26.8.3。
- deb SHA256 `12e3626e6e61b2754882f0f625e9092e5b1c13578d9b7555d38dbecabf910bf2`。
- `.config` SHA256 `5b58b391d0a7fbd541cce217a09bae0adaf67904ead72f612f79ff0041862f8f`。
- `Module.symvers` SHA256 `314fdd61e1438ceb2cdb08cae57ba6f6f92ac40d2d9311903c04251381006e91`。
- target工具链：Ubuntu GCC `13.3.0-6ubuntu2~24.04.1` / binutils2.42。
- `CONFIG_MODVERSIONS=y`，签名强制关闭。禁止`--force`/去vermagic/去modversions。

头包不含预编modpost。构建时在**可丢弃副本**用同包源码构建fixdep/genksyms/modpost host工具，然后重新提取**原样pristine headers**，只拷贝这些host可执行程序，在pristine上做M=外置模块编译，`KCONFIG_NOSILENTUPDATE=1`。普通`make scripts`曾改变CC文本、pahole/BTF等探测配置，被hash检查拒绝；没有将漂移后的headers用于最终模块。最终config/symvers均保持原hash。无需全内核编译、BTF运行时内存补丁或T6工具链安装。

## 已交父会话的artifact

VM301：`/root/rkmoon-artifacts/20260923-hdmirx-codec/rkmoon_hdmirx_codec.ko`

- 453296 bytes；SHA256 `3f1ea066271374f84e4fb90bfa5ab9739fe3c2ea49eaedd2666c62ae9b319cd8`。
- vermagic：`6.1.115-vendor-rk35xx SMP mod_unload modversions aarch64`。
- depends为空：所需符号在目标配置内置。
- 27个undefined均在精确symvers中，28条modversions（含module_layout）CRC全部一致；module_layout=`0xe7dc8449`。
- `build-external.exit=0` / `MODULE_BUILD_OK`。仅因没有vmlinux跳过BTF生成；未强制加载。
- 证据同目录：`build-external.log`、`modinfo.txt`、`module-versions.txt`、`undefined-symbols.txt`、`abi-validation.json`、原样source及patch/hash；此前失败和config漂移diff也保留。

## 父会话部署/回滚契约（本子会话未执行）

父会话控制设备窗口并已备份至 `/root/agent.backup/rkmoon-hdmi-audio-20260923`。
停止本实例音频使用者；确认codec.5的父设备和原driver后，先解绑 `rk-hdmi-sound` 的 `hdmiin-sound`，加载已校验模块，解绑**仅**codec.5，将其driver_override设为新driver并绑定，再重新绑定machine创建PCM。检查实际capture节点/格式，不能改采Realtek并宣称HDMI声音。

任一步失败：停止测试/释放PCM，解绑machine及新codec，恢复原driver_override和原built-in `hdmi-audio-codec`绑定，再恢复machine；确认退出状态后卸载新模块。其他TX codec/HDMI video/HID/USB role/EDID/boot保持不变。不要操作全局driver unregister或卸载原built-in codec；不得以失败后未恢复的状态继续推进。

源码生成/3项转换单测、交叉编译/CRC校验与真实模块加载、PCM枚举、真实HDMI录音/频谱及客户端Opus声音必须分别记录。

父会话后续实机反馈：已验证模块SHA并insmod、仅codec.5 override成功，card0 `rockchiphdmiin` capture PCM恢复；源端MSA2 hw:0,3输出48k stereo S16处于RUNNING/ELD有效，但T6相同格式arecord报`pcm_read Input/output error`，Read Wait Time MS=50。枚举修复已验证，真实声音**仍未通过**，继续只读分析RX/I2S/DMA启动条件；不得继续宣称端到端验收完成。
