# PCM恢复后EIO：固定源码的下一判据

全部源位置固定于 `95e85f6cb496c75807c5b16f158853578e7e7d1b`；不再构建新模块，不写设备。

1. `sound/soc/rockchip/rockchip_i2s_tdm.c:2512` 在startup将非零Read Wait Time MS传入 `substream->wait_time`。`sound/core/pcm_lib.c:1879,1935` 直接用该值等待，超时即返回 `-EIO`。它不是只在发生DMA硬件错误时才报错。父会话报告值50ms，因此应保存实际hw_params，先排除period超过等待时间；可由父会话做10ms period/40ms buffer的有限录音，不应盲目加大timeout就宣称修复。
2. `drivers/media/platform/rockchip/hdmirx/rk_hdmirx.c:3640..3655` 的codec hw_params和startup即便未检测audio_present也返回成功。PCM打开不证明RX正在输出I2S。
3. RX音频work由 `DEFRAMER_VSYNC_THR_REACHED_IRQ` 安排在0.5秒后启动（2810..2816）；work设置AUDIO_ENABLE（3780），从ACR CTS/N和TMDS计算采样率（3500），只有有效采样率路径才更新音频clock并使能I2S（3790..3819）。最终还会因AUD_SAMPLE_FLAT关闭I2S_EN（3842）。
4. `hdmirx_audio_setup()` 初始设置44100并关闭audio domain；所以sysfs audio_rate=44100、clk_hdmirx_aud=5644800只是默认状态，不能据此认为收到了44.1k音频。

父会话已确认模块加载与capture PCM枚举恢复，但真实录音仍EIO。随后本子会话只读观察：

- RX mode是HDMI，1920x1080p60；不是当前DVI模式。
- 曾执行Vertical Sync阈值中断与delayed_work_audio enable audio。
- audio_present=0，audio_rate=44100；clk_hdmirx_aud=5644800。
- 父会话启用的**单一函数**dynamic_debug中，连续12条 `hdmirx_audio_fs` 均为fs_audio=0、acr_cts=0、acr_n=0。

这些读数必须和源端测试音RUNNING时间对齐；单独不能证明音源活动期间完全无包。若同一窗口仍全零，应优先定位实际HDMI路由/源端ACR发送/RX packet接收解析，不应再修改codec枚举、I2S DMA或使用未验证寄存器patch。源端ELD有效与ALSA RUNNING本身也不能代替RX收到有效ACR/音频的证据。

真实HDMI录音、频谱、Opus/客户端闭环仍未通过；硬件操作与失败回滚只由父会话执行。
