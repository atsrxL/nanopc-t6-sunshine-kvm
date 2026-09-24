# 客户端黑屏与USB直通复查

> 最新：用户明确选择固定2560×1440 90Hz后，重新应用已验证EDID a053f21b1d205dc749f531edec0ea9c01f0e164cbae8f3ba248f7b65595fc55e，并以NVAPI TryCustomDisplay+SaveCustomDisplay（输出/显示器限定）成功保存，均返回0。Guest RTX5060Ti报告2560×1440/90，T6锁定2560×1440/89.9935。两个rkmoon服务active，客体USB键鼠仍OK。此次不自动恢复1080p；Windows临时任务/脚本已清理，保存的自定义模式保留。EDID仅当前内核运行态，尚未配置T6重启自动加载，不能承诺重启后仍保持此模式。客户端需同步选1440p90，真实呈现等待用户确认。

2026-09-24用户确认1080p有画面、2K黑屏。T6生产日志显示2K请求被Capture::validate拒绝：client/source dimensions do not match。Guest Agent查询RTX5060Ti仍输出1920×1080/60Hz，T6锁定1080p59.9968；不是已确认的2K解码失败。当前实现不缩放、不自动更改源模式，客户端尺寸和帧率必须匹配物理HDMI输出。用户报告1080p有画面与日志HEVC帧发送相符，但不证明90fps客户端验收。

USB只读验证：Znas Tiny11_clone live XML含USB1d6b:0104 bus8/device2；lsusb设备在位。QGA客体枚举RKMOON001 Composite、HID Keyboard、HID mouse均Status OK；T6 UDC configured。直通本身已正确配置。

T6 kvmd OTG保持旧offline状态，源码online只有成功写report才置true；全部未按下的释放事件可能被去重，没有使状态恢复，未据此宣称硬件故障。单独restart rkmoon-input（未重绑gadget/UDC、未改VM）后keyboard/mouse online均true。备份/home/at/rkmoon-http-1440p90/runtime.json到/root/agent.backup/rkmoon-http-runtime-before-input.json，恢复input.enabled=true并重启专用rkmoon-http-live。两服务active、私有hid.sock创建；等待用户实际键鼠确认，不将枚举正常等同输入消费验证。
