# 补丁交付方式

`tools/apply_sunshine.py`是源码补丁生成/应用工具，固定commit且默认dry-run。它给7个已核对的真实接口增加专用入口，并复制`sunshine/`及必要自有辅助代码到新上游树的`src/rkmoon/`。

不在包中伪造一个声称“git apply检查通过”的.patch：构建容器无GitHub网络checkout，当前没有完整树的实际套用证据。接手后先用`--patch-output /tmp/rkmoon-sunshine.patch`生成真实diff审阅，再`--apply`，完整编译并将生成patch/hash保存在本目录。

小型锚点fixture离线测试已通过；它不是对完整Sunshine项目成功套用/编译的替代。遇上游不匹配先检查固定版本，禁止放松认证/软件回退/模糊覆盖。
