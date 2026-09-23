# 实际云端检查记录（非硬件验收）

2026-09-23。环境见environment.json；无T6、无MPP SDK、无Moonlight客户端。

| 命令/项目 | 结果 | 日志 |
|---|---|---|
| cmake -S . -B 独立build + cmake --build -j4 | exit0；真实核心/V4L2 probe/输入fixture | configure.log / build.log |
| ctest --test-dir 独立build --output-on-failure | 25/25通过 | ctest.log |
| PYTHONPATH=python RKMOON_NATIVE_TEST_DIR=独立build python3 -m unittest discover -s tests -p test_*.py -v | 68/68通过，无skip；实际C++输入线程→Python socket→fake backend | python-tests.log |
| cmake ... -DRKMOON_SANITIZE=ON && build && ctest | 25/25通过，ASan+UBSan | asan-configure/build/tests.log |
| Python联合测试使用ASan C++ fixture | 68/68通过 | asan-python-tests.log |
| g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -Iinclude -c src/worker_main.cpp | exit0，仅CLI object，未链接MPP | worker-cli-compile.log |
| python3 -m compileall -q python tools tests | exit0，语法检查，不是部署验证 | 见final-checks.json |
| bash -n tools/build.sh | exit0，语法检查 | 见final-checks.json |
| 容器git读取上游 | DNS失败；连接器另可读取公开源码 | network-check.log |

使用小型锚点fixture验证了补丁工具的失败保护/多行签名，**没有完整Sunshine checkout上的真实套补丁/链接结果**。`encoder_mpp.cpp`是真实API实现，但本容器没有SDK，未编译该翻译单元。没有以mock头文件或软件编码替代。

25+68为不同测试类别；ASan复跑不算新增硬件/功能用例。任何模拟数据、fake HID、测试NAL都只验证离线逻辑。所有P1真实采集、P2客户端、P3物理USB、P4时延、P5长稳仍未测试。
