---
type: Architecture
title: "IPv6、NAT、服务发现与负载均衡"
status: stable
verified: []
maturity: L2
---
# IPv6、NAT、服务发现与负载均衡
> 验证与基准：按文中命令执行最小实验，记录结果与边界。

> 知识成熟度：L2；地址规划和故障结论必须在目标网络验证。
> 知识基线：RFC 8200、RFC 4861、RFC 8445 与常见 DNS/L4/L7 实现。
> 最后更新：2026-08-20。
> 官方参考：https://www.rfc-editor.org/rfc/rfc8200、https://www.rfc-editor.org/rfc/rfc4861。
> 验证入口：使用 dig、ping6、conntrack、tc/netem 与压测。

## IPv6

- IPv6 地址为 128 位。
- 链路本地地址通常以 fe80::/10 开始。
- 全球单播地址由前缀、子网和接口标识组成。
- SLAAC 依赖路由器通告。
- DHCPv6 可补充地址和配置分发。
- 邻居发现替代 ARP。
- DAD 检查地址冲突。
- RA Guard 防止伪造路由器通告。
- IPv6 没有广播，使用组播。
- PMTUD 依赖 ICMPv6 Packet Too Big。
- 防火墙不能简单阻断全部 ICMPv6。
- Happy Eyeballs 并行尝试 IPv4 与 IPv6。
- DNS 通过 AAAA 记录发布 IPv6。
- 双栈服务需分别观察连接成功率。

## NAT 与边界

- SNAT 修改源地址，DNAT 修改目的地址。
- PAT 通过端口复用连接。
- NAT 会破坏端到端可达性。
- UDP 打洞依赖映射保持时间。
- 对称 NAT 降低打洞成功率。
- STUN 发现外部映射。
- TURN 作为中继兜底。
- UPnP/PCP 自动请求端口映射需审计权限。
- NAT hairpin 允许内网访问内部公网地址。
- 连接跟踪表容量影响突发流量。
- 端口耗尽表现为间歇建连失败。
- 负载均衡前后都要记录真实客户端地址。
- PROXY protocol 传递源地址但需鉴权。

## 服务发现

- 服务发现将逻辑名称映射到实例。
- DNS、注册中心和控制面是常见实现。
- TTL 过长会放大故障窗口。
- TTL 过短增加查询和控制面压力。
- 客户端缓存必须处理过期和空结果。
- 健康检查要验证业务可用而非仅进程存活。
- 注册租约需要心跳续期。
- 时钟漂移不能直接决定租约有效性。
- 启动抖动时使用随机退避。
- 删除实例先摘流，再等待连接排空。
- 服务名、版本、地域和协议应纳入标签。
- 变更必须支持回滚。

## 负载均衡

- 四层均衡依据 IP、端口和连接状态。
- 七层均衡可依据 Host、Path、Header 路由。
- Round-robin 适合实例同质场景。
- Weighted round-robin 支持容量差异。
- Least-connections 适合请求耗时差异明显场景。
- 一致性哈希减少扩缩容迁移。
- Maglev 提供稳定哈希映射。
- 健康检查失败要有连续失败阈值。
- 慢启动防止新实例被瞬间打满。
- 熔断器阻止故障实例持续接收流量。
- 重试必须限制次数并设置预算。
- 重试非幂等请求可能造成重复副作用。
- 超时预算应沿调用链递减。
- 区域优先可降低延迟但增加失效复杂度。
- 跨区故障转移需验证数据一致性。

## 验证

- 用 dig 验证 A、AAAA 和 TTL。
- 用 ping6、traceroute6 检查路径。
- 用 conntrack 观察 NAT 状态。
- 用 tc/netem 注入丢包与延迟。
- 记录每个后端的请求率、错误率和延迟。
- 区分 DNS、连接、TLS、应用四类耗时。
- 演练 DNS 缓存污染和注册中心不可用。
- 演练负载均衡器重启与连接排空。
- 对源地址透传做端到端校验。
- 用压测验证扩缩容期间分布稳定。
## 实验与 Benchmark

建立 client、lb、server 三个 network namespace，配置 IPv6 ULA 与公网前缀，使用 `curl -6` 验证路由、邻居发现和 ACL。

```bash
ip netns add c; ip netns add l; ip netns add s
ip -n s addr add 2001:db8:1::10/64 dev veth-s
curl -6 http://[2001:db8:1::10]:8080/health
```

用 nftables MASQUERADE 和 conntrack 测量 UDP 映射在 30/120/600 秒 idle 下的存活、端口耗尽和重建次数。

部署 CoreDNS/Consul，注入 DNS 延迟、过期记录和节点摘除；验收缓存 TTL 内可服务，解析失败不扩散为雪崩。

比较 round-robin、least-connection、一致性哈希、EWMA：输出 p50/p95/p99、重试率、重映射率和实例负载标准差。

| 算法 | 适用 | 风险 |
|---|---|---|
| RR | 同构实例 | 慢节点拖尾 |
| Least | 长连接 | 状态抖动 |
| Hash | 有状态会话 | 热点 |
| EWMA | 异构实例 | 反馈滞后 |

工程案例：跨地域匹配服务由 DNS 返回地域 VIP，L4 健康检查，L7 按版本灰度；会话使用一致性哈希。

健康检查区分进程、依赖和业务可写，连续三次失败摘除、五次成功恢复并设置隔离时间防抖。

容量验收：连接数、端口、conntrack、DNS QPS 均低于 70% 预算；故障切换 RTO<30 秒。

## 可复现实验与版本基线

固定环境 Ubuntu 24.04、Linux 6.8、iproute2 6.1、nftables 1.0.9、CoreDNS 1.11、Envoy 1.31。`CLIENT_IP`、`VIP`、`BACKEND_A`、`BACKEND_B` 是占位符，执行前替换；`2001:db8::/32` 仅用于文档。

```bash
ip -6 addr; ip -6 route; resolvectl status
sudo nft list ruleset
sudo tcpdump -i any -nn -w dualstack.pcap 'ip6 or (ip and (tcp port 80 or udp port 53))'
```

netns 双栈拓扑：

```bash
sudo ip netns add client; sudo ip netns add lb; sudo ip netns add back
sudo ip link add c0 type veth peer name c1; sudo ip link set c0 netns client; sudo ip link set c1 netns lb
sudo ip link add b0 type veth peer name b1; sudo ip link set b0 netns lb; sudo ip link set b1 netns back
sudo ip -n client addr add 192.0.2.2/24 dev c0; sudo ip -n client addr add 2001:db8:1::2/64 dev c0
sudo ip -n lb addr add 192.0.2.1/24 dev c1; sudo ip -n lb addr add 2001:db8:1::1/64 dev c1
sudo ip -n back addr add 192.0.2.10/24 dev b1; sudo ip -n back addr add 2001:db8:2::10/64 dev b1
```

开启转发并配置 nftables/NAT（仅隔离实验）：

```bash
sudo sysctl -w net.ipv6.conf.all.forwarding=1
sudo nft add table ip nat; sudo nft add chain ip nat postrouting '{ type nat hook postrouting priority 100; }'
sudo nft add rule ip nat postrouting oifname "eth0" ip saddr 192.0.2.0/24 masquerade
```

服务发现使用 CoreDNS 静态记录，TTL 设为 5s、30s、300s，记录缓存命中率、切换延迟、负缓存和连接池复用。

## IPv6、NAT 与负载均衡 Benchmark

使用 wrk2 或 h2load；预热 30s、测量 120s、重复 5 次。固定请求大小、连接数、CPU 亲和性，报告吞吐、p50/p95/p99、连接错误率、后端分布和故障恢复时间。

|场景|协议|连接|故障注入|关键指标|
|---|---|---:|---|---|
|IPv6 直连|HTTP/2|100|无|p99、CPU|
|IPv4 NAT|HTTP/1.1|100|回收 NAT 映射|重连率|
|双栈 Happy Eyeballs|HTTP/2|200|IPv6 延迟+100ms|选择延迟|
|加权 LB|HTTP/1.1|500|权重 80/20|分布误差|
|节点故障|HTTP/2|500|下线 A|错误率、RTO|
|一致性哈希|HTTP/2|500|增加节点 C|缓存迁移率|

预期：IPv6 直连减少转换开销；NAT 映射回收造成短时重连；健康检查收敛时间小于 TTL 与探测间隔之和；一致性哈希增节点的迁移比例低于普通取模。

抓包与状态检查：

```bash
sudo tcpdump -i any -nn 'icmp6 or port 53 or tcp port 80'
ss -s; conntrack -S; sudo conntrack -L | head
```

## 失败排查与安全边界

- IPv6 不通：查 RA、默认路由、链路 MTU、ICMPv6 放行；不要屏蔽 ICMPv6。
- NAT 端口耗尽：查 conntrack、临时端口、TIME_WAIT 与真实并发。
- DNS 切换慢：查 TTL、客户端缓存、负缓存和连接池。
- LB 分布偏斜：查长连接、权重算法、请求耗时和健康检查缓存。
- 故障抖动：增加连续失败阈值、退避和隔离时间，记录 flapping 次数。

规范依据 RFC 8200、RFC 8305、RFC 6555、RFC 6888。实验保存配置、版本、原始日志、pcap、指标和复盘，确保结果可复查。

## 运行记录清单

1. 记录内核、iproute2、nftables、CoreDNS、Envoy 版本。
2. 记录所有接口 MTU 和 MAC。
3. 记录 RA、默认路由和前缀。
4. 记录 AAAA/A/SRV 记录。
5. 记录 DNS TTL 与负缓存。
6. 记录客户端地址选择策略。
7. 记录 Happy Eyeballs 延迟。
8. 记录 IPv4 NAT 类型。
9. 记录 conntrack 表上限。
10. 记录临时端口范围。
11. 记录 TIME_WAIT 数量。
12. 记录 VIP 与后端地址。
13. 记录 LB 算法与权重。
14. 记录健康检查间隔。
15. 记录失败阈值和恢复阈值。
16. 记录摘除与恢复时间。
17. 记录客户端连接池设置。
18. 记录 keepalive 设置。
19. 记录代理超时。
20. 记录重试和退避。
21. 记录请求大小与响应大小。
22. 记录 wrk2/h2load 版本。
23. 记录并发连接与线程数。
24. 记录预热时长。
25. 记录测量时长。
26. 记录重复次数。
27. 记录 p50/p95/p99。
28. 记录吞吐和错误率。
29. 记录后端分布误差。
30. 记录缓存命中率。
31. 记录 DNS 查询 QPS。
32. 记录 conntrack 使用率。
33. 记录端口耗尽事件。
34. 记录邻居表溢出。
35. 记录 ICMPv6 丢弃。
36. 记录 PMTU 探测失败。
37. 记录 TCP SYN 重传。
38. 记录 TCP RST。
39. 记录 DNS SERVFAIL。
40. 记录 DNS NXDOMAIN。
41. 记录服务端 5xx。
42. 记录 LB 连接拒绝。
43. 记录 LB 重试次数。
44. 记录节点健康状态。
45. 记录节点 CPU 和内存。
46. 记录网络带宽。
47. 记录丢包和抖动。
48. 记录网卡错误计数。
49. 记录防火墙命中计数。
50. 记录 nftables 规则快照。
51. 注入 IPv6 延迟。
52. 注入 IPv6 丢包。
53. 注入 IPv4 NAT 回收。
54. 注入 DNS 暂停。
55. 注入后端 A 下线。
56. 注入后端 B 下线。
57. 注入权重变更。
58. 注入节点扩容。
59. 注入节点缩容。
60. 注入健康检查误报。
61. 注入连接池 stale。
62. 注入 MTU 黑洞。
63. 注入邻居发现失败。
64. 注入 conntrack 满载。
65. 注入端口耗尽。
66. 记录每个故障的开始时间。
67. 记录告警触发时间。
68. 记录自动修复时间。
69. 记录人工介入时间。
70. 记录用户影响范围。
71. 记录恢复 RTO。
72. 记录数据错误率。
73. 记录重复请求率。
74. 记录会话迁移率。
75. 记录缓存迁移率。
76. 记录普通取模对照。
77. 记录一致性哈希对照。
78. 记录 Maglev 对照。
79. 记录 EWMA 对照。
80. 记录最小连接对照。
81. 记录长连接偏斜。
82. 记录短连接偏斜。
83. 记录跨地域延迟。
84. 记录地域故障切换。
85. 记录 DNS 地域策略。
86. 记录 TTL 对 RTO 的影响。
87. 记录客户端缓存差异。
88. 记录 resolver 实现差异。
89. 记录防火墙 IPv6 策略。
90. 记录安全组规则。
91. 记录源地址验证。
92. 记录反射放大防护。
93. 记录 RA Guard。
94. 记录 DHCPv6 策略。
95. 记录地址隐私扩展。
96. 记录日志脱敏。
97. 记录 pcap 权限。
98. 记录配置版本。
99. 记录脚本 commit。
100. 记录结果文件 SHA256。
101. 保存原始 stdout/stderr。
102. 保存 Prometheus 指标。
103. 保存 Grafana 截图。
104. 保存 DNS 查询日志。
105. 保存健康检查日志。
106. 保存 conntrack 快照。
107. 保存 nftables 计数器。
108. 保存路由表快照。
109. 保存邻居表快照。
110. 保存故障注入命令。
111. 说明剔除样本规则。
112. 报告均值和标准差。
113. 报告尾延迟。
114. 报告置信区间。
115. 报告容量余量。
116. 报告风险等级。
117. 给出临时缓解。
118. 给出永久修复。
119. 增加回归测试。
120. 增加监控告警。
121. 指定复盘责任人。
122. 指定评审人。
123. 指定上线门槛。
124. 指定回滚阈值。
125. 指定下一轮实验日期。
