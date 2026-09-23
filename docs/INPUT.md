# Moonlight → USB输入

## 路径与身份

Moonlight已配对/验证/解密输入 → Sunshine高层映射 → 专用平台出口 → C++有界队列 → 同UID Unix socket → Python独占租约 → 鉴权的既有kvmd socket API → 既有USB gadget → 目标电脑。

没有root键盘抓取，没有T6本机uinput注入，没有网络可访问的输入端口。配对安全留在上游；本地所有权通过0700目录/0600socket/SO_PEERCRED约束，同UID仍属于信任边界。不要让不可信程序共享运行UID或私有配置。

## 支持范围

规范化Windows VK的常见US键盘键、左右修饰键、导航、F1–F12、数字区、常用OEM标点；鼠标相对移动、5按钮、垂直和水平滚轮。固定 kvmd 不支持大部分 F13–F24，首版统一拒绝这些扩展键，释放过程也不发送它们。Generic/Left modifier别名用引用集合避免过早key-up；重复down不重复按键，自动重复交给USB host。

VK不是任意Unicode文本。JIS/IME特有键、媒体键、layout-specific扩展/非标准flags、绝对鼠标、触摸、笔和手柄不在首版验收范围。未知键撤销输入租约并释放，不转发到T6“兜底”。必要时本地扩展映射必须补测试并核实kvmd允许的Web key names。

输入日志不输出VK值/字符/事件内容，只记录连接、撤销、队列/错误计数。上游debug日志可能包含输入，不在生产或共享日志中打开。禁止提交配对资料或auth_headers_file。

## 既有相对HID检查

kvmd状态必须enabled且keyboard/mouse online、mouse.absolute=false。对于只有一个相对mouse实例，outputs.active可能为空，因此不能只检查active==usb_rel。源码`MouseProcess`把absolute写入初始状态，且相对event在absolute模式直接被忽略。[S7]

本包不调用set_params、reset、set_connected，不绑定/解绑UDC。旧记录为absolute，不能直接假定兼容。需要修改descriptor或增加相对gadget时由用户另行授权，先保存旧descriptor和绑定配置，不能在本包start中偷偷完成。

## 释放策略

一次租约允许一个客户端。hello前锁定busy，校验成功后接受事件；C++心跳250ms，Python lease timeout2s。队列满、非法事件、EOF、桥接失联、后端失败均关闭租约。按下操作在请求前先记录“可能已按下”，因为超时也可能已送达；撤销时释放这一集合和按钮。

释放失败保留集合并重试，busy持续锁住，拒绝新租约。bridge启动和本实例停止后可独立neutralize全部支持键/按钮，覆盖桥接SIGKILL后的内存状态丢失。该操作必须独占授权，因为会影响其他控制源。

kvmd HTTP成功仅表示事件接受，不等于目标电脑收到USB报告。USB断开、kvmd不可达/内部队列卡住时，逻辑释放不能保证最后一公里送达。要测试实际主机事件，并把未确认释放视为未完成回滚。不得抹掉错误后再允许接管。

## 验证

离线测试覆盖别名/组合键/滚轮120单位累计、motion分块、queue edge/overflow、EOF、超时、异常释放重试及真实C++→Python socket交互。必须额外实测按住Ctrl/Shift/鼠标按钮中断网络、退出Sunshine、kill worker、kill bridge、kvmd拒绝响应，以及另一客户端同时连接。

逐事件kvmd HTTP路径优先正确性，未承诺它比旧路径更快。若P3测得明显输入延迟，可在新的ADR中改为已验证kvmd websocket或独立HID后端；不能省掉鉴权/独占/释放机制换吞吐。
