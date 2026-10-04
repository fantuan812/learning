---
type: BestPractice
title: "测试、静态分析、Fuzz 与持续交付"
status: stable
verified: []
maturity: L2
---
# 测试、静态分析、Fuzz 与持续交付
> 验证与基准：按文中命令执行最小实验，记录结果与边界。

> 知识成熟度：L2；质量阈值应结合项目风险设定。
> 知识基线：测试金字塔、Clang Sanitizers、libFuzzer 与 CI/CD 门禁。
> 最后更新：2026-08-20。
> 官方参考：https://clang.llvm.org/docs/LibFuzzer.html、https://clang.llvm.org/docs/AddressSanitizer.html。
> 验证入口：运行单元/集成测试、静态分析、Sanitizer 和持续 fuzz 任务。

## 测试设计

- 单元测试验证局部不变量。
- 集成测试验证模块边界和协议。
- 系统测试验证真实部署路径。
- 契约测试验证消费者与提供者。
- 回归测试固定已修复缺陷。
- 属性测试验证输入空间中的性质。
- 负向测试覆盖拒绝、超时和重试。
- 测试应尽量确定性。
- 随机测试保存 seed 和环境。
- 测试夹具避免共享可变状态。
- 时间、网络和随机数应可注入。
- 测试命名说明场景与预期。
- 失败输出必须包含最小复现信息。

## 最小测试项目与覆盖率

```bash
cmake -S . -B build -G Ninja -DENABLE_COVERAGE=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
llvm-profdata merge -sparse build/*.profraw -o build/merged.profdata
llvm-cov report build/app -instr-profile=build/merged.profdata
```

示例门禁（不是通用标准）：行覆盖率 ≥80%、分支覆盖率 ≥70%，且新增代码覆盖率不得低于 75%。阈值必须写入仓库配置并允许按风险分层；覆盖率下降时输出差异报告，而不是简单补无意义断言。

测试样例固定 `TZ=UTC`、locale、随机 seed 和时钟注入；失败时保存 seed、命令、提交、编译器和依赖版本。

## 静态分析与 Sanitizer

```bash
clang-tidy src/*.cpp -- -std=c++20 -Iinclude
scan-build --status-bugs cmake --build build
cmake -S . -B asan -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build asan && ctest --test-dir asan --output-on-failure
```

门禁示例：新增 clang-tidy error=0，UBSan/ASan 运行期错误=0；历史告警使用带责任人和过期日期的基线文件，禁止无限期抑制。

## 最小 libFuzzer harness

```cpp
// fuzz/parse_fuzz.cpp
#include <cstddef>
#include <cstdint>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > 4096 && data[0] == 0x7f) __builtin_trap();
  return 0;
}
```

```bash
clang++ -g -O1 -fsanitize=fuzzer,address fuzz/parse_fuzz.cpp -o parse_fuzz
mkdir -p corpus artifacts
./parse_fuzz corpus -max_total_time=60 -artifact_prefix=artifacts/
```

验收记录运行时长、执行次数、覆盖函数数、崩溃 artifact 和最小化输入。示例要求 60 秒无崩溃；这不是安全证明，需将发现的输入加入回归 corpus。变更解析器后至少重新跑 10 分钟 nightly fuzz。

## CI 流水线

```yaml
jobs:
  quality:
    steps:
      - uses: actions/checkout@v4
      - run: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
      - run: cmake --build build --parallel 2
      - run: ctest --test-dir build --output-on-failure
      - run: clang-tidy --quiet ...
      - run: ./scripts/coverage_gate.sh 80 70
  fuzz:
    steps:
      - run: ./parse_fuzz corpus -max_total_time=60 -artifact_prefix=artifacts/
      - uses: actions/upload-artifact@v4
```

PR 阶段执行快速单测、静态分析和 60 秒 smoke fuzz；夜间执行长 fuzz、集成测试、依赖扫描和性能基准。失败日志必须保留 14 天，artifact 关联 commit SHA。

## Benchmark 与故障案例

测试吞吐基准固定输入规模、线程数、CPU 亲和性和预热轮数：预热 5 次、测量 20 次，报告 p50/p95/p99、吞吐和错误率。示例回归阈值为 p95 不得比主分支下降超过 10%，否则阻断合并。

故障案例：一次边界检查缺失在 ASan 中触发堆越界；复盘链路为 fuzz artifact → 最小化输入 → 单元回归 → 修复提交 → 线上监控。另一次 flaky 测试通过注入时钟和固定 seed 消除，连续 100 次重复运行通过后才解除隔离。

发布验收清单：单测全绿、覆盖率达到示例阈值、静态分析无新增 error、Sanitizer 无错误、fuzz smoke 无崩溃、性能 p95 在预算内、制品可回滚。

## 测试数据与隔离

- 测试数据按 schema 版本管理，禁止直接复用生产快照。
- 随机输入使用可记录 seed，失败后可一键重放。
- 网络、文件系统、时钟和线程调度通过接口注入。
- 并行测试不得共享临时目录、端口和全局环境变量。
- 集成测试启动依赖后执行 readiness 检查。
- 超时失败同时输出线程栈、容器日志和资源水位。
- 测试完成后清理容器、卷、临时凭据和子进程。
- 任何 flaky 用例进入隔离队列并设置最长修复期限。

## CI 成本与安全

按变更路径选择流水线：文档变更不触发完整编译，公共头和构建脚本变更触发全平台矩阵。缓存 key 必须包含编译器、OS、依赖锁文件和 commit，避免错误复用。

CI token 使用最小权限，第三方 action 固定 SHA；上传 artifact 前清理环境变量、密钥和个人路径。Fork PR 不应获得生产凭据或写权限。

基准任务使用专用 runner，记录 CPU 型号、内核、频率策略、容器限制和温度；跨机器比较只做趋势判断，不宣称绝对性能提升。

## 质量报告样例

```text
commit: 1a2b3c4
compiler: clang 18.1.8
tests: 248 passed, 0 failed
line coverage: 84.2% (gate 80%)
branch coverage: 73.1% (gate 70%)
asan/ubsan: clean
fuzz: 1.8M exec, 0 crash, 412 corpus files
benchmark p95: 3.4 ms (baseline 3.2 ms, +6.2%)
```

报告中的每个数字都应能通过命令和 artifact 重算；门禁脚本退出码非零时禁止人工勾选通过。

## 失败矩阵与验收

|故障|检测|预期动作|证据|
|---|---|---|---|
|单测失败|ctest 非零|阻断合并|测试日志|
|ASan 越界|Sanitizer 报告|上传最小复现|artifact|
|覆盖率下降|gate 脚本|要求补测试或批准例外|lcov|
|Fuzz 崩溃|崩溃文件|加入回归 corpus|输入文件|
|静态分析 error|clang-tidy|修复或带期限抑制|报告|
|性能回归|p95 超预算|标记 benchmark job|CSV|

示例输出：

```text
248/248 tests passed
line 84.2% >= 80%; branch 73.1% >= 70%
ASAN: no errors; UBSAN: no errors
fuzz: 2,104,311 exec/s, crashes=0
benchmark: p95=3.4ms, baseline=3.2ms, delta=6.2%
```

验收规则：任何 P0/P1 缺陷、未解释的 flaky、Sanitizer 错误或覆盖率低于阈值均阻断发布；性能阈值为示例，服务 owner 可在配置中调整但必须记录理由。

## CI 分层配置

```yaml
pull_request:
  - format
  - unit
  - tidy
  - asan
nightly:
  - integration
  - fuzz-10m
  - mutation
  - benchmark
release:
  - full-matrix
  - sbom
  - artifact-sign
```

PR 流水线目标 10 分钟内反馈；nightly 失败自动创建 issue 并附日志、commit、runner 镜像和重现命令。发布流水线拒绝带未签名 artifact。

## 复盘模板

```text
缺陷: parser 越界
引入: commit abc123
检测: nightly fuzz 18m
影响: 无生产影响
逃逸原因: 缺少长度边界属性测试
修复: def456 + 回归输入
预防: 新增 fuzz corpus 门禁
负责人/期限: team-x / 2026-08-30
```

复盘必须验证修复在旧版本、ASan、Release 和目标平台均生效；关闭行动项时附 CI run 链接。

## 最终交付清单

- 变更说明包含测试范围。
- 失败用例已关联 issue。
- 测试数据已脱敏。
- 运行环境已记录。
- 依赖版本已锁定。
- artifact 已签名。
- 覆盖率报告已归档。
- Fuzz corpus 已上传。
- 性能基线已更新。
- 回滚版本已验证。
- 监控告警已演练。
- 责任人已确认。
- 发布窗口已预约。
- 审批记录完整。
- 变更后观察期已安排。
- 复盘时间已设置。
- 例外项有到期日。
- 门禁脚本可离线运行。
- CI 日志无秘密。
- 用户影响评估完成。
- 兼容性矩阵通过。
- Windows/Linux 构建通过。
- Debug/Release 构建通过。
- 并行测试无竞态。
- 资源清理无泄漏。
- 长时 soak 无增长。
- 关键路径有回归。
- 边界输入有属性测试。
- 变异测试结果已审查。
- 供应链扫描无阻断项。
- 产物下载可验证。
- 发布通知已准备。
- 值班人已轮值。
- 紧急联系人已更新。
- 变更窗口无冲突。
- 质量指标进入周报。
- 技术债务已登记。
- 下一步行动已排期。
- 覆盖率是信号，不是正确性证明。
- 变异测试评估断言强度。
- 性能测试隔离预热与稳态。

## 静态分析

- 编译器警告按级别治理。
- clang-tidy 规则按目录逐步启用。
- 静态分析结果需要去重和分级。
- 抑制必须有原因、范围和过期时间。
- include-what-you-use 减少隐式依赖。
- include guard 或 pragma once 保证幂等。
- 未定义行为检查优先于风格规则。
- AddressSanitizer 捕获越界和 use-after-free。
- UndefinedBehaviorSanitizer 捕获整数和类型问题。
- ThreadSanitizer 捕获部分数据竞争。
- Sanitizer 构建不直接替代发布构建。
- 分析版本应锁定工具链。

## Fuzz

- Fuzz target 应小而可组合。
- 输入解析器优先于业务全链路。
- 语料库保存有效和边界样本。
- 变异器关注长度、编码和状态转换。
- 超时、崩溃和 OOM 分开统计。
- 崩溃样本自动去重。
- 最小化输入降低修复成本。
- Fuzz 运行需设置资源上限。
- 长期 fuzz 使用语料库轮换。
- 修复后将样本加入回归测试。
- 覆盖率增长不等于缺陷减少。
- 外部输入路径应有持续 fuzz 预算。

## CI/CD

- CI 分为格式、编译、测试、分析和打包阶段。
- 快速检查先运行以缩短反馈。
- 失败步骤保留完整日志。
- 依赖缓存必须可失效。
- 并行任务声明资源需求。
- 密钥通过受控注入而非仓库文件。
- 合并门禁阻止红灯进入主干。
- 夜间任务运行全量和长时测试。
- 制品上传后做哈希校验。
- 发布使用不可变版本号。
- 灰度发布观察错误率和延迟。
- 自动回滚需要明确触发阈值。
- 数据库迁移必须向后兼容。
- 发布记录提交、构建、环境和审批。
- 供应链步骤生成 SBOM 和签名。
- 事故后把复现加入 CI。
## 可执行案例与实验

网络协议解析器流水线：单元测试覆盖错误码，属性测试验证 round-trip，libFuzzer+ASan/UBSan 捕获越界和溢出；崩溃样本最小化后纳入回归 corpus。

```bash
cmake -S . -B build -DENABLE_ASAN=ON -DENABLE_UBSAN=ON
cmake --build build -j
ctest --test-dir build
./fuzz_parser -max_total_time=300 corpus/
```

实验记录 line/branch/function coverage 与 mutation kill rate；门槛分别为 80%、70%、60%，并要求 Fuzz 覆盖持续增长。

故障注入实验：依赖超时、磁盘满、线程耗尽、证书过期；验证熔断、回滚、告警和错误预算暂停发布。

Benchmark 记录 CI 排队、构建、测试、制品上传、部署 p50/p95；Fuzz 记录 executions/s、覆盖边、唯一崩溃和去重耗时。

静态分析启用 clang-tidy、cppcheck、include-what-you-use；新增告警阻断，历史债务进入燃尽队列。

测试设计覆盖状态、输入域、时间、并发、故障、恢复；随机测试记录 seed，报告包含环境、跳过项、风险和回滚指针。

---

## 关联知识与工程落地

- **前置依赖**：
  - [软件工程与构建基础](../工程设计与协作/软件工程与构建基础.md)：测试金字塔架构。
  - [版本控制依赖与构建工程](../工程设计与协作/版本控制依赖与构建工程.md)：CI 流水线集成。
- **调试器与 Sanitizer 结合**：
  - [11-工程调试与性能分析/01-调试与性能分析方法论](../调试与性能分析/01-调试与性能分析方法论.md)：ASan/TSan 内存检测。
  - [16-容器云与可观测性/02-可观测性SLO与故障演练](../部署运维与可观测性/02-可观测性SLO与故障演练.md)：发布后故障演练。
- **游戏测试工程落地**：
  - [游戏测试与质量/README](../../../00_Index/学习路线/工程实践与质量.md)：自动化测试框架与压测体系。
- **分类与领域入口**：
  - [15-软件工程与构建 README](../../../00_Index/学习路线/工程实践与质量.md)
  - [计算机与工程基础 Domain MOC](../../../00_Index/学习路线/编程与计算机基础.md)
