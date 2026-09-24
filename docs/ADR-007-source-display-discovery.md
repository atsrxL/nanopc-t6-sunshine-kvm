# ADR-007: 由HDMI源决定客户端画面模式

用户要求客户端自动跟随服务端真实画面尺寸与刷新率，取消手动模式选择，视频设置只保留码率和编码。保持HTTP密码认证，无UDP明文广播、无自动修改源EDID或GPU模式、无缩放/桌面重抓屏。

## 认证服务发现字段

现有GET /serverinfo在密码校验后增加以下XML子元素：

| 字段 | 含义 |
|---|---|
| RKMoonDisplayVersion | 1 |
| RKMoonDisplayStatus | ready / no_signal / unsupported / unavailable |
| RKMoonDisplayWidth、RKMoonDisplayHeight | ready时真实像素尺寸，否则0 |
| RKMoonDisplayFpsX100 | ready时时序计算Hz×100四舍五入，否则0 |

查询仅open+VIDIOC_QUERY_DV_TIMINGS+close，250ms缓存限制重复请求。禁止通过G_FMT探测发布模式：此固定Rockchip驱动的G_FMT会更新内部DMA布局，可能干扰正在使用旧布局的worker。无信号不复用上次有效画面。宽高、隔行、帧率及服务端60/实验1440p90许可均验证，未知模式不发布ready。像素格式与颜色仍由worker启动时严格校验；时序ready并不是完整编码保证。

客户端连接和发起流前读取模式；认证失败、缺少版本或非法字段不回退旧手动配置。流中异步轮询检测信号变化；小于等于0.15Hz的时钟抖动不触发重连。分辨率/刷新率改变必须结束原流并完成资源清理，再以新参数发起流。无信号时等待恢复，用户主动退出优先，不能被后台重连覆盖。兼容API字段扩展，不重写GameStream媒体协议。

Ctrl+Alt+Shift+C保留本地鼠标指针显示切换；该显示行为不得改变USB相对鼠标协议。
