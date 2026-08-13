# 06 SLI-SLO与可观测性

> 知识成熟度：L2
> 适用范围：平台可靠性-SLI-SLO与可观测性
> 本文由《01-鉴权限流幂等灾备与可观测性》"七、OpenTelemetry、SLO/SLI、RED/USE 与容量模型"拆分而来，正文逐字迁移。

## 元数据
- 最后更新：2026-08-13。
- 知识成熟度：L2。
- 适用范围：平台可靠性-SLI-SLO与可观测性。
- 知识基线：平台可靠性工程方案（非 UE 内置能力）；事实边界以原《01-鉴权限流幂等灾备与可观测性》对应章节为准。
- 拆分来源：原《01-鉴权限流幂等灾备与可观测性》"七、OpenTelemetry、SLO/SLI、RED/USE 与容量模型"。

## 概述
可观测性用 traces、metrics、logs 三类信号把一次请求从入口连接到依赖，SLO/SLI 把"服务是否可靠"变成可度量的语言。
本文覆盖 trace_id 传播、RED 与 USE、SLO 与错误预算、告警分级、容量模型与压测方法。
观测数据应在每个边界带上相同的 trace_id、请求名、版本和结果分类。

### 1.1 三类遥测信号

OpenTelemetry（OTel）提供 traces、metrics、logs 的统一采集与关联模型；实现方式和后端产品仍需按当前项目选择。
Trace（链路）回答一次请求经过哪些服务和依赖；Metric（指标）回答总体趋势和分位数；Log（日志）回答某个事件的详细上下文。
三类信号应共享 trace_id、span_id、service.name、service.version、deployment.environment 和 region 等字段。
日志要避免把 token、密码、支付密文、完整设备密钥和大段请求体写入可检索平台。
高基数字段如完整 account_id 不应无审查地作为指标标签，否则可能耗尽时序数据库。
OTel 的采集和语义约定可参考 [OpenTelemetry Documentation](https://opentelemetry.io/docs/)。

### 1.2 trace_id 传播

入口生成或提取 trace_id 时，必须校验格式和长度，避免用户输入污染日志。
HTTP、RPC、消息头和异步任务都要定义传播方式。
异步消息可以保存 producer_trace_id 和 consumer_span_id，但不能把一条长生命周期 trace 无限延伸。
跨线程、协程和队列切换时要显式传递上下文，不能依赖线程局部变量的偶然行为。
采样策略应保证错误、慢请求和高风险写操作优先保留。
响应给客户端的 request_id 可以由 trace_id 派生或单独生成，但不能泄露内部账号或拓扑信息。

```text
function handle_request(request):
    ctx = telemetry.extract(request.headers)
    span = tracer.start_span("game.request", parent=ctx)
    span.set_attribute("rpc.method", request.method)
    span.set_attribute("service.version", BUILD_VERSION)
    response = dispatch(request.with_context(span.context))
    span.set_attribute("http.status_code", response.code)
    if response.code >= 500:
        span.record_exception(response.error)
        span.set_status(ERROR)
    span.end()
    return response.with_header("x-request-id", span.trace_id)
```

### 1.3 RED 与 USE

RED 适合请求型服务：Rate 是速率，Errors 是错误，Duration 是延迟。
USE 适合资源型组件：Utilization 是利用率，Saturation 是饱和度，Errors 是错误数。
游戏业务还需要补充业务正确性指标，例如扣款成功率、幂等冲突率、订单对账差异和匹配成功时延。

| 对象 | RED 指标 | USE 指标 | 业务补充 |
| --- | --- | --- | --- |
| 网关 | QPS、5xx、p99 | 连接利用率、队列长度 | 限流拒绝率 |
| 业务 API | 请求率、错误、p50/p95/p99 | 工作线程/协程饱和 | 成功状态迁移率 |
| 数据库 | 查询率、错误、查询延迟 | CPU、连接池、锁等待 | 提交回滚、账本差异 |
| Redis | 命令率、错误、响应延迟 | 内存、连接、阻塞 | 缓存命中与失效 |
| 消息系统 | 发布/消费率、错误、端到端延迟 | 分区积压、消费者槽位 | 重复和死信率 |
| 匹配服务 | 入队/出队率、失败、匹配延迟 | 队列占用、线程饱和 | ticket 过期率 |

### 1.4 SLI 与 SLO

服务水平指标（SLI）是可测量的服务表现；服务水平目标（SLO）是时间窗口内的目标阈值。
可用性不能只用进程存活率，应使用“符合业务成功定义的请求数 / 有效请求数”。
延迟 SLO 要写清分位数、请求范围、超时是否计入、排队时间是否计入。
数据正确性 SLO 可以用账本对账差异为零、重复发奖率为零、不可解释订单比例低于阈值表达。
错误预算（Error Budget）让发布、压测和功能开发共享同一风险语言。
SLO 的数值只是方案起点，需结合真实基线、用户体验和成本评审。

```text
availability = good_requests / valid_requests
error_budget = 1 - availability_slo
budget_consumed = bad_requests / valid_requests
remaining_budget = error_budget - budget_consumed
```

例如一个 SLO 是 99.9%，不意味着每个请求都能失败 0.1%，还要明确统计窗口和业务分层。
订单、货币和背包可以比装饰性推荐使用更严格的正确性目标。

### 1.5 告警分级

告警应指向动作，不应只报告“CPU 高”。
P0 表示大范围、持续或数据安全/经济安全风险，需要立即全员响应和止损。
P1 表示核心登录、订单、货币、背包、匹配或大区不可用，需要值班快速介入。
P2 表示局部退化或有明确绕行方案，可在工作时段处理。
P3 表示趋势、容量或维护提醒，不应制造夜间噪声。

| 等级 | 例子 | 首要动作 | 通知范围 |
| --- | --- | --- | --- |
| P0 | 货币账本出现不可解释差异、双主写入 | 立即冻结高风险写入并保护证据 | 事件指挥组、业务负责人 |
| P1 | 核心 API 5xx 超 SLO、登录大面积失败 | 限流/降级/摘流，定位版本和依赖 | 值班、服务 owner |
| P2 | 单区服 p99 上升、队列积压可控 | 扩容、调参、创建工单 | 值班与模块 owner |
| P3 | 证书 30 天内到期、容量趋势 | 排期处理并复核 | 模块 owner |

告警必须包含影响范围、开始时间、当前版本、trace/日志查询链接、runbook、负责人和升级路径。
同一根因的多个告警应聚合，避免告警风暴掩盖 P1。

### 1.6 容量模型

容量估算至少包含流量、并发、资源成本、峰值系数、故障余量和增长率。
Little 定律可以用来校验排队关系：并发数约等于到达率乘以平均停留时间。
数据库写入容量应按业务事件数、重试放大、索引写放大和复制开销计算。
缓存容量应包含热键、过期抖动、峰值装载和故障回源，不只看平均命中率。
跨区灾备要预留单区失效后的容量，不能两区都按 50% 满载运行却没有故障余量。

```text
peak_rps = average_rps * peak_factor
effective_rps = peak_rps * (1 + retry_amplification)
required_instances = ceil(effective_rps / safe_rps_per_instance)
db_write_rps = command_rps * writes_per_command * index_write_factor
headroom = available_capacity - required_capacity
```

容量模型中的 safe_rps 应取压测中满足延迟和错误 SLO 的值，而不是进程刚开始排队时的最大吞吐。
模型应按登录峰值、开服峰值、活动结算峰值、故障转移峰值和回放峰值分别计算。

### 1.7 压测方法

压测数据应区分基准、阶梯、尖峰、耐久、故障恢复和容量边界六类。
基准压测建立单实例安全吞吐；阶梯压测寻找饱和点；尖峰压测观察突发和限流；耐久压测发现泄漏和积压。
故障恢复压测加入数据库、消息、缓存或服务发现故障，验证降级和回切。
测试账号、设备、订单和资产必须可清理，不能让压测污染生产经济。
压测请求要包含真实比例的读、写、重试、超时和消息消费，而不是只发健康检查。
结果至少保留 p50、p95、p99、最大值、错误分类、队列长度、数据库等待和 CPU/内存/网络。

## 失败路径

| 失败路径 | 快速判断 | 默认动作 |
| --- | --- | --- |
| trace 上下文丢失 | 跨线程/协程/队列切换后无 span | 显式传递上下文，不依赖线程局部变量的偶然行为 |
| 采样丢弃关键请求 | 错误/慢请求无样本 | 错误、慢请求和高风险写操作优先保留 |
| 高基数标签 | 指标基数爆炸 | 完整 account_id 不无审查作为指标标签 |
| 告警风暴 | 同根因多条告警 | 同一根因的多个告警聚合，避免掩盖 P1 |
| 容量无故障余量 | 单区失效即过载 | 跨区灾备预留单区失效后的容量 |

## FAQ

### Q8：CPU 不高但接口很慢，先扩容可以吗？

先看连接池等待、锁等待、队列积压、下游延迟、尾延迟和重试放大。CPU 低可能意味着线程在等待依赖，盲目扩容会把连接和下游压得更满。

## 验收清单

- traces、metrics、logs 共享 trace_id、span_id、service.name、service.version、deployment.environment 和 region 等字段。
- 入口生成或提取 trace_id 时校验格式和长度，避免用户输入污染日志。
- 采样策略保证错误、慢请求和高风险写操作优先保留。
- RED 用于请求型服务，USE 用于资源型组件，并补充业务正确性指标。
- SLO 写清分位数、请求范围、超时与排队时间是否计入。
- 错误预算让发布、压测和功能开发共享同一风险语言。
- 告警按 P0-P3 分级并指向动作，同一根因的多个告警聚合。
- 压测覆盖基准、阶梯、尖峰、耐久、故障恢复和容量边界六类，结果保留分位数与错误分类。

## 关联阅读

- [00-平台可靠性总览与迁移说明.md](00-平台可靠性总览与迁移说明.md)：本目录总览与迁移说明，含责任边界、最佳实践清单与 FAQ。
- [04-平台与可靠性/README.md](README.md)：本目录导航与学习路径。
- [02-数据与业务/05-部署运维与监控.md](../02-数据与业务/05-部署运维与监控.md)：监控、告警与运维专题细节。
- [游戏算法/03-工程与实用技巧/03-AOI与视野计算.md](../../游戏算法/03-工程与实用技巧/03-AOI与视野计算.md)：AOI 与视野计算涉及热点、分片、广播和容量模型。
- [OpenTelemetry Documentation](https://opentelemetry.io/docs/)：traces、metrics、logs 与上下文传播的官方文档入口。

## 更新日志

- 2026-08-13：从《01-鉴权限流幂等灾备与可观测性》"七、OpenTelemetry、SLO/SLI、RED/USE 与容量模型"（7.1-7.7）拆出独立成篇；正文逐字迁移，标题按本文重编号。
