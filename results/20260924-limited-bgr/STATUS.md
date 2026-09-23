# Limited BGR worker 紧急修复

父会话报告：NUC9安装Intel驱动后，T6 HDMI输入为BGR24、quantization=2（LIMITED）；旧worker明确拒绝该范围，View立即退出。当前音源是NUC9，不是此前MS-A2；音频调查暂停，NUC9尚未枚举Intel HDA控制器。

## 交付

- 分支 `fix/limited-bgr-worker`，冻结源码 `5445ddc62da27cfde47fdeb45a10e9cdee61ac45`。
- VM301：`/root/rkmoon-artifacts/20260924-limited-bgr/rkmoon-worker`。
- ELF AArch64，82296 bytes，SHA256 `26db57837dc394e2f8f1ac77b9ba54304d6eb84172064f8851ed29ce582e5626`。
- Ubuntu GCC13 ARM64交叉编译，仅worker/core/capture目标；不编Sunshine或MPP，不访问/部署T6、NUC9或Windows。
- 重用已保留ARM64 runtime中的固定MPP `0986d01294d5c2449c14cf13af9b740368c33967` 动态库与同commit headers；未使用浮动SDK。
- librockchip_mpp.so.0 SHA256 `3123715c4e284b1f851f78184ac505b90091461f70b6a999ae0a3704d27b82db`，SONAME `librockchip_mpp.so.1`。
- 依赖既有MPP库、系统libstdc++/libgcc/libc/ARM64 loader；需GLIBCXX_3.4.32（目标Debian13现有运行时）。保留父会话现有LD_LIBRARY_PATH、授权和`--allow-copy`。无root/setcap、设备模式设置或重启要求。

## 修复路径

`Layout.rgb_limited`仅标记BGR输入16..235，独立于表示编码输出YUV范围的`full_range`。Capture识别LIMITED不再拒绝。原MPP RGB CSC假定full RGB，故limited BGR即使尺寸/stride符合直通条件也禁止DMA导入；必须明确允许copy。

有限范围BGR先顺序拷贝至缓存缓冲，按BT709 Q16矩阵转换为limited NV12：Y16..235，UV16..240，2x2色度平均后才舍入；输入superblack/superwhite裁至16..235。MPP接收NV12并仍做**硬件视频编码**，不是CPU编码兜底；输出full_range标志仍为false，不能假报DMA-BUF。原full BGR硬件CSC及NV12路径保持不变。

## 验证与边界

- VM301 Linux x86_64 native离线 **28/28通过**：原25项及3项新增limited测试。
- 新增覆盖灰阶与黑16/白235、低/高越界、六基色/互补色，512点独立浮点BT709参考（误差≤1 LSB），混色2x2色度、源/目标stride与黑边、缓冲边界、拒绝错误full输出和aligned limited BGR直通。
- ARM64 worker编译/链接exit0，保留 `build.log`、`build.exit`、`native.log`、`native.exit`。只有固定MPP头的原有pedantic扩展警告。
- **未测试**：T6真实HDMI像素范围、硬件编码图像颜色/帧率/负载、NUC9客户端View及声音。父会话部署验收；不能把交叉编译或离线测试写作硬件通过。

音频内核修复在同一祖先分支已有提交，但本worker构建没有编入或操作任何内核模块。VM301的音频容器仅作为现有交叉工具链复用，当前本任务构建已结束且容器仅sleep；artifact/树保留等待父会话确认回收。
