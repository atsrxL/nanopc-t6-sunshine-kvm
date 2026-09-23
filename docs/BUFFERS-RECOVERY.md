# Buffer所有权、队列与关键帧恢复

## Buffer状态

V4L2分配/MMAP/可选EXPBUF → QBUF：驱动可写 → DQBUF：worker独占 → MPP导入或显式CPU像素处理 → 提交一帧 → 收到完整EOI、验证AU/PTS → QBUF。

MPP仍可能访问输入时绝不QBUF。编码异常不通过“作用域自动归还”偷偷QBUF：保持dequeued，先销毁/flush encoder，再STREAMOFF/关闭Capture。正常路径也在完整AU形成之后归还。`Encoder`声明在`Capture`之后，使析构逆序成立；不要在重构时颠倒。

CPU路径仍保持同样保守所有权，直到编码完成后归还。staging buffer来自MPP；CPU访问exported DMA时使用DMA_BUF_IOCTL_SYNC，写MPP buffer用MPP sync begin/end。直接路径不做CPU访问，依靠驱动/MPP设备同步契约；该契约与目标内核实际行为仍需验证。

本包只接受明确单plane紧凑布局。DMA直接路径额外要求width64对齐、height16对齐及紧凑stride/sizeimage。本版1080p触发显式拷贝路径；不能将1080直接虚报成1088，也不能越界读取UV。NV16/NV24、多独立plane、RGA缩放未实现。

## 每层上限

| 层 | 上限/行为 |
|---|---|
| V4L2 | 4个buffer；drain最多buffer数，仅跳过尚未编码raw |
| MPP提交 | 同时1帧；500ms完整AU deadline，完成前不提交下一帧 |
| AU内存 | 8MiB；超限结束 |
| worker→Sunshine | 一帧一ACK；有限socket内核buffer，send300ms/credit2s deadline |
| Sunshine待发送入口 | 自定义锁保护queue入队，最多2；不使用默认清队列策略 |
| 输入C++/Python | 分别64事件；仅合并相邻相对移动，不越过键/按钮/滚轮边界 |

上游发送线程、FEC暂存、内核UDP队列和客户端仍是额外缓冲，**并未因为入口队列有界就全部验收**。P2/P4需要在真实带宽与丢包下记录frame age。200ms dequeue→enqueue阈值只用于止损，不等于低延迟目标。

## 不能丢P帧后继续依赖它

捕获前/编码前可以跳过raw；输出编码AU后若传输背压，等待有限时间或终止会话，不清掉某个P帧后继续发送依赖它的P帧。重新连接从带参数集IDR开始。

HEVC只把NAL19/20认作安全IDR，拒绝首版CRA/BLA随机访问复杂路径；H264为NAL5。IDR需VPS/SPS/PPS或SPS/PPS内联，NAL解析只做边界/类型/单picture检查，不代替独立完整解码。

客户端恢复请求转MPP_ENC_SET_IDR_FRAME。请求到达时可能已有一个AU形成/在途，该AU保持序列发送，下一提交强制IDR，避免丢帧序号与参考链不同步。初始每会话首帧必须IDR。禁用上游reference-frame-invalidation能力声明，不能把全IDR恢复虚报成逐参考帧失效支持。

## 已测/未测

AU边界、错误头、超限、连续序号、EOF/超时等纯逻辑已离线测试。实际V4L2 DQBUF/QBUF与MPP所有权、cache/coherency、完整EOI语义、sender拥塞/客户端恢复均未实机验收。不要把这里的状态机文字当成硬件已验证证明。
