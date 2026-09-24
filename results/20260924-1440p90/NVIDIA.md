# NVIDIA 自定义1440p90验证

2026-09-24，Znas Tiny11_clone / Windows Znas-vm，RTX5060Ti直通，驱动32.0.16.1692，物理DISPLAY1对应RKP3588。全部Windows操作经宿主Guest Agent，试用在at交互会话的临时计划任务执行。

## 结果与限定

NvAPI_DISP_TryCustomDisplay两次返回NVAPI_OK；第一次T6只读V4L2 probe实测2560×1440、89.9935Hz、BGR24单plane、stride7680、sizeimage11059200。这证明源输出及T6时序锁定，不证明DMA采集帧率、MPP编码、RTP或客户端呈现90fps。没有执行STREAMON。

## 可复现配置

- 基于原EDID SHA256 `629fd2a4bb8644ef4639d8f373cde28032c97d977826cff63353c0561d7e286c`，备份在T6 `/root/agent.backup/rkmoon-1440p90-edid-original.bin`。
- NVAPI自动生成CVT-RB：2560×1440，H前肩48/同步32/总2720，V前肩3/同步5/总1503，H正V负，像素时钟367.93MHz，名义90Hz。比上一轮沿用60Hz消隐的手工时序更合适。
- 将此详细时序置于EDID首选位置；原1080p60移至第二位置。范围描述垂直上限95Hz；HDMI VSDB最大TMDS375MHz；CTA加入HDMI Forum VSDB `67 d8 5d c4 01 4b 80 00`（version1/max375MHz/SCDC present）。重算两块校验、移位保留原CTA DTD。
- 成功试验EDID SHA256 `a053f21b1d205dc749f531edec0ea9c01f0e164cbae8f3ba248f7b65595fc55e`。
- NvAPI_DISP_GetDisplayIdByDisplayName定位DISPLAY1；NvAPI_DISP_GetTiming计算CVT-RB；TryCustomDisplay设置2560×1440、depth32、colorFormat0、srcPartition(0,0,1,1)、比例1、hwModeSetOnly0。每次试用25秒后finally调用RevertCustomDisplayTrial，不调用SaveCustomDisplay。
- NVIDIA公开头文件确认NV_TIMING_INPUT为32字节（flag内匿名union独占存储），NV_TIMING96字节，NV_CUSTOM_DISPLAY144字节；版本为size | (1<<16)。最初24字节绑定返回版本错误，修正后60Hz对照成功。

原EDID下NVAPI自定义1440p60成功，T6锁定59.9962Hz；90Hz返回NVAPI_ERROR。只加入HF-VSDB/范围上限仍失败，加入首选90Hz时序并统一旧VSDB上限后成功。尚未逐个撤销字段做归因实验，不能宣称某一个字段是唯一根因。Windows标准ChangeDisplaySettingsEx的BADMODE不能替代NVAPI自定义分辨率测试。

私有复现脚本和完整日志：private/results/local/1440p90/{edid-hf.py,edid-preferred.py,nvtrial.ps1,make-nvrun.py,nv90-first.txt,nv90-repeat.txt,nv90-repeat-input.json}。脚本通过运行时读取私有连接记录访问设备，不将凭据写入产物。

## 重复验证和结束状态

第二次T6仍锁定2560×1440、89.9935Hz，两次NVAPI revert均为0/NVAPI_OK。临时计划任务和Windows脚本已删除，未持久保存自定义模式。恢复原EDID后读回原SHA256一致，两块校验和0；T6恢复1080p约60Hz，rkmoon-minimal-live与rkmoon-input均active。正式server/client帧率门未改，下一阶段须测真实采集和MPP吞吐，不能从锁定结果直接推导90fps串流验收。
