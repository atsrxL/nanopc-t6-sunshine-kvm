# 真实1440p90采集阶段

> 后续T6访问已恢复，HEVC/H264各60秒真实编码与完整独立解码通过，见[编码记录](ENCODE.md)。下文保留先前阶段和认证失败历史。

2026-09-24，沿用NVIDIA.md已验证的临时EDID/NVAPI90Hz时序，独立V4L2 MMAP采集，无编码器、无RTP或客户端。开始前fuser确认/dev/video0无人占用，原rkmoon服务保持active但无worker。

30秒预检：2695帧，wall30.00836s，wall89.8083fps（含启动等待），驱动时间戳89.99812fps；序号缺失0、错误帧0；每帧bytesused固定11059200，匹配2560×1440 BGR24，stride7680。帧间隔中位11111us、最大11283us。证据private/results/local/1440p90/raw90-30s.json。此项仅证明缓冲采集吞吐，不证明帧内容、编码和客户端呈现。

测试脚本初版遗漏G_FMT，第一次30秒仅获得6220800字节旧1080p缓冲，因此该轮作废，不能计为2K90通过。核对固定内核95e85f6的rk_hdmirx.c：G_FMT内部set_fmt(false)更新stream->pixm，REQBUFS依赖该缓存。生产Capture::query已有G_FMT；修正独立测试脚本并严格断言尺寸/stride/sizeimage及每个DQBUF有效长度后得到上述结果。未修改内核、未调用S_FMT。

当前MPP编码及90fps串流未测试。

## 编码预检的访问阻碍

新实验worker已上传独立路径 `/home/at/rkmoon-1440p90-validation/worker`，没有覆盖现有服务。T6读回SHA256 `9158a60d319e28b818e900c5d7bfc7336a2109cddb607091023944f824269a3e` 与构建产物一致；ldd成功解析既有 `/home/at/rkmoon-20260923/local/mpp/lib/librockchip_mpp.so.1` 和系统库。紧接下一次SSH执行合成probe前，认证返回Permission denied(publickey,password)，命令未执行，停止认证重试。

在该认证失败之前，NV试用已自动恢复（revert=NVAPI_OK），Windows临时任务和脚本已清理，原EDID已恢复，T6最后一次probe确认1080p59.9952Hz。没有编码进程、真实码流或编码通过结果。待恢复已授权SSH访问后继续，不以产物可链接代替MPP实测。
