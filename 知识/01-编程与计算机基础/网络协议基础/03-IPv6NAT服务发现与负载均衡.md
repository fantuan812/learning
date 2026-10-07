---
type: Architecture
title: "IPv6、NAT、服务发现与负载均衡"
status: stable
verified: []
maturity: L2
---
# IPv6、NAT、服务发现与负载均衡

> 知识成熟度：L2。本文核对正式规范选段、指定版本产品文档、有限 API 注释与原论文表，建立可手算的架构案例；没有网络协议运行、部署、抓包或性能测量证据。
> 本次修订：2026-10-07。延续原有成文的 IPv6、NAT、发现与负载均衡贡献，将当前学习入口改为静态推导；2026-08-20 的前言、实验方案与完整运行模板保留在文末有界历史区。原日期和版本不能代表本轮环境。

## 0. 一次请求究竟成功到哪一层

“DNS 有记录”“端口能连接”“实例健康”和“订单成功”回答的是不同问题。本文以创建一张订单为目标，沿地址、连接、身份、实例选择和提交结果逐层追踪；每个后续例只替换明确列出的条件。所有地址都是文档示例，时间、容量、权重和业务约束均是题设输入，以下预期结果属于纸面推导（PAPER_EXPECTED）。读者只需读取表格并演算，不需要访问任何目标网络。

### 0.1 双栈 VIP、两级选择与独立身份边界

逻辑入口是 `https://api.example.test/orders`。客户端 C 在 eth0 有 IPv4 `192.0.2.20` 和 IPv6 `2001:db8:10::20`；本主例先给定地址有效、默认路由与下一跳解析成功、正反路径和相关策略均满足要求。DNS 返回 AAAA=`2001:db8:100::10`、A=`198.51.100.10`，TTL 为 30 秒：这是同一服务的两个 VIP 候选，既不是后端实例清单，也不是服务身份凭据。

数据路径为 C → 双栈 VIP → L4 选择前端 F1/F2 → F 的 TLS 终止与 L7 路由 → 后端 A/B → 业务存储。这里 L4 只按 IP、端口和连接状态选择每条新 TCP 连接的前端，不解密 TLS；本次选中 F1，同一连接始终归 F1。C 验证原 URL 的 reference DNS-ID `api.example.test`，SNI 同名。题设为 TLS 1.3 完整握手，无恢复、无 0-RTT、无客户端证书认证；证书路径、有效时间、名称、CertificateVerify、双方 Finished 及其他必要检查全部通过，ALPN 为 h2。HTTP/2 初始化已完成，流控与并发额度足以承载本请求。

F1 解密后，根据已校验的 authority、path 和版本策略为每个新 HTTP 请求选 A 或 B，本请求选 A。Header 也可成为明确应用路由合同的输入，但未经校验的头不自行取得路由或授权权力。同一个下游 HTTP/2 TCP 连接的不同 stream 都先到 F1，F1 可以分别为它们选 A/B；因此“L4 已选 F1”不等于“后端永远是 A”。上游连接池按选定 host、协议等参数隔离，不能选 A 却取 B 的池代发。

F1→A 是另一个独立受保护传输。本例另行给定 A 的服务身份检查与准入成功，不能把 C→F1 的 TLS 自动延伸到这一跳。应用还独立验证 tenant-a 有 CreateOrder 权限：源 IP、SNI、Host、注册标签、PROXY 字段都不能替代租户授权。上述 TLS 与业务身份条件是本例前提；本文不提供证书或监听器配置。

| 当前得到的证据 | 它允许推进的下一步 | 仍需获得的证据 |
| --- | --- | --- |
| A/AAAA 地址候选 | 尝试路由和建连 | 地址有效、下一跳、正反路径 |
| TCP 到所选 F1 | 开始本连接的身份验证 | 预期服务身份及完整 TLS 握手 |
| TLS 身份验证通过 | 按协议发送请求 | 客户端授权、L7 选路、上游独立安全边界 |
| A 在发现集合且健康 | 按当前策略考虑 A | 本请求准入、依赖、有效提交者资格 |
| 请求已被接受 | 执行业务合同 | 原子持久提交和完整终态 |
| 服务端已持久提交 | 返回或恢复原结果 | 客户端收到可信、完整、可关联的终态 |

### 0.2 让重试有明确含义的最小业务合同

本例 CreateOrder 恰有 `sku=A`、`qty=1` 两字段，拒绝重复字段后按项目规则规范化；请求键为 k-17，去重作用域是“已授权租户 + CreateOrder + 键”。同键异参拒绝。订单效果和完整终态结果写入同一个可原子持久提交的存储事务，唯一约束与并发串行化只允许一个有效提交者，提交边界拒绝失效执行者。执行中重复请求等待或返回明确 pending；本例没有支付、邮件等事务外副作用。

提交前崩溃回滚本事务所有写入；提交后订单 o-42 与终态一起保存。成功终态从提交起保留 24 小时，pending 不会因这段时间过去就被遗忘并重新执行。所有可接收恢复请求的后端，在保留期内遇到同作用域、同键、同规范化参数，必须读取权威已提交终态并返回原结果，不能另建订单。仅仅“拒绝重复提交”的接口没有承诺返回原结果，不足以完成这里的恢复路径；权威读取不可用时只能失败或明确报告 pending/unknown。

客户端收到完整、可信、可关联到本请求的成功终态 o-42，且服务合同规定它表示该事务已持久提交，才记为 confirmed。仅看 HTTP 状态码不够；完整 202 也可能只表示 pending。若 A 提交后回复丢失，客户端只能记 unknown，不能从超时推断“订单失败”。唯一提交、结果复用和结果在网络中的交付是三个条件，前两个不保证回复在某个时限内到达。

这样就能定位反例：有 AAAA 而默认路由失效，停在路由；TCP 通而证书名称不符，停在身份；健康检查通过而写依赖失效，停在业务；网络均正常而租户未获授权，停在授权；提交后丢回复则进入结果恢复。后面的摘流、跨区和预算只改变到达路径或等待方式，不自动撤销已提交效果。更一般的协议分层与确认边界见[网络分层协议与工程实践](01-网络分层协议与工程实践.md)，本例所需前提已在这里自含给出。

## 1. IPv6：有地址以后，还缺哪些状态

### 1.1 地址结构、作用域和下一跳

IPv6 地址宽度为 128 位。给定站点前缀 `2001:db8:10::/48`，本项目分配 16 位子网号 1，再给 64 位接口标识 `::20`，可得 `2001:db8:10:1::20`，即 48+16+64=128。前缀用于路由聚合，子网号区分本规划中的链路，接口标识区分该子网中的地址；这是明确的地址规划，不是所有 IPv6 地址都能按同一业务字段解释。地址写得出来不代表已配置或可达。[RFC4291 §2、§2.4、§2.5.4](https://www.rfc-editor.org/rfc/rfc4291.html#section-2)

链路本地地址的识别前缀是 `FE80::/10`；RFC4291 §2.5.6 所示标准格式还要求后续 54 位为零，再接 64 位接口标识，不能把 /10 中任意位组合都当成该格式。普通 link-local 数据不能被路由器转发到另一链路。C 的 eth0、eth1 所接的两个不同链路都可能有 `fe80::1`。本例没有可消除歧义的 default zone，访问 eth0 的路由器需把目标记作 `fe80::1%eth0`；zone 是节点本地的链路标识，具体 API 接受接口名还是数字由平台决定。若实现已有无歧义默认 zone，则可省略文字后缀，不应断言“没有 % 就必失败”。这也不是浏览器 URL 输入格式教程。[RFC4291 §2.5.6](https://www.rfc-editor.org/rfc/rfc4291.html#section-2.5.6)、[RFC4007 §11](https://www.rfc-editor.org/rfc/rfc4007.html#section-11)

给定目标 `2001:db8:10::30` 已被判为 on-link，但邻居链路层地址未知，C 向由目标地址确定的 solicited-node 组播地址发送 NS；题设目标的 NA 成功给出链路层地址，才得到本链路下一跳解析资料。对于 off-link 目的，需解析的是已选路由器的下一跳，而不是向远方目标做本链路解析。IPv6 没有广播，仍有面向特定接收集合的组播；ND 除地址解析还含路由器发现和邻居可达性维护，不能只理解成“ARP 改名”。若抽象邻居缓存容量只有 1，已占 1 且不能驱逐，新解析就可能因局部资源失败，即使路由与 PIO 都在。[RFC4861 §2.3、§3.1](https://www.rfc-editor.org/rfc/rfc4861.html#section-3.1)

接口 MTU、MAC 和邻居项须带接口、命名空间、观察时刻。MAC 是这段链路的地址资料，不是应用账户身份；表项存在或标为 STALE，也不能直接当作远端应用活着或已死的证据。STALE 的含义是缺少当前可达性确认，而非确认不可达。路由表决定“下一跳选谁”，邻居解析解决“怎样在此链路发送”，返回路径仍需另给条件。[RFC4861 §5.1 状态定义](https://www.rfc-editor.org/rfc/rfc4861.html#section-5.1)

### 1.2 RA 的三个寿命不能互相续期

给一台新接入的以太网客户端，不设其他地址或路由来源。t0 收到合法 RA：Router Lifetime=1800 秒，PIO 前缀 `2001:db8:10::/64`、L=1、A=1、preferred=600 秒、valid=3600 秒；前缀与接口标识长度匹配，接口标识已给定为 `::20`。DAD 在 t1 成功，此后没有新 RA、管理变更、重启或额外安全扩展改变状态。

Router Lifetime 只维护该 router 的默认路由候选资格；PIO 的 L 决定是否提供 on-link 信息，A 决定是否让此前缀参与 SLAAC。PIO valid 一方面供 ND 维护 on-link 前缀有效期，另一方面供 SLAAC 按自身规则维护地址寿命；preferred 则决定地址是否仍适合优先用于新通信。因此同一 RA 中包含这些字段，并不使它们变成一个计时器。

| 时刻或独立替换输入 | 可推导的状态 | 不能推出的结论 |
| --- | --- | --- |
| t1，DAD 完成 | 地址 `2001:db8:10::20` 可用；有 on-link 前缀和默认 router 候选 | 全球正反路由、TCP443、TLS 已通过 |
| t601 | 地址 deprecated，仍 valid；若有容易使用的合适非 deprecated 地址，优先不用旧地址发起新通信，既有通信可继续使用 | 地址立即删除或全部连接立即关闭 |
| t1801 | 该默认 router 寿命已过；地址仍 valid，on-link 信息仍有自己的期限 | 地址 valid 会续默认路由，或同链路目的也必然不通 |
| t3601 | 地址已不再 valid，不能继续当有效地址使用 | 某个系统的 socket 报错时刻或业务已回滚 |
| 独立新 RA：Router Lifetime=0，其余相同 | 不把该 router 加入默认路由表；仍可能由 PIO 得到地址和 on-link 前缀 | A=1 就一定有默认路由 |
| 独立新 PIO：L=0/A=1，其余相同 | A 仍可参与 SLAAC；L=0 本身不作 on-link 或 off-link 断言 | L=0 禁止通信或撤销先前 on-link 信息 |
| 独立新 PIO：L=1/A=0 | 可得到 on-link 信息，但不能靠该 PIO 自主生成地址 | L=1 就产生 SLAAC 地址 |

这里用的是新配置的简单寿命。更新既有 SLAAC 地址时，不能把新 valid=0 直接解释为旧地址立即消失：RFC4862 §5.5.3 还按原剩余寿命、两小时保护和 RA 是否经过协议认证选择分支。只说“来自受信路由器”或“格式合法”不足以取得该认证例外。ND 对 on-link 前缀的处理和 SLAAC 地址处理也不能互相套用；缺这些输入时停止撤销推导。[RFC4861 §4.2、§4.6.2、§6.3.4](https://www.rfc-editor.org/rfc/rfc4861.html#section-4.2)、[RFC4862 §5.5.3–5.5.4](https://www.rfc-editor.org/rfc/rfc4862.html#section-5.5.3)

DHCPv6 可分配地址、前缀或其他配置，但仅取得 IA_NA/IA_TA 地址不能推导 on-link 前缀；一般也不能把地址分配视为本例 RA 默认 router 发现的替代。应分别记录 RA 和 DHCPv6 策略、接收内容及其权威。本文按 RFC8415 的这些职责选段讨论，不断言所有后续 DHCPv6 扩展永远不能携带路由信息。[RFC8415 §1、§3、§18.2.10.1](https://www.rfc-editor.org/rfc/rfc8415.html#section-18.2.10.1)

### 1.3 DAD、入口防护与隐私各解决一个问题

候选地址先处于 tentative 状态。按本题 DAD 规则收到同链路冲突信息时，不能把它像成功地址一样投入使用，后续重选或报错还取决于生成方式与实现。DAD 未观测到冲突只描述本链路的一次检测，不能排除分区、丢包、日后冲突或另一链路上同字面地址；更不是全球唯一或端到端可达证明。ND/NUD 的邻居可达判断同样止于邻居和前向路径状态。[RFC4862 §2、§5.4](https://www.rfc-editor.org/rfc/rfc4862.html#section-5.4)

RA Guard 的作用可以用一个静态入口例说明：已配置交换设备位于所有相关 L2 路径上，仅 pR 连接获准路由器，且设备能完整识别本例 RA 并执行策略，则来自 pR 的合法 RA 可通过、pH 的 RA 被阻止。若存在绕过该设备的同链路路径、隧道或识别能力缺口，仅“已启用 RA Guard”不能证明全路径受保护。它控制路由通告入口，不替代地址寿命、TLS 或租户授权；这里没有设备配置或攻击构造。[RFC6105 §2–3、§5](https://www.rfc-editor.org/rfc/rfc6105.html#section-2)、[RFC7113 摘要及 §1](https://www.rfc-editor.org/rfc/rfc7113.html#section-1)

临时地址则改变关联窗口。某应用两次新连接按给定策略分别使用 a1、a2，却都以同一已验证账户访问，仅按源 IP 会把同一主体拆开。轮换地址可缩短只靠固定地址关联的时间，但账户、cookie 等仍可关联两次活动。日志需要区分连接源与已授权主体，隐私扩展不等于匿名保证，也不更改上一节主例已给定的寿命。本篇未展开 IID 生成算法。[RFC8981 §1、§1.2、§2.2](https://www.rfc-editor.org/rfc/rfc8981.html#section-1)

### 1.4 大包为何可能在握手成功后失败

给定首跳 MTU1500，源发送总长 1400 字节的 IPv6 包，下游链路 MTU1280，无隧道或扩展头。中间 IPv6 router 不能替源分片，发回合法 Packet Too Big，MTU=1280；本例已明确它能关联该流并通过验证和策略。源依 PMTU 信息减小后续包，在 IPv6 头40、TCP头20、没有其他 option 的简化布局下，TCP payload 上限为 1280−40−20=1220 字节。这是包尺寸算术，不承诺一次应用 write 如何切分或一定送达。[RFC8200 §4.5、§5](https://www.rfc-editor.org/rfc/rfc8200.html#section-4.5)、[RFC8201 §3–4、§6](https://www.rfc-editor.org/rfc/rfc8201.html#section-3)

若该 PTB 被阻断，大包可能持续失败，小包或 TCP 握手却正常；“大包失败”也不是 PMTU 黑洞的唯一诊断。伪造、无法关联的 PTB 不能不加判断就当真实路径变化。应保留必要的 ND/PTB 等控制功能，同时按消息类型、地址族、方向、来源与速率制定具体边界，不能把“不要全挡 ICMPv6”改成无条件全部放行。

独立策略例中，云安全组允许 IPv6 TCP443，而主机规则拒绝同一流，最终仍拒绝；只看到 IPv4 允许项或业务端口允许项，也推不出 RA、ND、PTB 通过。记录丢弃计数要关联接口、规则点、时间及对应流。当前证据若只含一个小包或某 hop 沉默，就停在该观察范围，不从中编造全路径 MTU、防火墙状态或服务结论。

## 2. Happy Eyeballs 竞争的是连接机会

客户端不应被某一地址族迟迟不完成的尝试拖住，但不能为抢时间把有副作用的业务向两边各发一次。RFC8305 的主线是异步 A/AAAA 解析、目的候选排序与交错、错开发起连接，建立一条连接后取消不再需要的竞争尝试；目的排序也不替代针对每个目的的源地址选择。[RFC8305 §2–5、§9](https://www.rfc-editor.org/rfc/rfc8305.html#section-2)

### 2.1 50 毫秒解析等待与 250 毫秒连接间隔

本例无可复用连接，无 0-RTT 或 TFO 业务发送。t0 并行启动 A/AAAA 查询；A 在 t10ms 返回，AAAA 在 t30ms 返回。Resolution Delay=50ms 从 A 到达起算，截止 t60；AAAA 在窗口内到达。题设排序后的候选只有 `[v6,v4]`，Connection Attempt Delay=250ms，v6 一直 pending，v4 建连需要20ms；另给定胜出连接的全部 TLS 验证在 t340 完成。

| t（ms） | 状态变化 | 此刻允许的结论 |
| ---: | --- | --- |
| 0 | 两种解析启动 | 未发业务 |
| 10 | A 返回，等待短解析窗口 | 尚不能宣称 IPv6 失败 |
| 30 | AAAA 返回，立即开始 v6 连接 | 已开始一个候选尝试 |
| 280 | 250ms 后 v6 仍 pending，启动 v4 | 两个连接尝试可重叠 |
| 300 | v4 建连成功，选中并取消无须继续的竞争尝试 | 只证明 TCP 胜出 |
| 340 | 本例 TLS 全部验证成功后只发送一次业务 | 才进入第0节应用链 |

独立替换：若 v6 在 t40 明确连接失败，可更快开始下一候选，不必等 t280；本例与 t30 已间隔10ms，不能据此忽略 RFC 的最小尝试间距。若 v4 在 t300 胜出，但证书 SAN 只有 `other.example.test`，业务应停在身份校验，不能关闭校验继续 POST。把 POST 同时发往 v6/v4 不是这里的连接竞速，会新增业务重放问题。

50/250ms 是本题采用的 RFC 推荐量级，不是通用端到端预算。Envoy v1.31.0 所选连接池文档对上游 TCP 描述的是 300ms，且关联 LOGICAL_DNS 的 ALL 解析或 EDS 的 additional_addresses；本文没有验证其实际配置或实现时序。解析、连接、TLS、请求处理分别计时，连接复用时某些阶段根本不会重新发生。[Envoy v1.31.0 connection_pooling.rst，Happy Eyeballs Support](https://github.com/envoyproxy/envoy/blob/v1.31.0/docs/root/intro/arch_overview/upstream/connection_pooling.rst#happy-eyeballs-support)

### 2.2 成功率要注明候选、连接还是请求

独立给定同一窗口内10次调用都成功回退 IPv4，且收到业务成功终态；10个 IPv6 尝试均未在胜出前建连，10个 IPv4 尝试均建连成功。请求终态成功率可为10/10，而 IPv6“在本次竞速窗口内建连成功”为0/10。被取消的 pending 不等于最终必然无法建连，也不能把这一小组纸面输入当 IPv6 故障率估计。

要比较地址选择策略，分别记录启动了多少候选、哪些主动失败或被取消、连接结果以及业务终态；把题设 v6 延迟增加100ms或丢失若干报文，只能重算相应阶段，不等于真实故障注入。SYN 重传、丢包、到达时间抖动也要各用自己的观察对象，不能仅凭某一现象断定根因或把 HTTP 成功掩盖为两种地址族都正常。

## 3. NAT：转换、入站许可和资源寿命分开看

### 3.1 一份外部映射不等于任意对端可达

给定 UDP 内端 C=`192.0.2.20:50000` 先向 S1=`203.0.113.10:3478` 发包，外侧所见源变成 x=`198.51.100.7:40000`、目的不变，这描述本例 SNAT/PAT；匹配回包的目的则翻回 C。另给内端 `192.0.2.21:50000` 映到同一外部地址的40001端口，可见 PAT 如何用端口及元组区分内部端点。独立入站例将 `198.51.100.7:8443` 映到 `192.0.2.30:443`，改写目的的是 DNAT；相应回程匹配另作题设，不能从一行规则存在推出报文已经命中或应用已成功。

沿 C 的 UDP 例，再向 S2=`203.0.113.20:3478` 发送。题设 mapping 是 endpoint-independent，活跃期间不同目的均复用 x；filtering 是 address-dependent，只放行 C 曾发往的对端 IP，无其他 ACL、双重 NAT 或资源失败。S1/S2 因而看到同一 x；来自 `203.0.113.10:9000` 也满足 IP 级过滤，而来自 `203.0.113.30:3478` 不满足。转换映射决定“外面看见哪个元组”，过滤决定“谁的返回包能进来”，是两条独立轴。[RFC4787 §4.1、§5](https://www.rfc-editor.org/rfc/rfc4787.html#section-4.1)

只将 mapping 改成按对端 IP:port 区分，S2 就可能看到外端40001；只将 filtering 改成 IP:port 级，S1 的9000端口回包就会被拒绝，即使 mapping 完全未变。因此旧称“对称 NAT”不足以给出完整行为或打洞成功概率。没有映射或不满足过滤时，外部主动入站可能无法到达内端，应用不能假定内网地址具有公网端到端可达性。

STUN 的 XOR-MAPPED-ADDRESS=x 是对应服务器对该事务所见反射地址，不能由它推断陌生 peer 已获入站许可、映射不会改变或媒体已连通；XOR 编码本身也不是地址加密或身份认证。旧方案的 idle=30/120/600秒是计划采样点，没有结果就不能说映射实际活到这些时刻。UDP 刷新方向、回收策略和 TCP session 超时必须分别给条件，TCP TIME_WAIT 更不能套为 UDP 的状态。[RFC8489 §1–2、§14.2](https://www.rfc-editor.org/rfc/rfc8489.html#section-2)、[RFC4787 §4.3](https://www.rfc-editor.org/rfc/rfc4787.html#section-4.3)、[RFC5382 §4.1、§5](https://www.rfc-editor.org/rfc/rfc5382.html#section-5)

### 3.2 穿越要经历候选检查，中继还要保持资源

给定一组已经写在纸上的 ICE 检查结果：host 候选直连失败，STUN 得到的 server-reflexive 候选与 peer 检查仍失败，relay 候选检查成功且被本例流程选为传输路径。于是可选择中继，不是因为“知道了公网地址”就自动打洞成功。ICE 的候选对检查、认证和路径选择承担了 STUN 地址观察以外的职责。本例策略主动规定选定路径后再发业务；RFC8445 §12.1 本身允许选定完成前在 valid pair 上发送，不能把本题策略改说成协议普遍禁止。[RFC8445 §2.2–2.3、§7.2.2、§8.1.1、§12.1](https://www.rfc-editor.org/rfc/rfc8445.html#section-12.1)

TURN 把数据交给 relay 后再转向 peer，占用 allocation 状态及中继入/出带宽。未来的容量、费用与路径时延应按中继链路计量，不能用直连样本替代，也无需假定每条现实中继路径一定更慢。给一个更小的寿命例：t0 获授权 allocation 至 t600，peer-IP permission 至 t300，针对 peer IP:port 的 channel binding 至 t600，期间没有刷新事务。t301 虽有 allocation 和 channel，peer→relay→client 仍因 permission 过期被丢弃。permission 以 IP 为键，channel 以 IP:port 为目标，二者不能合成同一端口许可。[RFC8656 §6、§9、§11.3、§12.6–12.7](https://www.rfc-editor.org/rfc/rfc8656.html#section-9)

ChannelData 本身不续 allocation、permission 或 channel 的寿命。permission 到期的上述结论针对 peer 入站路径，不能凭它宣布两方向在同一步都必然断开；client→server ChannelData 另按其处理规则判断。已选中的候选对也不是永久可达证明：缺后续刷新、同意或检查证据时，结论止于给定时刻。

### 3.3 同一条新流可能受四种不同资源约束

下面是四个独立的给定池，数量不能交叉相减。CGN 资料把端口限额、状态内存和分配速率分开要求，本机端口与 TCP 关闭状态又有自己的对象；现实容量必须先明确映射、元组复用、配额和生命周期。[RFC6888 §3 REQ-4、REQ-5](https://www.rfc-editor.org/rfc/rfc6888.html#section-3)

| 对象 | 题设与纸算 | 不能偷换的单位 |
| --- | --- | --- |
| 外部 PAT 分配 | 2个公网地址，各限定100可用端口，禁止跨目的复用；已占190，剩 2×100−190=10 | 不是以2×65535直接得出所有连接上限 |
| conntrack 表 | 上限512项，已有400，每新流恰占1项，剩112 | 跟踪可能不做 NAT；112不是剩余PAT端口 |
| 本机临时端口 | 候选100，其中90按当前元组规则占用，另5保留且不重叠，剩5 | 源地址、绑定和目的复用规则未给时不能套数 |
| TCP TIME_WAIT | 本机有40个此状态对象 | 不代表40个活跃请求、PAT分配或必须清除的障碍；UDP没有此TCP状态 |

只有再给定“一次新建动作对前三池一一占用、没有其他限制”，最小剩余额度才是5；这仍不是实测可建5条。若请求在1秒内需要新建21个映射而独立分配率限额为每秒20，即使存量池有余量，也可能被速率限制。此21/20是额外容量反例，不是设备默认。

端口耗尽可能造成建连失败，但间歇失败不能反向唯一诊断为端口耗尽。要区分 NAT 外部分配失败、本机端口失败、跟踪状态满载及其他监听/资源问题；全表满也不能在没有实现规则时推断所有既有流同时受损。跟踪项回收只改变网络状态，不证明业务已取消或回滚。使用率的分母、峰值窗口与分配失败事件比一个平均百分比更能界定容量；没有这些输入就停止容量结论。

### 3.4 hairpin、显式映射和 keepalive 的用途

同 NAT 内的 X1 访问 X2 活跃外部映射 `198.51.100.7:8443`，题设设备支持 hairpin 与对应回程，报文经转换到达 X2=`192.0.2.30:443`。这是内部主机经外部映射回到内部的路径，与任意公网客户端获准入站不同。RFC4787 的要求说明设计职责，不能认证任意现实设备已经支持该路径。[RFC4787 §6](https://www.rfc-editor.org/rfc/rfc4787.html#section-6)

显式映射把“等待出站形成状态”改为申请外部(protocol,port)到内部(protocol,port)的映射。PCP 的职责包括创建映射以及获知、影响其期限，公布候选给其他参与者的 rendezvous 仍由应用承担。审查至少要有申请者、实际分配结果、获准内部目标、期限和续约/删除责任，不能把映射管理自动化当作所有申请都安全。UPnP 保留为另一映射管理机制的选型项；本轮没审其具体版本，不能把 PCP 的 opcode、认证或寿命细节套过去。协议/版本、谁能请求、目标和期限不明确时停止配置选择。[RFC6887 §1](https://www.rfc-editor.org/rfc/rfc6887.html#section-1)

再给一个 keepalive 反例：应用的 HTTP 连接对象可复用，但本题 NAT 映射只在 outbound 数据真正穿过时续期，单纯称它为 HTTP keepalive 不会产生这个包。TCP keepalive、应用心跳、NAT refresh、TURN allocation Refresh 各作用于自己的对象；看到其中一个定时器“已开”不能推出另外三个仍有效。保活本身会消耗包和状态预算。自然 idle 过期、人工回收和设备重启还属于不同失效原因，不能用一次给定回收代替全部寿命分析。

### 3.5 透传来源须保留可信链，不能把它当租户身份

PROXY protocol 在连接开头陈述连接元信息，不自带协商握手或密码认证。独立正例规定：一条上游连接只陈述一条原客户端连接的来源；专用入口仅允许经本题认证的代理 F 到达，代理→入口传输完整性与接入控制均成立。F 按自己实际接受的连接生成源地址 `203.0.113.7`，后端按预先约定的版本和格式完整解析头一次，再处理后续协议。由此可记录“经 F 陈述的原连接源”，并同时保留真正 direct peer=F 和可信链。[HAProxy v3.0.0 PROXY 文档 §2、§5](https://github.com/haproxy/haproxy/blob/v3.0.0/doc/proxy-protocol.txt)

若不可信客户端能直达同一入口并自行写出相同字节，解析成功不能让陈述变真；自动猜测有无 PROXY 头也破坏入口边界。多原客户端复用一条上游连接时，一个连接级头不能归属其全部请求，所以不能把此例套在第0节跨客户端复用的 L7 池上。HTTP Forwarded/X-Forwarded-For 应按可信边界剥离、重建或明确的链规则解释，不能任取最左地址授权。即使来源链可靠，也仍需独立验证 tenant-a 的身份与 CreateOrder 权限；前后端源地址对账应分“直接 peer、代理陈述、应用身份”三栏。

## 4. 发现、缓存、连接池与租约有各自的时钟

### 4.1 先形成候选，再决定能否接收请求

服务发现把逻辑名称关联到实例或入口；名字解析、注册中心查询与控制面推送只是取得候选的不同方式。给定逻辑名 orders 的快照：A=(v2,east,h2)、B=(v1,west,h2)、C=(v2,east,h2)。本次要求 v2/h2 且只允许 east，先保留 A/C，再依据健康、准入和选择策略决定发给谁。标签只是一份声明，不能证明进程真实能力、服务身份或租户权限。

pull 查询要等本地下一次成功刷新，watch 推送也须经过传递、接收和按版本应用快照。注册权威、resolver、客户端、本地 LB 各自可能持有不同版本，不能从“支持 watch”推出零延迟一致性。控制面暂不可用可能阻止新增候选，而旧池仍继续服务；缓存中有错误值、缓存正常过期、无可用刷新响应和进程失败也要分开诊断。

A/AAAA 只给地址。SRV 另外表达 target、port、priority、weight，是否采用取决于应用协议；普通 HTTPS 的第0节并未假设客户端自动查 SRV，SRV 的选择规则也不能用后文 WRR 纸例代替。DNS 的地域答复还可能基于递归解析器的位置或特定策略，递归器不必与终端同地域，所以“east VIP”只是本题给定路由输入，不证明最小用户 RTT。[RFC2782，Applicability Statement 与 SRV 字段](https://datatracker.ietf.org/doc/html/rfc2782)

短 TTL 的代价也有分母。假定100个彼此独立的 resolver 缓存同一记录，持续有请求，无共享层、预取、提前驱逐或上下限，过期后立即成功重查；忽略边界瞬时波动，TTL30时长期查询率约100/30次每秒，TTL5时约100/5。若100个客户端共用一个 resolver，上游分母变成1。TTL 变短可能增加查询与控制面压力，但不能无条件说 TTL 减半 QPS 必翻倍；原5/30/300秒方案应分别记录实现策略与命中率。

### 4.2 TTL 到期没有把旧 TCP 连接搬到新地址

以下 A/B 只作为两个地址符号，不指第0节的业务后端。给定普通缓存没有提前刷新、驱逐、额外 TTL 上下限或 stale：resolver R 在 t0 获 VIP=A、TTL30；t20 权威改为 B；C 在 t10 从 R 得剩余 TTL20。R 和 C 的这份缓存都到 t30 过期，t25 仍可能指向 A。权威写入不会隔空修改缓存；到 t30 没有新响应，也不能编造“已经解析到 B”。TTL 定义及其 stale 更新应分别阅读。[RFC1035 §3.2.1、§4.1.3](https://www.rfc-editor.org/rfc/rfc1035.html#section-3.2.1)、[RFC8767 §4](https://www.rfc-editor.org/rfc/rfc8767.html#section-4)

另给连接池输入：C 在 t25 用 A 建立可复用连接，连接保持到 t90，系统没有 DNS→pool 强制失效联动。即使 t31 新解析得到 B，旧连接仍能承载请求；一个新请求未必重新做 DNS。因而 stale pool 指的是池所保留的旧目标状态，不等于 DNS 此刻一定过期。DNS 缓存命中、连接复用和业务缓存命中也必须分开记。

负缓存独立例固定查询 `(api.example.test,A,IN)`，无 CNAME：t0 得权威 NXDOMAIN，SOA TTL600、MINIMUM300，本题接受缓存，无本地限额或 stale。负 TTL=min(600,300)=300秒；t60创建 A 记录，t120仍可回答 NXDOMAIN，剩余180秒。NXDOMAIN 的缓存键是 QNAME/QCLASS；若替换为 NOERROR/NODATA，则缓存只针对 QNAME/QTYPE/QCLASS，此处不能推出名称整体或 AAAA 不存在。到 t300只说明旧负缓存不能再按未过期使用，不保证下一查询成功。[RFC2308 §5–6](https://www.rfc-editor.org/rfc/rfc2308.html#section-5)

SERVFAIL 是解析失败，timeout 是没有及时结果，都不能当成名称不存在；实现可有不同失败缓存政策，例如 CoreDNS v1.11.0 文档单列 servfail 缓存。解析日志必须保留 QNAME/QTYPE/QCLASS、RCODE、查询层、接收时刻和 TTL；没有查询日志可能来自本地命中或采集覆盖不足，不能据此认定请求从未发生。

stale 则需要两个互不混用的例：

- RFC8767 例已支持并启用 stale，过期旧记录仍在最大 stale 保留期内，且满足客户端响应计时、查询总计时、失败重检等条件，未能及时刷新时可先回旧值、继续后台刷新；返回的过期记录 TTL 必须大于0，例如推荐的30秒。来自权威、RCODE 为 NoError 或 NXDOMAIN 且 AA=1 的答复，才按该节的判据刷新 resolver 状态。这里 AA=1 限定收到的权威刷新答复，不是要求给 stale 回复设置 AA。[RFC8767 §4–5](https://www.rfc-editor.org/rfc/rfc8767.html#section-5)
- CoreDNS v1.11.0 的 cache 文档例明确启用 serve_stale、旧记录仍在 DURATION 内：immediate 先返回旧记录再刷新，文档规定响应 TTL=0；verify 先检查能否从源取得该记录的更新答复；若能取得，便不使用该 stale 条目答复。这个具体版本文档与上一 RFC 例的正 TTL 不能合成一个值；此处没有用未读实现代码作合规裁决，也不修改别篇采用的 RFC 题设。[CoreDNS v1.11.0 cache README，Syntax](https://github.com/coredns/coredns/blob/v1.11.0/plugin/cache/README.md#syntax)

stale 扩大可能看到旧值的窗口，却不保证无限可用；TTL 上下限、预取、驱逐和刷新失败也会改变实际轨迹。没有具体 resolver、客户端及其策略，就只能列出这些未知，不能用记录 TTL 单独给出故障恢复上界。

### 4.3 Envoy v1.31.0 的发现类型会改变池的去留

下面逐行比较固定版本文档或 API 注释给出的条件，不假定真实部署采用了它们：

| 类型与明确设置 | 给定变化后的文档语义 |
| --- | --- |
| STRICT_DNS，关闭 active health | 返回每个 IP 被当作显式 host；成功结果从[A,B]变[B]，移除 A 并 drain 已有 pool；成功空结果意味着无 host |
| LOGICAL_DNS，关闭 active health | 服务发现概览用“新建连接时取返回的首个IP”解释逻辑pool，不把全部返回IP当作独立LB成员；若DNS lookup family=ALL启用上游TCP Happy Eyeballs，同版连接池文档规定会排序地址，先尝试首个，失败或持续连接300ms后尝试后续地址（见第2.1节）。单逻辑pool可同时保留连向不同IP的物理连接；DNS更新本身不drain，成功空结果也一样 |
| EDS/最终一致发现，开启 active health，ignore_health_on_host_removal=false | EDS 从 gRPC/REST-JSON xDS 管理服务获得 endpoint 及权重/zone 等属性。A 已从发现移除却仍 health OK 时，所选文档的 Absent/Health OK 例仍向 A 路由 |
| 上一行改为 ignore_health_on_host_removal=true | cluster.proto 注释规定移除不再等待 A 变 unhealthy；这尚未证明全部旧请求已停止 |
| tcp_proxy 且 close_connections_on_host_health_failure=true | 注释描述健康失败立即关闭连接，但开关适用范围限 tcp_proxy，不能外推给所有 HTTP pool |

LOGICAL_DNS 的“更新不 drain”仅说明该触发原因，不禁止超时、GOAWAY 或其他机制关连接。tcp_proxy 字段的适用限制也不能反推 HTTP pool 在健康失败时永不关闭：独立的连接池文档明确描述 host 从 available→unavailable 时关闭 pool connections。DNS 刷新还受 respect_dns_ttl、refresh rate、failure refresh 等选项影响，不能把每条 TTL 当所有 cluster 的刷新周期。未给发现类型、health flag 或过滤器时，就不能回答“删除实例会不会立即断流”。[Envoy v1.31.0 service_discovery.rst，Strict/Logical DNS、EDS、最终一致发现](https://github.com/envoyproxy/envoy/blob/v1.31.0/docs/root/intro/arch_overview/upstream/service_discovery.rst)、[connection_pooling.rst，Health checking interactions](https://github.com/envoyproxy/envoy/blob/v1.31.0/docs/root/intro/arch_overview/upstream/connection_pooling.rst#health-checking-interactions)、[cluster.proto，字段31/32](https://github.com/envoyproxy/envoy/blob/v1.31.0/api/envoy/config/cluster/v3/cluster.proto#L1109-L1127)

### 4.4 注册 epoch 不能自动阻止旧 worker 提交

设唯一注册权威 R 使用自己的单调时钟管理 `(instance=A,epoch=7,expiry=130)`，t100已生效。续约必须在 R 的时间严格小于130且 epoch 仍为当前时被接受；客户端只发出 renew，没有收到接受结果，不等于已经续期。t130未续就失效，t131允许新 owner epoch8，t132到达旧 epoch7 回调。R 对更新和迟到回调都比较当前 epoch，因此7不能覆盖8；它不靠两机 wall clock 的差值裁定租约。

但是旧 A 的进程、线程、TCP 和已提交订单仍可能存在。如果业务写入只检查“我曾经有注册”，旧 worker 仍可能造成效果。要拒绝旧7提交，项目必须额外规定提交边界在原子事务内核对当前有效提交资格/epoch8，并与效果和终态写入保持原子性；检查之后再异步提交、其间允许资格变化，不满足这一前提。注册 generation 与业务提交 fence 是两个责任，只有明确接通才有后一结论。

这整个小状态策略是抽象项目合同，不冠名 Consul、etcd 或任何真实实现保证。若回调只按 instance ID 更新，会复活旧“healthy/renewed”状态；若本地钟还没到130，也不能证明 R 的租约有效。缺时钟权威、接受条件、传播或提交检查时，停止“无双活”推断，而不是从租约名词补出缺失机制。

## 5. 健康检测之后，还有传播、准入和退场

### 5.1 连续三次失败要等到哪一刻

健康检查是沿某条路径、在某个时刻、按特定成功条件得到的观察。连接检查、HTTP 响应检查或依赖检查覆盖不同故障，不能用“健康”一个词替代业务可提交。Envoy v1.31.0 的健康文档也分别列检查类型、间隔和失败/成功阈值，并单列 health identity 问题；同一 IP 重新出现可能已经是另一服务。[Envoy v1.31.0 health_checking.rst](https://github.com/envoyproxy/envoy/blob/v1.31.0/docs/root/intro/arch_overview/upstream/health_checking.rst)

给定探测在全局纸面单调轴 t0/10/20/30秒启动；t0通过，A在t0.1坏掉。以后每次失败等2秒 timeout，无调度抖动、快速失败或中间成功；连续3失败才标 unhealthy，状态传播到目标 LB 另需3秒。DNS TTL独立给5秒。

| 事件 | 时刻（秒） | 连续计数/可得结论 |
| --- | ---: | --- |
| 第一失败探测完成 | 12 | 1次失败 |
| 第二失败探测完成 | 22 | 2次失败 |
| 第三失败探测完成 | 32 | 3次失败，判 unhealthy |
| 目标 LB 获得新状态 | 35 | 仅这些 LB 的健康视图已改变 |

从实际故障到该视图变化为35−0.1=34.9秒，已大于 TTL5+interval10=15秒，所以“健康收敛总小于 TTL 与探测间隔之和”不成立。客户端缓存、旧连接池、准入和在途请求都还没有计入；35秒不能改叫用户恢复 RTO。

恢复另给输入：A从t60起正常，t60/70/80/90/100启动检查，每次0.1秒成功，要求连续5成功，再传播3秒，则t100.1判healthy、t103.1目标LB看到。若一次失败插入，连续成功计数重置；若周期改为“上次完成后再等10秒”，整条表要重算。即便视图恢复，隔离期限、准入或慢启动也可能尚未满足，103.1不是满负载时刻。

另一个独立反例是 t30 的本地 `/health` 回200，但订单写依赖坏了；这不属于上表 t30超时的同一轨迹。反向误报也可能发生：A能处理订单，专用于检查的 monitor 路由坏了，三次超时会误摘A，损失可用容量。检查越深并非一定越好；把共同的非必需外部依赖作为所有实例的摘除条件，可能造成同时误摘。默认探测应无真实业务副作用，若未来确需写 probe，要另有隔离数据、幂等、权限和清理合同。

连续失败/成功阈值、隔离期能抑制 flapping，却增加检测或恢复等待，不能只增大数字而不看代价。旧“三失败、五成功、70%、RTO<30秒”是项目政策例，无协议提供这些通用承诺。

### 5.2 从停止新选择到执行者退出

独立计划同时让后端A和入口F1退场，F2/B满足第0节业务合同。下表给定每个控制点真的完成其事件，不能省掉中间层再相加成为通用 RTO 公式：

| 时刻（秒） | 给定事件 | 这一事件管得住什么 |
| ---: | --- | --- |
| 40 | R删除A注册 | 权威候选集合；旧缓存、连接和进程仍可能在 |
| 43 | 本题所有L7收到v8，禁止新选A | 这些L7的新请求选择；其他客户端视图另算 |
| 44 | A原子关闭业务准入屏障，拒绝旧epoch的新任务 | 所覆盖入口的新任务；屏障前任务仍可执行 |
| 45 | L4的新连接集合排除F1 | 新TCP连接；既有TCP仍归F1 |
| 46 | F1发送且C已收到有效HTTP/2 GOAWAY，最终Last-Stream-ID=3 | 接收方不再在原连接开新stream；1/3可能处理或继续完成，5在此连接未处理 |
| 50 | stream1已提交o-42但回复丢失，stream3仍running | 1在客户端unknown，3尚无终态 |
| 55 | 本题drain deadline到，仍有任务和连接 | 可按已定义政策停止等待/关连接；不能自动宣称任务退出、回滚或内存可释放 |
| 60 | 本题所有resolver/client完成新发现 | 这些参与者的新选址视图；不证明老任务终态 |
| 65 | 明确获得剩余任务终态及执行者退出证据 | 才能按资源合同完成任务退场和回收；unknown仍需对账 |

GOAWAY 例假设对端遵守 RFC9113，最终边界不小于任何已送上层的 stream。不能先让5提交，再声称 Last-Stream-ID=3证明5未处理；只有大于有效边界的5可以按协议及预算在新连接恢复。小于等于边界的1/3既不是全部成功，也不是全部失败。没有 GOAWAY 或只有普通 RST/timeout，不能套未处理结论；协议中的 REFUSED_STREAM 还需符合其“未交给上层处理”保证，不能把所有 reset 都等同它。多次 GOAWAY 的边界不得增大，本表只给最终3，没有假造完整线上交换。[RFC9113 §6.8、§8.7](https://www.rfc-editor.org/rfc/rfc9113.html#section-6.8)

单删 DNS 可能让旧 HTTP/2 池继续开新 stream；单标 unhealthy 不能撤销已提交业务；单关连接不能让后台任务自动退出。Envoy v1.31.0 的 graceful draining 文档甚至明确某种模式下会继续接受新连接直到 drain timeout，所以“进入 draining”不能代替表中的 L4 新连接停止事件。热重启、热重载、健康失败、listener 移除与任务排空也不是同一事件。[Envoy v1.31.0 draining.rst](https://github.com/envoyproxy/envoy/blob/v1.31.0/docs/root/intro/arch_overview/operations/draining.rst)

### 5.3 恢复速度、抖动和容量是共同约束

三个 worker 失败后，项目给定合法退避区间100–200ms，本次抽样110/150/190ms，尝试时刻因此错开；若抽到相同值仍会碰撞，总容量不足也不会因随机化消失。是否还能尝试必须继续受第7节 deadline、次数及永久错误停止线约束。

新实例C在t0得到readiness，抽象慢启动政策把C相对其稳态配置权重的倍率上限，在t0/t10/t30依次设为10%/30%/100%，且每阶段仍需健康和容量条件成立。逐步提高可选权重的目的是减轻刚入池的瞬时压力；它不保证任一有限窗口的流量、请求数或CPU按同样比例分配，也不证明缓存已热或CPU能承受满量。该纸面规则不是 Envoy 的运行配置；缩容则必须经历上一节的选择、准入、在途和执行者退出条件。

故障时轴还须区分：注入动作发出、故障真正生效、检查完成、告警评估、通知送达、自动修复动作完成、用户恢复、人工介入。前者不自动证明后者，介入时刻也不证明根因或责任。RTO需先指定影响对象与恢复判据；A下线例不能认证B下线或A/B同时失效时的容量。LB连接拒绝同样可能来自监听、资源或候选策略，不能仅凭拒绝就断言所有后端业务坏了。

## 6. 负载均衡先问“每次在选什么”

### 6.1 连接份额、请求份额与工作量

L4通常对新连接选择F，L7在解密并解析后可对新请求选择A/B；连接池再决定复用哪条上游连接。HTTP/2一个连接可有多个活跃stream，因此连接数、请求数、字节数与CPU工作量都不是可互换的负载单位。配置的连接数或线程数也不是实际活跃stream数。[Envoy v1.31.0 connection_pooling.rst，HTTP/2及Number of connection pools](https://github.com/envoyproxy/envoy/blob/v1.31.0/docs/root/intro/arch_overview/upstream/connection_pooling.rst)

给定确定性 WRR 周期 `[A,A,A,A,B]`，每次选一条新连接，完整5次是4:1，前3次却是3:0。若A的4条连接各承载1请求，B的1条长连接承载100请求，则连接80/20，请求A:B=4:100，A只占4/104≈3.846%。没有请求成本数据，还不能推出CPU或尾延迟。这个周期是纸面确定调度，不是随机抽样期望；随机加权算法才另需讨论样本波动。

least-connections 的独立快照：A只有1条h2连接但有100个进行中请求，B有4条连接但当前没有进行中请求，严格按连接数会选A。它观察连接的忙闲代理量，只有这个量与任务成本相关时才有用，不能仅凭“请求耗时不同”就断定最优。Envoy v1.31.0 的 weighted least request 看 active requests 及其权重配置，并不是 least-connections；按请求数也不会自动知道每个请求成本或异构后端容量。[Envoy v1.31.0 load_balancers.rst，Weighted least request](https://github.com/envoyproxy/envoy/blob/v1.31.0/docs/root/intro/arch_overview/upstream/load_balancing/load_balancers.rst#weighted-least-request)

### 6.2 固定环与取模：先给哈希和成员顺序

给十个独立新key，hash值恰为0..9。环坐标为[0,16)，A在0、B在8，相等取该节点，否则取顺时针后继；加入C在4，原token不动，无virtual node、权重或健康变化。取模对照独立规定旧有序成员[A,B]、新[A,B,C]，以同一hash分别对2或3取模。

| hash | 环旧 | 环新 | 取模旧 | 取模新 |
| ---: | --- | --- | --- | --- |
| 0 | A | A | A | A |
| 1 | B | C | B | B |
| 2 | B | C | A | C |
| 3 | B | C | B | A |
| 4 | B | C | A | B |
| 5 | B | B | B | C |
| 6 | B | B | A | A |
| 7 | B | B | B | B |
| 8 | B | B | A | C |
| 9 | A | A | B | A |

环只有h1..4改归C，共4/10；取模在h2、3、4、5、8、9改变，共6/10。环把变化局限到新增token接管的区间，所以这个样本变化较少；这不是“一致性哈希普遍迁移40%”或“总比取模好”的证明。样本分布、热点成本、token重新计算、成员次序、virtual nodes 或权重一变，结论就可能变。哈希稳定也不保证热点均衡。

key归属改变只描述新选择。会话状态是否复制、客户端是否重连、业务缓存需搬多少字节和命中率损失是其他指标，不能给它们套同一个4/10。没有状态位置与迁移合同，就停在归属表，不把它算成真实迁移性能。

### 6.3 从 Maglev 原表手填七个槽

直接取 Maglev 原论文 §3.4、Pseudocode1、Table1 的小例：M=7为质数，等权有序成员B0/B1/B2，(offset,skip)分别为(3,4)、(0,2)、(3,1)。按 `(offset+j×skip) mod7` 得三张优先序：

- B0：3、0、4、1、5、2、6
- B1：0、2、4、6、1、3、5
- B2：3、4、5、6、0、1、2

成员按序轮流扫描自己的优先序，取其中尚未被任何成员占用的首个槽。原填入顺序为(B0,3)、(B1,0)、(B2,4)、(B0,1)、(B1,2)、(B2,5)、(B0,6)。移除B1，保持剩余成员名字、hash、permutation与B0/B2轮次顺序，重填为(B0,3)、(B2,4)、(B0,0)、(B2,5)、(B0,1)、(B2,6)、(B0,2)。

| 槽 | 原表 | 移除B1后 |
| ---: | --- | --- |
| 0 | B1 | B0 |
| 1 | B0 | B0 |
| 2 | B1 | B0 |
| 3 | B0 | B0 |
| 4 | B2 | B2 |
| 5 | B2 | B2 |
| 6 | B0 | B2 |

共变3/7槽：两个原属B1，还有一个原属仍存活的B0。这说明“只改被删除成员原有槽”也不是本算法承诺。新连接若给定hash%7落槽6会选B2；旧连接只有在本次转发实例仍能命中并复用指向B0的原connection tracking记录、且B0仍可服务时，才可沿旧路径。别处还有一份记录并不足够，表变也不能单独证明旧连接已迁移。[Maglev，NSDI 2016，§3.3相邻连接跟踪段、§3.4、页6 Table1](https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/44824.pdf)

3/7是这张表的槽变化，不是任意有限请求比例、缓存迁移字节、时延或吞吐。这里手读原算法和原表，没有实现模拟器或运行 Envoy。M/N、名字、哈希、顺序、权重或健康层改变时须重新提供输入；原论文未展开权重实现细节，产品文档的性能倍数也不能移植为本例或当前主机的测量。

### 6.4 RR、反馈与熔断各有失真条件

给定按新请求轮询 `[A,B,A,B]`，两个A请求各消耗100ms工作，两个B各1ms，则请求2:2而工作200:2。RR让同质任务在同质实例上容易解释，但慢节点和异质成本会产生尾部差异；改成更多短连接可能增加建连/TLS成本，不能从短窗口份额更接近权重就推短连接更快。

EWMA用一个明确的抽象反馈模型解释：`E新=0.5×x新+0.5×E旧`。A的旧值10ms、最新样本100ms，更新后55ms；B当前40ms。若本例每次新请求选较小E，纳入新样本前选A，之后选B。采样与更新延迟会改变决策，旧低值不证明当前快；没有样本不能造出“已优化”曲线。这不是任何命名产品的EWMA-LB实现。

熔断也先限定对象：抽象项目的某客户端对某依赖，在closed态连续两次符合判据的失败后转open；open拒绝这个客户端的新调用，给定冷却点转half-open只允许一个试探，成功回closed、失败回open。其他客户端未必共享状态，旧在途任务也未撤销。另一个“并发上限2、已有2个active”例可在没有故障时拒绝新调用，属于容量限制；二者又都不同于健康系统移除某个host。不能因 Envoy 也出现 circuit breaker 字样就把该三态机当作其实现。

最后读分布：本题目标A80/B20次，给定实际新连接A75/B25，则份额偏差分别−5/+5个百分点；若报相对误差，分母还需另写。这个读数本身不认定调度bug，需结合选择单位、窗口、有效候选与第6.1节的连接复用。比较RR、least、hash、EWMA时要固定这些条件，再按后端记录同窗口请求率、错误率、延迟、CPU、内存和负载标准差；仅算法名称与“适用场景”无法替读者做选择。

## 7. 换到另一个后端，旧效果还在不在

### 7.1 从 unknown 恢复原终态

沿用第0节完整业务合同：总deadline为t10秒，最多2次尝试；A在t1.5把订单o-42和去重终态一起持久提交，客户端t2超时。给定退避合法区间100–200ms，本次取150ms；A被摘除，B接收恢复请求。额外明确B访问同一去重/订单权威、使用同一租户与操作作用域，仍在24小时保留期内，原子性、有效提交者隔离与读取返回原终态的义务同样成立。

再给定t2.15恢复请求被接收、权威读取成功、t3完整可信o-42响应到达C。这一到达事件是输入，不能从去重机制推得回复必在t3到达。状态可按下表重演：

| 时刻 | 服务端事实 | 客户端能够确认什么 |
| ---: | --- | --- |
| 1.5 | A已原子持久提交o-42及终态 | 尚未给定收到回复 |
| 2 | 回复未及时到达 | unknown；超时不撤销旧提交 |
| 2.15 | B接收同键同参，读取权威旧终态 | 恢复正在进行，不能凭发出重试即confirmed |
| 3 | 完整可信原终态到达 | confirmed，同一o-42，没有第二次订单效果 |

若第二次仍失败，次数已用尽，停止自动恢复并保留unknown；若已收到完整o-42，随后连接关闭，继续保持confirmed，不重开业务。服务端只会拒绝重复却不提供原终态、权威读取不可用或根本没有定义结果查询/恢复接口，都不足以完成此路径；不要自行编造查询URL或已查询结果。

跨区尤其不能省略权威条件。若west只有异步落后副本，看不到k-17，不代表A没提交；相同键不自带跨区唯一性。换新键、改参数、越过旧结果保存期都会破坏该恢复前提，需先取得可归属的结果证据。5xx、普通TCP RST、本地cancel、超时或摘流都不是“未提交”的通用证明；明确服务端拒绝/取消终态与仅发送取消信号要分开。若存在事务外副作用，第0节的单事务模型不覆盖它，不能继续沿此例宣称安全。

### 7.2 预算限制行动，不能保证完成或撤销

在t9.95才准备重试，而最少退避0.1秒已经超过t10，就不启动新尝试；永久错误或授权拒绝也不该靠耗尽重试次数处理。每层需区分connect、TLS、单请求、idle和总deadline的起点/终点；idle限制空闲等待，不等于全操作deadline。

给某一跳入站剩余800ms，项目分配排队上限100、连接150、TLS100、处理及响应450，合计800。若还存在已知开销，应从分配中留出余量；未知开销、重试、连接复用和并行阶段则不能机械相加。这些只是等待预算，不证明业务在450ms内完成，更不保证超时在提交前发生。沿链路传播剩余预算能约束下游行动，但不能当作数据库回滚指令。

多层重试还会放大：最外层最多2次，每次又允许LB最多3次下游尝试且没有共享总budget，最坏可启动2×3=6次下游尝试。外层的“2”不是全链总尝试上限；共享一次全链budget才按那份额度判上限。幂等结果合同控制重复效果，预算、限流与抖动控制资源压力，各自不可代替。

### 7.3 地域优先与回滚的终止边界

跨地域匹配服务可由DNS返回地域VIP、L4选择入口、L7按版本灰度、会话key做稳定归属，这是一种有限架构例，不能仅凭这些术语视为生产高可用证据。地域策略需分别记录网络RTT、应用延迟、复制延迟、剩余容量和数据权威，低RTT不补上订单一致性。

给定east失去候选而策略允许west新建连接；若west只有异步去重副本，则虽然可达，仍停在第7.1节的副作用恢复边界。只有另行满足相同权威、有效提交者隔离及原终态复用合同，才可按该例恢复。恢复east候选、调回权重或回滚发现配置，能改变未来选择，不能撤销既有订单、搬走所有旧池或强刷全网DNS。临时跨区缓解须有容量条件、代价、退出/回切阈值；配置可回滚与业务效果可逆是两种不同承诺。

## 8. 怎样阅读证据，而不把实验计划当结果

### 8.1 旧矩阵保留了研究问题，也混合了多个变量

旧矩阵把“IPv6直连/HTTP2/100连接/无故障”和“IPv4 NAT/HTTP1.1/100连接/回收NAT映射”并列，地址族、NAT、应用协议、故障至少四个因素同时变了。即便后来有数值，也不能单独归因“IPv6减少转换开销”；当前根本没有这些原始样本，不能报告已测P99或CPU改善。映射回收可能需要重建路径，但是否产生重连、持续多久、业务是否unknown，还取决于协议、状态和恢复合同。

其余四行仍有具体价值：双栈行研究候选启动和选择延迟，加权LB行研究新选择的份额，节点下线行研究影响与恢复时轴，扩容hash行研究key归属和实际缓存状态变化。它们连接数、协议和故障不同，不能横向排出统一性能名次；第2、5、6节分别给出可手算判据。旧健康上界已被34.9秒反例否定，hash“比取模少迁移”只在本节给定样本成立，原预测逐字保存不表示继续作为一般结论。

未来若获授权做对照，应固定应用协议、请求语义、请求/响应大小、复用模式、负载分布、硬件与路径、窗口、预热和采样口径，再逐次改变所研究因素；NAT所必需的结构变化若无法完全隔离，须单列混杂。生成器版本、线程数、连接数、CPU亲和性及生成器是否先成为瓶颈也是方法条件。原预热30s、测量120s、重复5次是旧计划，120s未必覆盖全部周期，5次也不自动独立或具有代表性。当前不搭环境、不运行小模型、不用合成时延替代真实结果。

### 8.2 原命令为什么不能成为当前默认路线

原作保留client/lb/server的拓扑意图，但旧“可复现实验”标题不证明环境完整或已复现：

- 早期c/l/s三命令组没有给出veth创建、接口up、路由和listener；curl也没有明确放进哪个namespace。不能从命令文字推出连通性。旧ULA字句和文档前缀示例亦不能充作已经获得的公网地址。
- 较长netns组没有给lb侧b0地址、接口/loopback启用和必要路由；IPv4相同子网还分处两条链路。仅给namespace名称不会自动形成图中连通路径。
- sysctl/nft组没有显式namespace限定；启IPv6 forwarding不等于启IPv4转发/NAT，规则中的eth0也不能自动解释为前面veth。原“仅隔离实验”注释不能补上实际执行作用域。
- 地址/路由/resolver/nft快照、socket汇总、conntrack状态和pcap各有不同观察对象。原抓包过滤器并未覆盖所有TLS与应用通信，`head`只保留部分输出，单一快照不能重建完整生命周期。读到旧命令不等于已经采集。

这些缺口在块外说明，下面历史命令不作补写或修正；当前没有可执行的替代步骤。未来netem丢包/延迟设计还需接口、namespace、方向、基线和恢复条件，LB重启还需区别进程中断、热重载与受控排空。此处只保留方法用途，不查询设备状态、不改转发/权限、不注入故障，也不新增runner或CI。

静态定位按最早未获证层推进：地址/zone → RA与路由/ND → PMTU → 传输 → TLS → 发现/健康/准入 → 业务终态。ping6的小包响应不替代TCP/TLS，traceroute6中间hop沉默不唯一定位丢包，conntrack表项不证明订单已提交。每层允许答案是“证据不足”，不因某一次后层成功就认证所有时刻和路径正常。

### 8.3 让125项记录能回答具体问题

历史清单保留125个字段，它们是未来受授权观察与复盘的索引，不是125个漏洞或本轮已采集结果。当前可先把给定记录放到下列问题中阅读；字段不足时应标未知，不能为了填满模板补造数据。

**方法身份与范围。** 内核、iproute2、nftables、CoreDNS、Envoy及wrk2/h2load的实际运行版本，应和本篇查阅的文档版本分列。配置快照、配置版本、脚本commit描述方法身份，不证明进程已加载或脚本确实运行，也不证明没有本地改动。每个接口MTU/MAC、路由、邻居、VIP/后端、NAT前后元组都须标所在节点与namespace；一个全局数字不能代替每条链路。这样才能将第1节地址/下一跳问题和第3节转换/资源问题对到正确对象。

**状态与计时。** RA/default router/PIO、DHCPv6、DNS正负缓存、resolver与客户端缓存、租约、健康连续计数、连接池和keepalive都各记创建或接收时刻、期限、更新条件和实现策略。DNS日志保query/RCODE/TTL，健康日志保探测启动/完成/timeout/阈值，conntrack快照保原/回复元组与采集时刻，路由与邻居快照保接口和状态。单份快照不能重建续约、回收或完整流生命周期；缺DNS日志也可能因缓存。这样才可分别回答“名称切换慢”和“旧池仍在用A”。

**选择和容量。** 每个后端在同一窗口记录连接、活跃请求、请求/响应字节、CPU、内存与算法权重，再谈分布误差和标准差。CPU低不排除其他瓶颈；标称带宽、实际吞吐、可用容量也不同。临时端口、外部PAT池、conntrack、邻居缓存、DNS QPS的分母和上限各自给出，满载、端口分配失败、邻居溢出是事件，TIME_WAIT是TCP对象数。若四种资源平均使用率都为69%，仍可能有突发分配、单客户端配额、热后端或TTL刷新峰值先触线；旧70%不是通用安全线，余量须按峰值窗口和具体约束判断。

**故障与影响。** IPv6延迟、IPv6丢包、映射回收、DNS暂停、A或B下线、权重变更、扩缩容、健康误报、旧池、MTU黑洞、ND失败、表满和端口耗尽，都需各自的原始条件、研究单位与停止线。单故障样本不能证明联合故障容量。给定发生时刻后，分别列检测、告警、修复动作、用户恢复和人工介入，用户影响还要按用户/地域/请求/会话给分母。新连接拒绝、SYN重传、RST、SERVFAIL、NXDOMAIN、5xx和超时是不同层的现象，不能合并成一种根因或一种“未执行”。

**错误、重复与业务结果。** 某行只有“错误率1%”，缺分母、时间窗和超时口径就不能比较。给定100次业务请求、6次额外重试、2次完整5xx、3次超时，请求结果分母100与尝试分母106不能交换；这些数字尚未交代每次重试归属和终态，所以不能据此计算数据错误率。5xx/timeout都不独立证明无提交；重复到达、重复执行、重复提交是三个计数。会话key改归属、状态复制、客户端重连、缓存迁移bytes与命中损失也须分列。跨区RTT低、HTTP成功不认证权威数据正确，缺逐请求终态就填未知。

**控制点与隐私。** IPv6防火墙、安全组、源地址验证、RA Guard、DHCPv6和临时地址选择需写对象、方向、策略与覆盖路径。nft规则快照不证明命中，累计命中计数不等于请求数或远端收到；网卡错误计数也要用同窗口差分和事件关联，不能把历史非零值认作本轮故障原因。ICMPv6丢弃和PMTU失败需关联具体流。源地址透传按第3.5节核对直接peer、可信代理陈述和应用身份。

反射/放大字段也有独立纸例：给定UDP服务收到60字节应用请求会回1200字节，源字段被伪称为第三方V且本例无源校验，应答就被送往V，应用数据尺寸放大比1200/60=20，链路开销另算。其用途是明确需要审查源校验、应答尺寸、速率与暴露入口，不能从一个阈值认证防护充分。本例没有构造或发送攻击包。

**原始证据与可复查性。** stdout/stderr应连同实际输入、时间、退出码、截断标记保存，缺流写缺失；SHA256匹配只证明字节一致，不证明采集真实。Prometheus需保原序列、标签、采样间隔和窗口，Grafana截图只是所选视图，不能重算总体分位。pcap权限范围/保存期限、日志脱敏规则是要明确的条件；敏感原件与可分享脱敏副本分开保存，副本不替代原件。这里未产生采集文件或修改任何权限。

**统计与决策。** 预热、有效测量窗口和剔除规则须先约定，不得删掉慢样本或失败样本后宣布改善。均值/标准差描述给定总体，不自动意味着正态尾；p50/p95/p99需同一请求起止、失败口径和采样机制，阶段P99不能相加，多次运行P99不能简单平均为总体P99，有限P99也不是最大延迟。置信区间需要指标、原样本和独立性假设，不能解释为“95%的请求落在区间内”。无样本时不算置信区间或达标结论。

风险等级要写影响与证据缺口；临时缓解要有条件、代价和退出线，现象消失不等于根因消失；永久修复要有因果依据和相应回归输入/判据。告警需阈值、持续时间、接收与处置责任，监控配置存在不等于通知已送达。复盘责任人、独立评审人、上线门槛、回滚阈值和下一实验日期都是有用途的待明确字段：实施与审查分开，指定评审人不等于已审，不能虚构姓名、日期、上线授权或新增提醒。回滚候选/权重的边界仍是第7.3节，不能倒销订单效果。

## 9. 来源版本与本轮证据边界

核对日期为2026-10-07。下面列出实际采用的窄范围；固定编号RFC是规范文字，产品文档是对应版本的描述，API注释并不是完整运行调用链。来源保存与SHA只固定字节，选读才支撑相应段落；未读章节、全部errata/更新链和当前所有产品版本没有被认证。

| 一手资料与版本 | 本文采用的定位及边界 |
| --- | --- |
| [RFC4291](https://www.rfc-editor.org/rfc/rfc4291.html)，2006；[RFC4007](https://www.rfc-editor.org/rfc/rfc4007.html)，2005 | 前者§2、§2.4、§2.5.4、§2.5.6的位宽/作用域/格式，后者§11的zone；未沿用旧IID生成建议作现代算法 |
| [RFC4861](https://www.rfc-editor.org/rfc/rfc4861.txt)、[RFC4862](https://www.rfc-editor.org/rfc/rfc4862.txt)，2007 | RA/PIO/默认router与on-link：4861§4.2、§4.6.2、§6.3.4，另选§2.3/§3.1的组播/NS/NA职责及本次窄补§5.1状态定义（原LF1926–1958）；地址状态、DAD总则、SLAAC与寿命：4862§2、§5.3–5.4、§5.5.3–5.5.4；不是完整ND实现审计 |
| [RFC8415](https://www.rfc-editor.org/rfc/rfc8415.txt)，2018 | §1、§3、§18.2.10.1的地址/其他配置与on-link推断边界；未声称它是最新DHCPv6全文 |
| [RFC8200](https://www.rfc-editor.org/rfc/rfc8200.txt)、[RFC8201](https://www.rfc-editor.org/rfc/rfc8201.txt)，2017 | 8200§4.5/§5源分片与MTU，8201§3–4/§6的PTB与黑洞；未核PLPMTUD实现或真实路径 |
| [RFC6105](https://www.rfc-editor.org/rfc/rfc6105.html)，2011；[RFC7113](https://www.rfc-editor.org/rfc/rfc7113.html)，2014；[RFC8981](https://www.rfc-editor.org/rfc/rfc8981.html)，2021 | 6105§1–3/§5、7113摘要/§1只支撑RA Guard入口与识别边界；8981§1/§1.2/§2.2只支撑临时地址动机，未审设备、攻击或生成算法 |
| [RFC8305](https://www.rfc-editor.org/rfc/rfc8305.txt)，2017 | §2–5、§9相邻选段：解析、排序、错开连接与限制；旧RFC6555编号仅保留在历史原文 |
| [RFC4787](https://www.rfc-editor.org/rfc/rfc4787.txt)，2007；[RFC5382](https://www.rfc-editor.org/rfc/rfc5382.txt)，2008；[RFC6888](https://www.rfc-editor.org/rfc/rfc6888.txt)，2013 | UDP mapping/refresh/filtering/hairpin在4787§4.1/§4.3/§5/§6；TCP mapping及idle边界在5382§4.1/§5；CGN资源在6888§2、§3 REQ-1/4/5；没有设备符合性或Linux实现结论 |
| [RFC8489](https://www.rfc-editor.org/rfc/rfc8489.txt)、[RFC8656](https://www.rfc-editor.org/rfc/rfc8656.txt)，2020；[RFC8445](https://www.rfc-editor.org/rfc/rfc8445.txt)，2018 | STUN摘要/§1–2、§9总则、§14.2；ICE§2.1开头、§2.2–2.3、§7.2.2、§8.1.1、§12.1；TURN§3.2–3.3/§3.5片段、§6/§9/§11.3/§12寿命及§12.6–12.7；未运行交易或核完整安全扩展 |
| [RFC6887](https://www.rfc-editor.org/rfc/rfc6887.html)，2013 | §1的显式映射、寿命和rendezvous职责；未把PCP细节移植给未审版本的UPnP |
| [PROXY protocol，HAProxy v3.0.0](https://github.com/haproxy/haproxy/blob/v3.0.0/doc/proxy-protocol.txt)，文件自述修订2020-03-05 | §2/§2.1与§5的头/入口/复用边界，另选§2.2.3与§2.2.6的CRC32C/SSL TLV；未核监听器或完整解析器。标签版本与文件修订日分开 |
| [RFC1035](https://www.rfc-editor.org/rfc/rfc1035.txt)，1987；[RFC2308](https://www.rfc-editor.org/rfc/rfc2308.txt)，1998；[RFC8767](https://www.rfc-editor.org/rfc/rfc8767.txt)，2020 | 1035§3.2.1/§4.1.3须连同8767§4–5更新读，不沿用旧signed/SOA-TTL0措辞；2308§4–6及§7开头限负缓存与失败边界 |
| [RFC2782](https://datatracker.ietf.org/doc/html/rfc2782)，2000 | Applicability与SRV字段；采用IETF正式镜像选读，不默认HTTPS使用SRV |
| [Envoy v1.31.0发现文档](https://github.com/envoyproxy/envoy/blob/v1.31.0/docs/root/intro/arch_overview/upstream/service_discovery.rst)及[连接池](https://github.com/envoyproxy/envoy/blob/v1.31.0/docs/root/intro/arch_overview/upstream/connection_pooling.rst) | discovery原LF22–82/138–183之外已补LF105–125的EDS；pool的HTTP1/2、TCP HE、pool划分与health交互。文档差异不能直接当部署结果 |
| [Envoy v1.31.0健康](https://github.com/envoyproxy/envoy/blob/v1.31.0/docs/root/intro/arch_overview/upstream/health_checking.rst)、[排空](https://github.com/envoyproxy/envoy/blob/v1.31.0/docs/root/intro/arch_overview/operations/draining.rst)、[LB](https://github.com/envoyproxy/envoy/blob/v1.31.0/docs/root/intro/arch_overview/upstream/load_balancing/load_balancers.rst)、[cluster.proto](https://github.com/envoyproxy/envoy/blob/v1.31.0/api/envoy/config/cluster/v3/cluster.proto) | health LF1–42/151–196，draining LF1–59，LB LF1–158；proto仅LF1109–1127两个字段注释。不引用性能倍数/推荐语作通用结论 |
| [CoreDNS v1.11.0 cache](https://github.com/coredns/coredns/blob/v1.11.0/plugin/cache/README.md) | Syntax、TTL上下限、serve_stale、servfail、keepttl，原LF16–83；TTL0与RFC例分开，未审完整cache实现 |
| [RFC9113](https://www.rfc-editor.org/rfc/rfc9113.txt)，2022 | §6.8、§8.7，原LF1931–2068/2961–2997；只用于stream未处理边界和退场，不替代业务去重 |
| [Maglev原论文](https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/44824.pdf)，NSDI 2016 | §3.3末段/§3.4、Pseudocode1、Table1，页6图与文本核对；没有重现性能实验或权重实现 |

Envoy v1.31.0标签解析为commit `7b8baff1758f0a584dcc3cb657b5032000bcb3d7`，CoreDNS v1.11.0为 `9f4aa9d2625bb8b8f13efa98d3ecf1928cd112b6`；标签与每份取得文本均在本轮材料中固定。原26份来源加6份窄补充表示来源集合，不表示32份全文通过。RFC文本保留form-feed，原件定位按LF与实际节号，不能把所有换页字符都当换行后继续套旧行号。补充网页保存的是结构化工具返回，不冒称完整HTTP实体；正式原件下载也不等于已全文审读。

保留的取证边界包括：Maglev首次网页读取Internal Error后才从同一官方PDF取得实体；RFC1035首次定位没读到所需TTL，随后补正确段；宽输出截断与缺失尾部后来以窄选读处理。产品读取曾有错路径与越界IndexError，EDS范围后来另补，不能追认旧索引已覆盖；RFC2782在RFC Editor遇429/Internal Error，随后正式镜像成功。作者阶段也有文件枚举遇不存在路径和长显示截断，后续按精确路径补读；未落盘原流只披露缺失，不补造。以上都不是协议实验失败，也没有被改记成成功运行。

第0、4.4、7节的业务原子性、epoch提交检查、权威结果复用是明示项目合同；第5、6节的健康时间、EWMA、慢启动、熔断和分配数字是限定纸例。本文没有读Consul/etcd或Linux网络实现，没有验证配置加载、网络安全、压测或生产RTO。旧Ubuntu/Linux/工具版本、命令和预测仅保留历史身份；没有原始stdout/stderr/pcap/样本时，不生成不存在的性能结果。

## 历史原件区：2026-08-20 的前言、方案与运行模板

以下【历史原件开始】至【历史原件结束】完整保留旧前言、实验/版本/Benchmark/排查与125项模板。早期版本已经是完整成文贡献，本次是对其因果解释与证据边界的修订。区内日期、命令、环境声明、70%门槛、RTO<30秒与预测均保持原字，不代表本轮执行或测得结果，也不构成当前默认运行路线。当前静态入口及旧方案缺口见前面的正文；区内原H2标题属于历史记录。

【历史原件开始】

> 验证与基准：按文中命令执行最小实验，记录结果与边界。

> 知识成熟度：L2；地址规划和故障结论必须在目标网络验证。
> 知识基线：RFC 8200、RFC 4861、RFC 8445 与常见 DNS/L4/L7 实现。
> 最后更新：2026-08-20。
> 官方参考：https://www.rfc-editor.org/rfc/rfc8200、https://www.rfc-editor.org/rfc/rfc4861。
> 验证入口：使用 dig、ping6、conntrack、tc/netem 与压测。

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
【历史原件结束】

以上原件仅为保留与追溯。当前结论以第0–9节明确的题设、来源和未验证边界为准；保留实验计划没有产生部署、修改系统或启动实验的授权。


---

## 关联知识与工程落地

- **前置依赖**：
  - [01-网络分层协议与工程实践](01-网络分层协议与工程实践.md)：IP 路由与传输层端口机制。
  - [02-QUIC与HTTP演进](02-QUIC与HTTP演进.md)：连接迁移。
- **分布式系统结合**：
  - [12-数据库与分布式系统/02-分布式系统基础](../../07-网络与游戏服务端/持久化与分布式一致性/02-分布式系统基础.md)：服务注册发现与节点高可用。
  - [16-容器云与可观测性/容器云与可观测性基础](../../08-工程实践与质量/部署运维与可观测性/容器云与可观测性基础.md)：K8s 容器网络与 Ingress。
- **游戏服务端落地**：
  - [游戏服务端/05-UE Dedicated Server平台化/03-DS会话注册与重连实现](<../../07-网络与游戏服务端/会话身份与在线服务/03-DS会话注册与重连实现.md>)：会话网关注册与调度。
- **分类与领域入口**：
  - [09-计算机网络基础 README](../../../00_Index/学习路线/网络与游戏服务端.md)
  - [计算机与工程基础 Domain MOC](../../../00_Index/学习路线/编程与计算机基础.md)
