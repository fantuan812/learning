# 可观测性、SLO 与故障演练
> 知识成熟度：L2

> 知识基线：以当前标准、协议或 Linux/工具链版本为准，平台差异需实测。
> 最后更新：2026-08-20。
> 官方参考：https://en.cppreference.com/、https://man7.org/。
> 验证与基准：按文中命令执行最小实验，记录结果与边界。

## 三类信号
- Metrics 是可聚合的数值，如 QPS、错误率、队列深度。
- Logs 记录离散事件，应包含时间、级别、服务、请求 ID。
- Traces 描述跨服务调用，span 必须传播 trace context。
- 高基数标签会造成存储和查询成本爆炸。
- 结构化日志便于检索，避免把敏感数据写入日志。

## SLI/SLO
- SLI 是用户可感知指标，如成功率、延迟、可用性。
- SLO 是目标；SLA 是对外承诺，通常带赔偿条款。
- 错误预算 = 允许失败比例 × 统计窗口。
- 消耗过快应暂停高风险发布并优先修复可靠性。
- 采用 burn rate 同时检测短时尖峰和长期趋势。
- 端到端 SLO 优先于单组件“绿色”。

## 容量工程
- 估算峰值流量、并发、数据增长和资源余量。
- 负载测试覆盖稳态、突发、长时间 soak 和恢复。
- 排队系统中利用率接近 100% 会导致延迟急升。
- 设置 CPU、内存、连接数、磁盘和队列水位告警。
- 自动扩缩容需防抖、冷却时间和最大副本限制。

## 故障演练
- 先定义假设、范围、停止条件和回滚方案。
- 注入网络延迟、丢包、分区、进程崩溃和磁盘满。
- 验证超时、重试、熔断、降级和幂等是否生效。
- 演练期间记录检测时间（MTTD）和恢复时间（MTTR）。

## OpenTelemetry 最小示例

Python 示例使用 OTLP exporter 将请求 span 发送到本地 Collector；生产环境应固定 SDK、Collector 和 schema 版本。

```python
from opentelemetry import trace
from opentelemetry.sdk.trace import TracerProvider
from opentelemetry.sdk.trace.export import BatchSpanProcessor, ConsoleSpanExporter
trace.set_tracer_provider(TracerProvider())
trace.get_tracer_provider().add_span_processor(BatchSpanProcessor(ConsoleSpanExporter()))
tracer = trace.get_tracer("demo")
with tracer.start_as_current_span("checkout") as span:
    span.set_attribute("http.route", "/checkout")
    span.set_attribute("tenant", "demo")
```

```bash
python -m venv .venv && . .venv/bin/activate
pip install opentelemetry-api opentelemetry-sdk
python demo.py | tee trace.json
```

预期输出包含 trace_id、span_id、`checkout` 和结束时间。生产 exporter 改用 OTLP/gRPC，并验证 context 在 HTTP、消息队列和异步任务边界正确传播；禁止把用户 token、完整请求体写入 span。

## Prometheus 指标与 PromQL

推荐指标：`http_requests_total{service,route,status_class}`、`http_request_duration_seconds_bucket`、`queue_depth`、`process_resident_memory_bytes`。避免 user_id、match_id 等高基数标签。

```promql
sum(rate(http_requests_total{status_class="5xx"}[5m]))
/
sum(rate(http_requests_total[5m]))

histogram_quantile(0.99,
  sum by (le) (rate(http_request_duration_seconds_bucket[5m])))

sum(rate(http_requests_total[5m]))
```

第一个查询是错误率，第二个为全局 p99 延迟，第三个为 QPS。录制规则应先按 service/route 聚合，避免每次查询扫描原始高基数序列。

## SLO、错误预算与 Burn Rate

假设 30 天可用性 SLO=99.9%，错误预算为 0.1%，即约 43.2 分钟不可用。成功事件定义为 HTTP 2xx/3xx 且端到端延迟低于 500 ms；超时、5xx 和依赖耗尽均计入不良事件。

```promql
1 - (
  sum(rate(http_requests_total{status_class="5xx"}[30m]))
  / sum(rate(http_requests_total[30m]))
)
```

示例告警：短窗 burn rate >14.4 且长窗 >1，表示 1 小时内可能耗尽两天预算；告警必须带 service、route、SLO 名称和当前预算剩余。预算耗尽时暂停高风险发布，但安全修复和回滚不应被阻断。

## 容量模型与 Benchmark

用 Little 定律 `L = λW` 估算并发：QPS 500、平均响应 100 ms 时，平均在途约 50；按 p99 和突发系数 3 规划连接池至少 150，并保留 30% 余量。

```bash
hey -z 10m -c 200 https://service.example/health
```

负载实验分为稳态、2 倍突发、长时间 soak、单实例故障和恢复。记录 QPS、p50/p95/p99、5xx、CPU、内存、GC、队列深度、扩缩容时间；每个场景至少重复 3 次，报告中位数和置信区间。

示例验收阈值：稳态 p99 < 500 ms、5xx <0.1%；突发恢复时间 <5 分钟；单实例丢失时错误率 <1%；内存无持续增长。阈值需根据业务 SLO 校准，不能照搬示例。

## 故障注入与演练 Runbook

1. 记录假设、影响范围、负责人、观察面板和停止条件。
2. 在 staging 或明确隔离的生产单元注入 200 ms 延迟、10% 丢包、依赖 5xx、进程 kill 和磁盘 90% 满。
3. 验证超时预算、重试上限、熔断、降级、幂等和告警路由。
4. 记录 MTTD、MTTR、错误预算消耗及用户影响。
5. 恢复注入后确认 backlog、连接池和缓存逐步回落，避免“恢复但未排空”。

网络延迟示例（仅限隔离命名空间）：

```bash
tc qdisc add dev eth0 root netem delay 200ms loss 10%
curl -fsS https://service/health
tc qdisc del dev eth0 root
```

预期：调用在客户端 deadline 内失败并触发降级，不应无限重试放大流量。演练结束必须保存命令、时间线、面板截图、日志查询和复盘行动项。

## 证据与发布门禁

每次变更至少归档一份 dashboard JSON、PromQL、采样 trace、告警截图和 benchmark CSV。指标 schema 变更需要兼容期与迁移说明；删除指标前先确认无告警和 SLO 依赖。

发布门禁示例：关键 SLO 当前预算剩余 >20%，最近 7 天无未关闭 P1，容量压测通过，故障演练行动项按期关闭。所有阈值均为示例，必须在服务目录登记真实目标、负责人和升级路径。

## 仪表盘设计

- 第一屏展示请求量、错误率、p50/p95/p99、饱和度和预算消耗。
- 每个图表标注单位、窗口、聚合方式和数据延迟。
- 支持从服务到 route、实例、依赖的逐级下钻。
- 面板链接到 runbook、最近发布和相关 trace。
- 告警图表同时展示阈值线与基线，避免只看红绿颜色。
- 高基数明细通过日志或 trace 查询，不直接放入长期指标。
- 关键面板在无数据时显示“无数据”，不能伪装为零。

## 告警治理

- 告警必须有负责人、优先级、摘要、影响和动作。
- 页面告警与通知告警分离，避免重复轰炸。
- 连续触发、恢复和抑制规则写入版本控制。
- 每月复核无动作告警，三次无效触发即降级或删除。
- 依赖告警应关联上游 owner，而不是复制到所有下游服务。
- 告警窗口与 SLO 窗口一致，避免短暂抖动造成误报。

## 演练记录模板

```text
演练编号: game-day-2026-08-20-01
假设: cache 节点不可用
范围: staging / shard-1
开始/结束: 10:00 / 10:30
检测时间: 42 s
恢复时间: 3 m 18 s
用户影响: 命中率下降 12%，无数据丢失
证据: dashboard.json, trace.ndjson, alerts.txt
行动项: 增加缓存降级测试，负责人 alice，截止 2026-08-27
```

演练前验证回滚命令和权限，演练中每 5 分钟记录时间线，演练后 24 小时内完成复盘并将行动项纳入迭代。

## 成本与数据治理

日志采样按错误、慢请求和新版本优先；trace 采用 head/tail sampling 组合。设置指标、日志、trace 的保留期和预算，超过预算自动降低采样而不影响 P1 错误。

遥测数据脱敏：移除 token、身份证、完整 payload 和内部密钥；访问审计记录查询人、目的和时间。跨地域传输遵守数据驻留要求，Collector 仅开放必要端口。

## 复盘与持续改进

每周审查 SLO 趋势、预算消耗、告警噪声、MTTD/MTTR 和容量余量。将重复故障转化为自动化测试或演练；将一次性手工诊断转化为 dashboard、录制规则或 runbook。

## 故障矩阵

|注入|观测信号|预期保护|验收|
|---|---|---|---|
|200ms 延迟|p99、timeout|deadline、降级|错误率<1%|
|10% 丢包|重试率|重试上限|无放大风暴|
|依赖 5xx|依赖错误率|熔断|恢复<5m|
|实例 kill|副本数、连接|重调度|无数据丢失|
|磁盘 90%|容量、写错|限流|服务可读|
|Collector 中断|telemetry drop|本地缓冲|业务不受阻|

每个场景执行前后保存 Prometheus 快照、关键 trace、日志查询和时间线。注入工具、版本、参数和清理命令写入演练仓库。

## OTel Collector 配置

```yaml
receivers:
  otlp:
    protocols:
      grpc: {}
processors:
  batch: {send_batch_size: 512, timeout: 5s}
  memory_limiter: {limit_mib: 256}
exporters:
  debug: {verbosity: basic}
service:
  pipelines:
    traces: {receivers: [otlp], processors: [memory_limiter, batch], exporters: [debug]}
```

启动后发送 10 个 span，预期 Collector 输出 10 条 trace 且无 dropped 数据；压测时观察内存限制触发，确认 backpressure 不会阻塞业务请求线程。

## 运行手册验收

- 值班人能在 5 分钟内定位服务、route 和依赖。
- 每个 P1 告警链接到可执行 runbook。
- runbook 包含回滚、扩容、降级和升级联系人。
- SLO 查询可在独立 Prometheus 重算。
- trace 可由日志 request_id 反查。
- 日志字段 schema 通过自动校验。
- 高基数标签被拒绝或采样。
- 数据保留期符合成本预算。
- 脱敏规则有正反向测试。
- 演练行动项按期关闭。

## 复盘示例

```text
根因: 上游超时未设置 deadline
检测: p99 告警 42 秒后触发
影响: 5xx 0.7%，预算消耗 8%
修复: 客户端 deadline=800ms，重试=1
验证: 200ms 延迟演练通过，MTTR 3m18s
后续: 将 deadline 检查加入静态规则
```

复盘结论必须区分触发原因、放大因素和检测缺口；只写“加强监控”不视为完成，必须落到指标、测试或自动化门禁。

## 交付检查

- SLO 定义已获业务确认。
- 错误预算窗口已配置。
- burn rate 告警已测试。
- dashboard 有 owner。
- runbook 链接有效。
- trace context 跨服务传播。
- 采样策略有成本预算。
- 敏感字段已脱敏。
- Collector 有资源限制。
- telemetry 丢失有可见信号。
- 容量基线已归档。
- 扩缩容阈值已演练。
- 单实例故障已验证。
- 网络故障已清理。
- 磁盘告警有处置人。
- MTTD/MTTR 进入周报。
- 演练证据可重放。
- 行动项有截止日期。
- 预算耗尽有发布策略。
- 复盘结论可验证。
- 事后形成时间线、根因、影响、行动项和负责人。
- GameDay 应逐步扩大 blast radius，保留 kill switch。

## 告警治理
- 告警必须可行动，避免仅以 CPU 高作为用户告警。
- 分级 P0/P1/P2，绑定值班、升级和响应时限。
- 抑制重复告警，聚合同一故障的关联事件。
- 监控监控系统自身的采集延迟和丢失率。

## 实验
1. 构造 5% 错误率，验证 SLO burn rate 告警。
2. 让依赖服务延迟 2 秒，观察超时和级联保护。
3. 关闭一个可用区，验证跨区路由和容量余量。
# 案例：在线服务 SLO
定义请求成功率、p95 延迟和可用区错误率三个 SLI。
按月设置 99.9% 可用性目标，预算约 43 分钟错误时间。
将预算分配给发布、依赖故障和容量波动，禁止无证据消耗。
仪表盘同时显示用户体验、资源利用率和错误预算燃烧速度。
快速燃烧触发值班告警，慢速燃烧进入周报复盘。

## 实验：故障注入
使用限流器将数据库延迟注入 100 ms、500 ms、2 s。
杀死一个 Pod，观察重启时间、请求失败率和连接重建。
阻断一个可用区网络，验证跨区流量和降级策略。
填满临时磁盘，确认日志轮转和写入失败告警。
暂停消息消费者，测量积压增长和恢复时间。
每次注入均记录开始、检测、缓解、恢复四个时间点。

## Tracing 实验
为入口、缓存、数据库和下游 RPC 注入 trace/span。
验证采样后仍可关联错误请求与完整调用链。
控制 baggage 大小，避免把用户隐私传播到下游。
比较 1%、10%、100% 采样对 CPU、网络和存储成本的影响。
使用尾采样保留高延迟和错误 trace。

## Benchmark：容量模型
逐步增加并发，记录吞吐、p50、p95、p99、CPU、内存和 GC。
找到延迟拐点并设置安全工作区间为拐点的 60% 至 70%。
分别测试冷缓存、热缓存和依赖降速三种场景。
改变副本数，验证吞吐近似线性区间和协调开销。
压测结果必须包含版本、硬件、数据集和脚本哈希。

## 演练复盘
演练前定义假设、爆炸半径、停止条件和回滚命令。
演练中禁止临时扩大权限，所有动作写入审计日志。
演练后计算 MTTD、MTTR、错误预算消耗和误报率。
将发现转化为带负责人和截止日期的改进项。
连续三次演练未达标时升级架构评审。

## 监控质量检查
指标名称、单位、标签和聚合方式写入字典。
禁止高基数标签直接进入时序数据库。
告警必须含影响、链接、建议动作和升级路径。
日志采用结构化格式并配置采样、脱敏和保留期限。
定期删除无消费者的指标和无效告警规则。
