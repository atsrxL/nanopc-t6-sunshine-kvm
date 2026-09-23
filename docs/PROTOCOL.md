# 私有 worker IPC v1

这是同机父子进程协议，不是GameStream/RTP。`posix_spawn`通过Unix socketpair继承fd3，只执行绝对路径，不用shell。MPP进程通过PDEATHSIG在父进程消失时结束。原始像素不经过socket。

## 64字节小端头

| 偏移 | 类型 | 字段 |
|---|---|---|
| 0 | 4 bytes | `RKMF` |
| 4 | u16 | version=1 |
| 6 | u16 | kind |
| 8 | u32 | payload字节数 |
| 12 | u32 | flags |
| 16 | u64 | 会话内连续AU序号，从1开始 |
| 24 | u64 | dequeue_us |
| 32 | u64 | submit_us |
| 40 | u64 | done_us |
| 48 | u32 | width |
| 52 | u32 | height |
| 56 | u16 | codec：1H264/2HEVC |
| 58 | u16 | reserved=0 |
| 60 | u32 | extra：caps/fps/bitrate等 |

kind：FRAME1、ACK2、STOP3、IDR4、BITRATE5、READY6、CAPS7、ERROR8。FRAME最多8MiB；诊断最多1024bytes；控制包无payload。flags：IDR=1、DMA-BUF=2、full_range=4、BT709=8。所有字段含义/验证以`core.cpp`为准；不要向公网监听本协议。

READY声明实际width/height/codec/fps×100。CAPS是worker --probe合成黑帧经MPP验证后的mask（H264=1、HEVC=2），不是HDMI信号声明。正常视频不使用合成帧。

FRAME必须完整AU、首帧IDR、inline参数集，序号连续且时戳顺序dequeue≤submit≤done。单socket表示一个epoch，重连新socket重新从1开始，禁止拼接旧socket残包。

ACK.seq必须匹配刚发送的AU；ACK之前不再提交下一个编码输入。IDR可在等待ACK时收到，应用在下一次编码。BITRATE只有worker降码率控制原语，Sunshine自适应码率尚未接入；上调要求重连。STOP/EOF终止并清理。

I/O deadline覆盖完整头+payload，防止只完成一部分字节后无限等待。任何长度/版本/序号/格式/关键帧错误关闭会话，不用搜索下一magic继续猜测流边界。IPC没有另加CRC，因为本地可靠socket保证有序传输，且长度/格式/AU和周期严格验证；这不意味着它适合不可信跨机网络。

时钟为同一Linux主机的steady_clock/CLOCK_MONOTONIC；字段只覆盖dequeue之后。不要用它宣称完整HDMI采集延迟，也不要直接减客户端时钟。
