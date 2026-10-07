---
type: Concept
title: "QUIC 与 HTTP/2/3 工程实践"
status: stable
verified: []
maturity: L2
---
# QUIC 与 HTTP/2/3 工程实践

> 当前阅读路线：先做本文的静态状态追踪与字节推导，再对照来源。核对日期：2026-10-07。知识成熟度仍为 L2，`verified: []`；PAPER_EXPECTED 表示合成题设与预期推导，不是网络观测。本文没有运行客户端、服务端、抓包、故障注入或性能实验。文末完整保存的 2026-08-20 前言、实验和记录模板属于历史原件，不是当前执行入口。

## 1. 为什么可靠下载仍可能得不到可用补丁

假设游戏启动器要恢复 build42 的补丁。QUIC 能让收到的流字节有序交付，HTTP 能告诉启动器这是什么响应，但这两层都不知道“当前文件槽中的内容是否就是发布者批准的 build42”。一个完整收到的错误版本、一个仍在异步写入的文件、一次结果不明的发布操作，都不能成为“补丁已可用”的证据。

因此先把结果分成四层：传输收齐所需 stream bytes；HTTP 消息合法且完整；表示身份、区间、长度及可信摘要匹配；稳定候选得到明确的本地发布成功回执。本文逐层回答每个状态由谁证明，以及证明不足时应该停在哪里。实际安装和游戏版本切换不在这四层承诺内。

### 1.1 固定对象、信任来源与恢复起点（P0）

教学对象只有 16 byte，ASCII 内容为 `abcdefghijklmnop`，没有换行。下载前，客户端已经通过预先信任的发布身份或钉住公钥验证了签名发布清单。这个可信通道是题设；本文没有生成密钥、实现签名验证，或以待下载 body 附带的 hash 建立信任。清单固定以下输入：

| 输入 | 值与作用 |
| --- | --- |
| URL / build | `https://patch.example.test/builds/42/client.bin` / build42 |
| 表示变体 | identity，长度 16；URI、变体和 validator 要一起匹配 |
| 强 ETag | `"build42-identity"`；是不透明验证器，不假定它是内容摘要 |
| 可信完整 SHA-256 | `f39dac6cbaba535e2c207cd0cd8f154974223c848f727f98b3564cea569b41cf` |
| 已保存前缀 | `abcdefgh`，表示 byte 0..7，已按清单的分片摘要验证 |
| 可信前缀 SHA-256 | `9c56cc51b374c3ba189210d5b6d4bf57790d351c96c47c02190ecf1e430635ab` |
| 缺失范围 | 表示 byte 8..15；本地物理文件长度不能替代已验证区间集合 |

资源合同允许重复读取这个不可变对象，不会扣款、授予权限或安装；重复流量与日志仍有成本。默认禁用 0-RTT。目的地解析、正常 TLS 信任链、origin 名称校验、QUIC v1 / TLS 1.3 / `h3` 支持，以及后文单写者和受控发布接口，是明确前置。一个“UDP 端口可达”的结论不能提供这些前提。

如果前缀没有校验、变体未知、清单未经认证，或旧文件可能被其他进程改过，就先停止续接。强 ETag 不能补足清单信任，两个 URL 碰巧有相同 ETag 也不是同一对象；自己重算出一个 SHA 只能描述现有字节，不能决定应当接受哪个版本。恢复的基础是被验证的对象与区间，而不是连接还活着。

### 1.2 先看一次成功需要经过哪些责任

一次正常路径是：按原 origin 验证服务器 → 完成 QUIC/TLS 与 HTTP/3 初始化 → 在请求流中取缺失范围 → 验证完整 HTTP 响应 → 等候候选的全部 I/O 完成及写者退场 → 对稳定快照校验 → 发布到受控可用槽并取得回执。第 8 节会给出每个失败分支的完整输入。

这也解释 HTTP 的演进为何有用：HTTP/2 在一个 TCP 连接上复用请求，减少反复建连及重新慢启动的成本；HTTP/3 将请求映射到 QUIC 流，把一个流的传输重组缺口与另一个流分开。但连接复用保留了拥塞、内存、压缩状态与应用工作队列的共享关系。协议进步减少某些等待，不会替应用决定文件身份、重试权限或发布事务。

## 2. 建连时分别证明身份、握手状态与地址（P1）

UDP datagram 可以承载 QUIC packet；QUIC 在其上提供可靠 stream。用户态实现便于独立部署和更新，但这是常见部署选择，不是 RFC 强制的运行位置。QUIC 用 CRYPTO frames 承载 TLS 握手信息，并把传输参数与 TLS 1.3 握手结合；它不在普通 QUIC stream 上再套一层 TLS record。由此可以减少分离建连阶段的等待，具体轮次仍依赖冷连接、恢复、Retry、丢包和 early data，不能承诺每次都节省固定 RTT。

### 2.1 同一个连接中的四个判断

客户端的参考身份来自原 URL 的 origin `patch.example.test`。TLS 需要可信证书路径、有效期、名称与握手证明；SNI 和 ALPN 是相关输入而非认证结果。题设最终选择 `h3`。证书只覆盖 `edge.example.test`，或选择的是 `h2`，都不满足本例的 H3 前提；前者停止身份验证，后者不能继续发送 H3 帧。关闭证书链或主机名校验不构成恢复。

| 纸面事件 | 现在能确认 | 尚不能确认 |
| --- | --- | --- |
| 收到可解密 Initial | 能处理这份 Initial 信息 | Initial 密钥从公开信息导出，不证明 origin 身份 |
| 客户端验证服务端 Finished，并发出自己的 Finished；服务端尚未收到 | 客户端 handshake complete | 服务端未必 complete，客户端未必 confirmed |
| 服务端已发 Finished，并验证客户端 Finished | 服务端 complete 且 confirmed，必须尽快发 HANDSHAKE_DONE | 客户端是否收到它 |
| 客户端收到 HANDSHAKE_DONE | 客户端 confirmed | 路径迁移、HTTP 成功或业务终态 |
| 客户端收到确认其 1-RTT packet 的 ACK | RFC9001 允许客户端据此认为 confirmed | 该请求已被消费或提交 |
| 服务端成功处理客户端 Handshake packet | 可按 RFC9000 认定客户端地址已验证 | 应用账号授权，或双方所有握手状态都已完成 |

complete 是端点视角，不是两端同时翻转的布尔值；confirmed 解决何时能确信握手进入稳定阶段。地址验证则回答对方能否在该地址接收数据，主要关系到反放大和路径状态。持有 CID、收到 Retry/token 或可解密 Initial，均不能替代服务器身份验证。

### 2.2 密钥退出也由状态触发

客户端第一次发送 Handshake packet 时必须丢弃 Initial 密钥；服务端第一次成功处理 Handshake packet 时必须丢弃 Initial 密钥。每个端点在自身握手 confirmed 时都必须丢弃 Handshake 密钥，不能把这项 MUST 写成任选清理。

客户端安装 1-RTT 密钥后不得再发送 0-RTT；服务端可以短暂保留 0-RTT 接收密钥处理乱序，收到 1-RTT 后须在短时间内丢弃。新密钥可用，不代表所有旧层密钥一律同时删除。1-RTT key update 改变包保护状态，既不重置 Application Data 包号，也不证明应用已处理一次或文件完整。

后续诊断应把握手失败归到身份、ALPN、参数或路径等具体阶段，并绑定所选协议、cipher suite、实现版本和连接标识。仅有“TLS 失败”或“升级后成功”不足以解释哪项前提变化。依据：[RFC9001 §§4.1、4.9、5.6、6](https://www.rfc-editor.org/rfc/rfc9001.html#section-4.1)、[RFC9000 §8](https://www.rfc-editor.org/rfc/rfc9000.html#section-8)、[RFC9114 §3](https://www.rfc-editor.org/rfc/rfc9114.html#section-3)。

## 3. 包被确认，为什么还不能交付响应

### 3.1 方向、包号空间和流偏移是不同坐标（P2）

每个发送方向分别维护 Initial、Handshake、Application Data 三个 packet-number space。Initial / Handshake / 0-RTT / 1-RTT 是四个加密层，其中 0-RTT 与 1-RTT 共用 Application Data 包号空间，密钥不同。Retry 和 Version Negotiation 没有 packet number。0-RTT 只有客户端发送，服务端用 1-RTT 包确认它。

因此 C→S Initial PN0、C→S Handshake PN0、C→S 0-RTT PN0、C→S 1-RTT PN1、S→C 1-RTT PN0 可以并存。若 C→S 后续 1-RTT 又用了 PN0，就在同一个 Application Data 空间重用了包号。ACK 位于对应的 PN 空间，确认的是对端发送的 packet ranges；承载 ACK 的包有自己的 PN，这个 PN 不是 stream offset，更不是 HTTP body 偏移。

为了看到差别，考虑独立于 16-byte 补丁的另一条已建立连接。服务器方向如下；长度是编码后 HTTP 帧占据的 QUIC stream bytes。题设小响应合法完整、QPACK 无阻塞，流控与调度预算足够：

| 事件 | 客户端能做什么 |
| --- | --- |
| PN10 携 stream0 offset 0..399，但尚未到达 | stream0 缺前缀 |
| PN11 携 stream4 offset 0..99 和 FIN，构成完整小响应 | 独立按序交付 stream4，并完成该响应 |
| PN12 携 stream0 offset 400..799 到达 | 暂存这些字节，不能越过 stream0 缺口交给 H3 |
| PN13 携 PING 到达；客户端 App PN7 带 ACK ranges 11..13 | 服务器知道这些 packet 收到，不知道 HTTP 消费或业务提交结果 |
| PN10 仍未确认，题设 packet threshold 为 3 | `13−10=3`，可以在本空间判 PN10 丢失；实际原包仍可能迟到 |
| 新 PN14 携 stream0 offset 0..399 | 补齐后可交付 stream0 0..799；重发的是仍需要的信息，不是“重发 PN10” |

原 PN10 随后到达，不应再次交付同一 offset 的字节。一个 packet 可以装多个流的 frames，故一次包丢失也可能同时在多个流留下缺口。反过来，PN11 如果缺自身前缀，或它的 HEADERS 等待 QPACK 条目，也不能像表中那样完成。所谓流独立，是重组与交付关系上的独立，不是每个请求的完成时间互不影响。

STREAM 信息、ACK、RESET_STREAM、MAX_DATA 等的再次发送规则不同；过期 ACK 或已被更大上限取代的额度不能机械复制。没有方向、PN 空间、frame 类型和 offset，就不能从“packet10 lost”推断“文件第十块丢了”。依据：[RFC9000 §§2.2、12.3、13.2.6、13.3](https://www.rfc-editor.org/rfc/rfc9000.html#section-12.3)。

### 3.2 RTT 必须先有合格样本（P3-R）

RTT 估计服务于恢复和拥塞控制，不是用户等待下载的总时长。以下时间均为同一路径的本地单调时钟，没有等待解密密钥等本地处理延迟；ACK Delay 已正确解码成毫秒。只有 ACK 的 largest acknowledged 是新确认的，并且本次新确认集合至少包含一个 ack-eliciting packet，才生成 RTT 样本。取 largest acknowledged 包的发送时刻，不随意挑一个更早包，也不对重复 ACK 重复采样。

首次发送 t=100ms，接收合格 ACK t=140ms，则 latest_rtt=40ms，min_rtt=40ms，smoothed_rtt=40ms，rttvar=20ms。首次初始化不先减掉对端报告的 ACK delay。后续样本先以未调整的本地观测更新 min_rtt，再考虑 delay：

| 独立快照 | 条件与算式 | 结果 |
| --- | --- | --- |
| A | 旧 min=20，send=100，ACK=150，decoded delay=30，max_ack_delay=25，已 confirmed | latest=50；delay 取 min(30,25)=25；50−25=25≥20，adjusted=25ms，min 仍20 |
| B | min=30，latest=40，delay=20，max=25，已 confirmed | 40−20<30，不扣 delay；adjusted=40ms，不是20，也不是强行钳到30 |
| C | 与 A 相同但未 confirmed | SHOULD 忽略 max_ack_delay 上限；本例用报告的30，adjusted=20ms；若低于 min，确认前还允许忽略该样本 |

Initial packet 的 ACK delay 可选择忽略（MAY）。这和后文 Initial/Handshake PTO 中 `max_ack_delay=0` 是两条规则，不能合并成“所有 RTT 都强制减零”。min_rtt 使用本地未调整样本，不随对端报大的 delay 而下降。存在本地延迟或跨路径时还需按规范处理，不能直接套这张表。

固定 RFC9002 的 §5.3 正文先更新 smoothed_rtt，再据此计算 rttvar_sample；A.7 的 UpdateRtt 伪码先用原 smoothed_rtt 更新 rttvar，再更新 smoothed_rtt。这里如实保留顺序差异，不裁决 errata 或实现优劣，不给依赖该差异的后续 rttvar 数值。以上推导止于首次初始化与 adjusted_rtt。依据：[RFC9002 §5](https://www.rfc-editor.org/rfc/rfc9002.html#section-5)、[A.7](https://www.rfc-editor.org/rfc/rfc9002.html#appendix-A.7)。

### 3.3 PTO 是恢复推进条件，不是物理丢包证明（P3）

另一个独立快照给定：握手 confirmed、路径已验证，只有 Application Data 空间有需确认数据；尚有未确认 STREAM 信息，有可发探测预算，没有更早 loss timer；smoothed_rtt=40ms、rttvar=5ms、kGranularity=1ms、max_ack_delay=25ms、pto_count=0。这些是输入，并非从前述 A/B 快照计算得到。

基础 PTO=`40+max(4×5,1)+25=85ms`。到期仍未获 ACK 时，本例至少发一个 ack-eliciting probe packet，可发送最多两个包含此类包的 full-sized datagram；所有 probe packet 都必须 ack-eliciting。定时器到期不能证明旧包在物理路径上丢失，本例不能因此把全部未确认包标 lost。RFC9002 §6.2.4 对无新旧数据可发的情况另有可选直接判丢替代分支，不能把本例推广成没有任何例外的口号。

条件和估计不变时，下次 PTO 期间因退避变为170ms，而非固定85ms。Initial/Handshake 空间的 PTO 公式用 max_ack_delay=0；握手未 confirmed 不得设置 Application Data PTO。服务器未验证客户端地址且已耗尽反放大额度时不得 arm PTO，探测包不能绕过这笔额度。持续拥塞还有跨足够长的已判丢区间等条件，一个 PTO 不是持续拥塞证明。

时间阈值也要有 ACK 进展：若 latest_rtt=48ms、smoothed_rtt=40ms、kTimeThreshold=9/8，则阈值为 max(54,1)=54ms；还须同空间更晚包已被确认等判丢前提，不是墙钟过54ms就可判任意包丢失。它与上一节 `13−10=3` 的包阈值，是不同判据。

### 3.4 谁共享发送预算

拥塞控制按路径管理共享预算，cwnd/bytes_in_flight 约束一批流，pacing 再安排它们的发送节奏。PTO 探测不应被普通 cwnd 检查完全挡住，但仍计入 bytes_in_flight，反放大限制也仍适用。多流不创造额外带宽，多连接还可能在同一瓶颈竞争。

RFC9002 给出类似 NewReno 的示例；固定 aioquic 1.2.0 示例参数默认 reno。CUBIC/BBR 是有价值的未来比较对象，但不能据协议名称猜部署默认或宣布胜者。pacing 旨在降低突发，不足以无条件证明尾延迟更低。观察值缺少路径、算法、阶段、ACK delay 或退避计数时，就停止报告确定 PTO 或性能结论。依据：[RFC9002 §§6–7](https://www.rfc-editor.org/rfc/rfc9002.html#section-6)，尤其 §6.2.2.1、§6.2.4、§7.5–7.7。

## 4. 收到更多字节需要额度，停止操作需要退场证明（P4）

### 4.1 两级流控不是可退还的收据

QUIC 流控保护接收端资源。MAX_STREAM_DATA 给一个发送方向的流累计 offset 上限；MAX_DATA 给连接各受控流最高 offset 或 final size 的累计上限。它们不是 ACK 一到就补回的令牌，也不等于拥塞窗口或 socket buffer。

独立 S→C 快照：stream0 已用最高 offset+1=600，stream4=300，其他流合计100；MAX_DATA=1200，连接只剩200个新 stream byte。stream0 上限800，单流剩200；stream4 上限600，单流剩300。二者合计仍最多新增200，不能各花一份连接余额。这里包含 H3、控制流、QPACK 编码占用的 stream bytes，不能当补丁文件长度。cwnd、MTU 与调度还会另外限制何时发送。

再次发送已计入 offset 的400 byte 不再占400新额度；ACK 也不直接增加 MAX_DATA。乱序到达的旧上限1100无效，增到1400才让累计上限多200。消费内存后何时宣告更大上限由接收端策略决定。

再从接收者看，已有最高 offset 同为600、300、100。合法收到 stream0 RESET_STREAM(final_size=700) 后，总占用变成700+300+100=1100，距离1200仅剩100；尚未到达的600..699也计入，reset 不退还700。final size 小于已收最高 offset、改变已知 final size，或收到超出最终长度的数据，属于 FINAL_SIZE_ERROR 的语义；超过 MAX_DATA/MAX_STREAM_DATA 则区分 FLOW_CONTROL_ERROR。对已关闭流的后续检测有保留状态的成本，不能把规范的 SHOULD 全改成无条件 MUST。依据：[RFC9000 §§4.1、4.4–4.5](https://www.rfc-editor.org/rfc/rfc9000.html#section-4.1)。

### 4.2 取消分别作用于方向、请求与本地写者

客户端仍在接收响应时，STOP_SENDING 请求服务端结束其发送方向；客户端自己的 RESET_STREAM 中止自己的发送方向，并带应用错误码和 final size。对端要按发送状态处理 reset，在途数据还可能到达，STOP_SENDING 后的数据仍计流控。只 reset 请求方向，不会自动结束响应方向；控制流等关键流也不能当普通请求流随意关闭。

由此产生三类业务结论：完整响应已经验证并发布，有确定成功；协议或服务明确保证本次未开始/未处理，有条件恢复；只有 timeout、普通 reset、H3_REQUEST_CANCELLED 或取消信号，没有终态，则是 unknown。客户端不再等待，不等于服务端停止副作用；完整终态已确认后，迟到取消也不会倒改它。

本地同样存在未完成责任：网络回调结束不等于异步磁盘写入完成；对象仍有引用或句柄只证明对象可能可访问，不证明唯一写者 W 已退出。取消、停止新入队、pending I/O 真实完成、任务 join/退出回执必须分开。第 8.4 节给出本例单写者状态机；在退出无法证明时保留候选与所有权状态 unknown，不自动删除或重用。依据：[RFC9000 §3.5](https://www.rfc-editor.org/rfc/rfc9000.html#section-3.5)、[RFC9114 §4.1.1](https://www.rfc-editor.org/rfc/rfc9114.html#section-4.1.1)。

### 4.3 接收额度之外还要限制工作量

连接数、同时开放的流数、在执行请求数、头区大小、CPU 与候选文件内存是不同资源，不能只配 MAX_DATA 就视为过载安全。连接/流额度解决某层接收压力，应用仍要限制排队和存储工作。按 IP 的建连速率限制可以约束一类滥用，但共享 NAT 后的正常用户可能相互误伤，也不能替代地址验证和三倍反放大；阈值必须关联主体、窗口与业务负载，本例不给通用常量。

## 5. HTTP/2 与 HTTP/3 怎样把字节变成消息（P5）

### 5.1 同一个请求及响应到底在哪条流上

H2 在一条 TCP 连接内用二进制帧分隔 HEADERS、DATA 与控制信息，普通客户端请求可使用 stream1、3。TCP read 的边界不是帧边界，frame header 也不是响应 body。H3 对应请求可使用客户端发起的双向 stream0、4；每个请求、其零个或多个中间响应和最终响应共用同一条双向流，服务器在该流的反向发送响应，不能另开 server-bidi 流充作这个响应。

H3 每端有一条单向 control stream，首帧必须 SETTINGS。QPACK encoder/decoder 使用另外的单向流。peer 必须允许它们创建，不等于所有情况下都已经创建并使用三条流：encoder stream 不会使用时可省略；decoder stream 只有本端 decoder 最大动态表容量设为0才可省略，不能从“自己的 encoder 不用动态表”推断自己的 decoder 也无工作。关键 control/QPACK 流关闭影响连接，不能当单个请求取消。

SETTINGS 的各参数具有各自方向与约束，有些宣告接收能力，不是所有数值都由两端对等议价。stream ID 是本连接内关联键，重连、代理转换与重试后还需要逻辑请求 ID 和 attempt ID。依据：[RFC9113 §8.1](https://www.rfc-editor.org/rfc/rfc9113.html#section-8.1)、[RFC9114 §§4.1、6](https://www.rfc-editor.org/rfc/rfc9114.html#section-6)、[RFC9204 §4.2](https://www.rfc-editor.org/rfc/rfc9204.html#section-4.2)。

### 5.2 H2 的窗口与消息结束

H2 的 DATA 同时受每流和连接窗口约束，WINDOW_UPDATE 增加的是额度增量；流控是逐跳的。题设 stream1 窗口1000、连接窗口600，则最多新发600 DATA payload byte。9-byte frame header 不计入这个窗口；若有 padding，按受流控的 DATA 长度计，不能只算业务 body。本例无 padding。只给 stream1 WINDOW_UPDATE+400，连接仍剩600，仍不能发1000。SETTINGS 初始流窗口变化甚至可使现有流窗口为负，这与 QUIC MAX_* 单调累计上限不同。

HEADERS 和控制帧不耗用 H2 的 DATA 窗口，仍受底层 TCP 排序和拥塞约束。一个8-byte响应可在同一客户端 stream1 上由 HEADERS(END_HEADERS=1, END_STREAM=0)、DATA8(END_STREAM=1) 完成。若 header block 分片，必须等紧接且同 stream 的 CONTINUATION 直到 END_HEADERS；只看到 END_STREAM bit，header block 却不完整，仍不能认定 HTTP 消息完成。完整消息还要满足字段与 content-length 等语义。依据：[RFC9113 §§5.2、6.9、8.1](https://www.rfc-editor.org/rfc/rfc9113.html#section-5.2)。

### 5.3 QPACK 把压缩依赖搬到了哪里

HPACK 利用连接上的字段压缩状态减少重复头部；QPACK 将动态表指令与 field section 分流，以适应 QUIC 各流独立到达。它减少传输排序带来的限制，同时引入必须显式管理的表依赖，而不是消除所有等待。

纸面输入：encoder 已合法插入6项，相关条目仍可引用且未 evict；decoder 只处理前4次插入，Insert Count=4。stream4 的合法 HEADERS 所需 Required Insert Count=6，容量足够，SETTINGS_QPACK_BLOCKED_STREAMS 至少允许1，而且当前还剩至少1个阻塞槽；插入5/6正在 encoder stream 上等待交付。stream4 字节即使全到，field section 仍不能解码。两次插入被处理后 decoder Insert Count=6，才解除这项依赖。

另一条 stream8 只用静态表/字面量、RIC=0，可以先解码。若实现必须等 field section 消费才放出连接或 encoder stream 的接收额度，而解码又在等该流的插入，就构成循环等待。发送与接收策略要保留关键指令前进条件；动态容量、两端计数、可用阻塞槽和流控状态必须分别追踪。依据：[RFC9204 §§2.1–2.2、5](https://www.rfc-editor.org/rfc/rfc9204.html#section-2.1)。

### 5.4 一个慢资源不足以证明传输队头阻塞

令资源 A 在应用中延迟2秒，B立即返回。没有传输丢失且应用不串行化时，H2、H3 都可能先完成 B。这个题设只测应用与调度，不能据此证明 TCP 丢包 HOL。真正的纸面对照应指定：TCP 前部有缺字节，而后来已收到的字节含 B 的 H2 帧；补齐前 TCP 无法把 B 帧交给 H2。已经完成的响应不会因此倒退。

第 3.1 节的 QUIC B 流不等待 A 流缺口，但共享 cwnd、MAX_DATA、QPACK 或磁盘队列仍可能拖慢 B。所以业务优先级还要观察依赖和资源，不应盲配旧 priority tree。RFC9113 §5.3 已弃用 RFC7540 的旧优先级信号；本文只保留调度目标及实际实现策略的记录用途，不以旧信号许诺服务顺序。

长连接的 idle timeout 要结合端点、中间设备和业务 deadline；PING 的回应或计时可观察该协议层活性，不能证明数据库健康或补丁可用。server push 仍是需明确是否启用的协议能力，本轮没有核对浏览器部署，不能将旧“多数浏览器弱化”的句子作为当前事实。依据：[RFC9113 §5.3](https://www.rfc-editor.org/rfc/rfc9113.html#section-5.3)；性能差异这里只作机制推导，没有 P95 提升实测。

## 6. 发现、排空与换协议不能丢掉请求状态（P6）

### 6.1 Alt-Svc 换目的地，origin 不跟着换

题设原站 `patch.example.test` 的权威响应给出仍新鲜的 `h3="edge.example.test:8443"`。客户端可以向 edge.example.test:8443 建连，但 HTTPS origin、TLS 参考身份和请求 `:authority` 仍是 patch.example.test。仅认证 edge.example.test 的证书不够；不能靠 `--insecure` 或修改 authority 绕过失败。

UDP443 / TCP443 是常见双协议部署选择，HTTP/3 并未要求所有 CDN 必须使用这两个端口。连接目的地、监听协议、LB 的 CID 路由、防火墙方向和 idle timeout 都需要各自证据。替代端点可达也不证明应用对象相同。依据：[RFC7838 §§2.1、2.4](https://www.rfc-editor.org/rfc/rfc7838.html#section-2.1)、[RFC9114 §3.1](https://www.rfc-editor.org/rfc/rfc9114.html#section-3.1)。

### 6.2 GOAWAY 的边界分类已经发出的尝试

以下每个请求只有一次尝试，无旧 early-data/重试副本，端点遵守规范。H2 服务端 GOAWAY Last-Stream-ID=5 表示 stream7/9 未被它处理，stream1/3/5 仍可能处理过。H3 服务端 GOAWAY ID=8 表示 stream8及更大的请求被拒绝，stream0/4仍可能处理过。H2 比较 `>5`，H3 比较 `>=8`，不能套同一算式。

收到 GOAWAY 后禁止在该连接发起新请求。上述 ID 是已有尝试的处理边界，不是还可以新开几个低 ID 请求的额度；新请求转到合规新连接。排空旨在停止接新工作并处理已有状态，不能把“连接在关闭”当作全部没执行。

H2 RST_STREAM(REFUSED_STREAM) 保证该流未交给可能采取动作的应用层；普通 CANCEL 没有这项保证。H3_REQUEST_REJECTED 是未进行应用处理的特定保证，H3_REQUEST_CANCELLED 不是。即便有未处理保证，也只覆盖所指的那次尝试，不能倒推另一个节点的旧副本没有执行。依据：[RFC9113 §§6.8、8.7](https://www.rfc-editor.org/rfc/rfc9113.html#section-6.8)、[RFC9114 §§4.1.1、5.2](https://www.rfc-editor.org/rfc/rfc9114.html#section-5.2)。

### 6.3 fallback 是新的尝试，必须继承原合同

H3 建连失败，且确认没有发送 0-RTT 或任何应用请求，可以在预算内新建 H2/TLS 再发。build42 只读 GET 已发但未得完整结果，可以保持同一 origin、表示和可信清单合同，按已验证缺口重新取；新连接取得的是新的 HTTP 表示片段，不能接续旧 QUIC stream bytes。普通 POST 已发后只有 timeout/断线，无未处理保证或有效服务去重合同，则标 unknown，停止自动重发。

每个代理 hop 也可能有连接池、超时与重试。客户端切协议、换 CID、换实例或指数退避，都不自动撤销代理曾发出的请求，更不产生重试授权。用同一逻辑请求及不同 attempt ID 追踪每次路径与预算，才能避免客户端和代理叠加重试。固定 curl 文档中的 `--http3` 可尝试较老 HTTP，`--http3-only` 不做这种 fallback；实际 build 能力和具体运行行为仍需另有观察，本文未运行。依据：[RFC9110 §9.2.2](https://www.rfc-editor.org/rfc/rfc9110.html#section-9.2.2)。

## 7. 地址变了、连接关了或提前发了请求，责任会变吗

### 7.1 CID 与路径验证共同支撑有条件迁移（P7）

成功纸例的输入是：连接已 confirmed，客户端保有 QUIC 状态，对端允许主动迁移，有可用 peer-issued CID，LB/路由仍把新地址流量送到原连接状态，目的服务器未变且新路径可用。客户端从新本地地址发非探测数据并启动路径验证。地址变化不要求重做原 origin TLS 握手，是这些前提满足时连接继续的性质，不是“换任意网卡必成功”。

验证者在待验证路径发 PATH_CHALLENGE payload X。对端必须沿收到 challenge 的路径发 PATH_RESPONSE；发起者收到匹配 X 的 response 时，验证的是发出 challenge 的路径，但不得额外要求 response 必须沿同一路径到达。任意 UDP 包、ACK 或不匹配的 Y 不能替代匹配响应。一方向完成不代表另一端独立验证完成。反放大受限时小于1200 byte 的探测可验证地址，却尚未证明路径支持所需 MTU，后面仍需足尺寸验证。

未 confirmed 不得主动迁移。disable_active_migration 限制从新本地地址向握手地址发送/探测，preferred_address 有规范例外；不可控的 NAT 重绑定不因此一律被视为攻击或直接关闭，但仍需验证。仅端口变化可允许保留拥塞/RTT状态，其他新路径按规则重置，不能承诺原吞吐。zero-length CID、无备用 CID、LB 找不到原状态、UDP 被挡、MTU 不足、idle timeout 等都可能让续接失败。有旧可用路径可继续，否则等待或关闭，并回到表示范围恢复；不能预先保证“迁移不增加握手次数”。

### 7.2 三倍反放大限制有地址和计量范围

服务端尚未验证客户端地址。题设归于该连接的已收 UDP payload 共1200 byte，其中包括可唯一归入该连接但包被丢弃的 datagram；已发3000 byte。因此还剩 `3×1200−3000=600 byte`，不是每收到一包都能任意再发3600。成功处理 Handshake 包或有效地址 token 等条件完成地址验证后，该地址不再受这项三倍限制，拥塞等约束仍在。

迁移后的新 peer 地址尚未验证时，不能把旧路径大量收包算作新地址额度。这不是整个连接永久的三倍总量限制。客户端仅换自己的本地地址、仍向已验证服务器地址发送，也不自动构成客户端→服务器方向的未验证 peer 地址题设。Retry/token 支持地址验证与反放大，不是应用去重令牌，也不认证 origin。依据：[RFC9000 §§5.1、8、9，尤其9.3.1](https://www.rfc-editor.org/rfc/rfc9000.html#section-9.3.1)。

### 7.3 有状态关闭与无状态重置

CONNECTION_CLOSE 是仍有连接状态时的关闭信号，应按 transport/TLS/application 错误层解释。Stateless Reset 则是在无法访问连接状态等条件下可用的终止机制；接收端要验证不可预测的 reset token。未知 UDP 包不是任意回 reset 的理由，随机报文也不能当合法 reset。服务重启可能丢失连接状态，但观察到 reset 只提示这类可能性，不能推出此前应用没有执行。恢复仍回到第 6 节的尝试状态和第 8 节的对象合同。依据：[RFC9000 §10.3](https://www.rfc-editor.org/rfc/rfc9000.html#section-10.3)。

### 7.4 0-RTT 的收益不能靠方法名换来（P9）

主例默认不发 early data。若另行允许：已有同一先前连接得到的会话状态、参数和 H3 配置，应用明确允许这个不可变补丁用 early data，跨实例策略一致且接受重复读取的资源成本，才进入该资源级案例。会话恢复成功和0-RTT被接受是两件事；PUT 幂等也不等于 safe method。一个 GET 若消耗下载券或触发付费任务，仅凭 GET 名称不能证明可重放。

具体反例：同一 early request E 被复制到 A、B。A 接受并执行一次；B 拒绝0-RTT，随后完成1-RTT。客户端只收到 B 的拒绝，于是重试 E，B 又执行一次。即使每个节点每份 early handshake 最多接受一次，全系统仍发生两次业务。应用去重若要承担恢复保证，必须明确作用域与参数、共享或固定路由的一致记录、与副作用原子提交、并发隔离、保存期及过期后的 unknown 处理；一个 HMAC token、Retry token 或 session ticket 不能替代这些。

TLS 拒绝0-RTT的连接不得处理被拒绝的 early packets。客户端须重置基于旧参数的所有 stream 及绑定应用状态，再按新协商状态决定重发或放弃；这个重置不重置本连接已发的 Application Data PN，后续1-RTT不能复用0-RTT包号。Retry 本身不表示拒绝0-RTT。HTTP425 表示接收者不愿承担 early-data 风险，重试不得再用 early data；它同样不证明别处副本回滚。一般 timeout、reset、fallback 仍按副作用 unknown 处理，确定终态不会被之后断开倒改。

依据：[RFC9001 §§4.6、5.6、9.2](https://www.rfc-editor.org/rfc/rfc9001.html#section-4.6)、[RFC8446 §8](https://www.rfc-editor.org/rfc/rfc8446.html#section-8)、[RFC8470 §§3–4、5.2、6.2](https://www.rfc-editor.org/rfc/rfc8470.html#section-3)。这里是跨节点反例，不是生产反重放测试。

### 7.5 DATAGRAM 适合另一种交付合同（P10）

遥测消息 D 可以容忍丢失时，双方可协商 QUIC DATAGRAM 扩展；帧须满足 peer max_datagram_frame_size、UDP payload 与路径 MTU。交给 QUIC 后仍可能因拥塞等待、过期或接收资源不足而被丢弃。DATAGRAM 不占 MAX_DATA/MAX_STREAM_DATA 的 stream 额度，却仍受连接路径拥塞控制；QUIC 不为它分片，也不在丢失后重发该 frame。承载包的 ACK 最多说明传输层接收处理，不证明应用消费 D。

补丁缺一个 byte，仅有“DATAGRAM 发送成功”仍不能恢复或通过摘要。普通 H3 响应继续走可靠 stream；若设计 DATAGRAM 可靠文件协议，还需自己的排序、重传、身份与完整性合同，本文不实现，也不把 QUIC DATAGRAM 当成 HTTP Datagram 映射。依据：[RFC9221 §§3、5](https://www.rfc-editor.org/rfc/rfc9221.html#section-5)。

## 8. 回到 build42：从条件 Range 到受控可用文件（P8）

前面各层最终要回答：现在收到的 DATA，能否补到0..7的可信前缀后面？Range 是 HTTP 表示的字节坐标。QUIC stream 中还有 HEADERS、frame header 和压缩字段，因此 stream offset 不能直接写入文件相同位置；只有经过 HTTP 解析的内容字节，才能按已验证的表示范围交给候选管理器。

### 8.1 先固定请求、字段与完整消息前提

所有下列纸例彼此独立，均从第1.1节起点开始。H3 已初始化，SETTINGS、流数与额度足够，QPACK field section 可合法解码。请求在客户端双向 stream0 上，字段如下；pseudo-fields 在前，字段名小写，HEADERS 完整，无请求 body，正常 FIN。这里给解析后的语义输入，没有声称生成或验证实际线上 QPACK 编码。

| 请求字段 | 基准值 |
| --- | --- |
| `:method` | GET |
| `:scheme` | https |
| `:authority` | patch.example.test |
| `:path` | /builds/42/client.bin |
| `range` | bytes=8-15 |
| `if-range` | "build42-identity" |
| `accept-encoding` | identity |

每个响应都沿同一 stream0 的服务器方向返回。共有字段为 `date: Wed, 07 Oct 2026 06:00:00 GMT`、`cache-control: no-store`、`vary: accept-encoding`、`content-type: application/octet-stream`。下表逐例给出 status、ETag、Content-Range、Content-Encoding、Content-Length、准确 body 及结束方式；未列请求差异就沿用上表全部字段。没有中间响应、trailers、connection、transfer-encoding 等字段。

“无编码字段”表示省略 Content-Encoding，即 identity，不发送 `content-encoding: identity`。正常分支的 HEADERS 与 DATA frames 都完整；body 无换行，最后正常 FIN，Content-Length 只数 DATA 内容，不含 frame header/QPACK。正常 FIN 只说明 stream 的最终长度，仍须补齐 QUIC 缺口、解析完整帧并满足 HTTP 语义。

### 8.2 十二组完整响应：协议允许与本例策略分开

表中“基准 ETag”精确为 `"build42-identity"`；“无 Range 字段”指不发送 Content-Range。gzip 字节 G 在表后完整给出。

| 例 | 请求差异 | status / ETag / Content-Range / Content-Encoding / Content-Length | body 与结束 | 判定及下一动作 |
| --- | --- | --- | --- | --- |
| B01 | 无 | 206 / 基准 ETag / bytes 8-15/16 / 无编码字段 / 8 | `ijklmnop`；完整 DATA、正常 FIN | 15−8+1=8，区间与表示一致；可构成完整候选，经过稳定快照校验及本地发布才能称可用 |
| B02 | 无 | 200 / 基准 ETag / 无 Range 字段 / 无编码字段 / 16 | `abcdefghijklmnop`；完整 DATA、正常 FIN | 服务端可忽略 Range；建立独立全量候选，绝不 append 到8-byte前缀变成24 byte |
| B03 | 无 | 200 / "build43-identity" / 无 Range 字段 / 无编码字段 / 16 | `ABCDEFGHIJKLMNOP`；完整 DATA、正常 FIN | If-Range 不匹配返回整份200可以合法；固定 build42 目标却失配，停混合、重核可信清单，不能擅自升级 build43 |
| B04 | 无 | 206 / 基准 ETag / bytes 9-15/16 / 无编码字段 / 7 | `jklmnop`；完整 DATA、正常 FIN | RFC允许返回所请求范围的子集；仍缺 byte8。本例严格客户端拒绝此次合并，不把它称为协议违法 |
| B05 | 无 | 206 / 基准 ETag / bytes 8-16/16 / 无编码字段 / 8 | `ijklmnop`；完整 DATA、正常 FIN | last>=total，Content-Range 无效，必须禁止重组；framing恰好8 byte也救不了表示语义 |
| B06 | 无 | 206 / W/"build42-identity" / bytes 8-15/16 / 无编码字段 / 8 | `ijklmnop`；完整 DATA、正常 FIN | 弱值不满足强 If-Range，构成条件处理不一致反例；停止部分重组，后续只能按独立认证完整对象合同恢复 |
| B07 | accept-encoding=gzip，其余请求字段不变 | 200 / "build42-gzip" / 无 Range 字段 / gzip / 36 | 完整 G；完整 DATA、正常 FIN | 变体改变，强比较不匹配后返回完整gzip表示可合法；不能拼入identity前缀，不能把16当编码后长度 |
| B08 | 无 | 206 / 基准 ETag / bytes 8-15/16 / 无编码字段 / 8 | DATA声明8，只到 `ijkl` 后 reset/timeout；无 clean FIN | HTTP不完整；保留已验证0..7，新4 byte没有分片验证，不能升级为已验证；先退场旧写者再决定恢复 |
| B09 | 无 | 416 / 基准 ETag / bytes */16 / 无编码字段 / 0 | 空 body；完整 DATA内容总长0、正常 FIN | 8..15对长度16原本可满足，故该响应异常；停止盲重试，核请求/服务合同，长度16不是完成证明 |
| B10 | range=bytes=16-，其余请求字段不变 | 416 / 基准 ETag / bytes */16 / 无编码字段 / 0 | 空 body；完整 DATA内容总长0、正常 FIN | 这是正常不可满足范围；独立题设本地物理length16但仅0..7已验证，错误把length当进度；修正范围状态，不据416宣称完整 |
| B11 | 无 | 206 / 基准 ETag / bytes 8-15/16 / 无编码字段 / 8 | `Ijklmnop`；完整 DATA、正常 FIN | 元数据与framing通过，但完整摘要失败；停止发布/安装，保留错误候选，不能自算新hash替代可信值 |
| B12 | 无 | 206 / 基准 ETag / bytes 8-15/16 / 无编码字段 / 8 | `ijklmnop`；完整 DATA、正常 FIN | 候选完整摘要通过；题设本地publish明确失败且可用槽未切换，只有验证过的临时内容，走本地恢复而非自动重下 |

B04 要求格外注意策略边界：RFC 的子集响应仍有用途，只是本例客户端仅接受精确8..15。若项目另有部分区间合同，可以把缺口 byte8 纳入新计划、重新计算 deadline 和尝试预算；当前不能默默把7 byte当成8 byte，或谎称协议禁止子集。B06 也不是建议客户端发送弱 If-Range；如果恢复起点仅有弱 ETag，客户端根本不得据此生成 If-Range。

B07 的 G 是固定36-byte gzip表示，十六进制为 `1f8b08000000000002034b4c4a4e494d4bcfc8cccacec9cdcb2f000093c03a9410000000`。它只说明编码表示身份独立；Range 偏移针对编码后的 octets，不能把压缩前16-byte范围挪到gzip表示。这里没有执行目标解压或网络请求，也没有把 G 当作通过原 identity 清单验证的可用候选。

B01 的后半块 SHA-256 为 `d2789a29f48befe96a70ef3e3eaf57975f692165155e96880b310c82ac012ebf`，完整候选为第1.1节可信16-byte对象。这个后半块摘要是纸面字节核对值，题设清单未单独授予中断后任意新片段的信任。B03 全部大写 body 的 SHA 为 `e7e8b89c2721d290cc5f55425491ecd6831355e91063f20b39c22f9ec6a71f91`；B11 拼成 `abcdefghIjklmnop` 后为 `e8d6ace71f66c7629031ed04416b5837e115b51c9200458b114e6228f8ec39c1`，均不同于可信完整摘要。

因此 200/206/416 都不能单独决定成败。200可能是合法全量替代，也可能是新版本；206可能少一段、范围无效、validator不一致或内容错误；416可能是合法不可满足，也可能反映请求/响应异常。决策要同时保留字段、body、framing结束和本地验证状态。依据：[RFC9110 §§8.4、8.8、13.1.5、14、15.3.7、15.5.17](https://www.rfc-editor.org/rfc/rfc9110.html#section-13.1.5)。

### 8.3 FIN 不是合法 HTTP 的替代证明

再固定三个独立 framing 反例，连接、请求和 B01 响应字段均保持第8.1节的共有前提；差异只在响应帧序列：

| 例 | 准确响应序列 | 错误与停止点 |
| --- | --- | --- |
| F1 | 第一个frame为DATA(Length=8,payload=`ijklmnop`)，之后才是B01的HEADERS，最后FIN | DATA出现在初始HEADERS前，H3_FRAME_UNEXPECTED连接错误；不可容忍后使用body |
| F2 | B01合法HEADERS含content-length=8；完整DATA(Length=7,payload=`ijklmno`)；正常FIN | frame本身完整，内容总长7≠8，H3_MESSAGE_ERROR流错误；FIN不能补缺失内容 |
| F3 | B01合法HEADERS；DATA声明Length=8却只有`ijkl`；随后clean FIN | 最终frame截断，H3_FRAME_ERROR连接错误；不得当作完整响应 |

B08 可以在 frame 任意位置被 reset，它与 F3 的 clean FIN 后发现截断不是相同分类，但两者都不能提供完整 body。对于 H2，也必须同时满足第5.2节的 END_HEADERS/CONTINUATION、END_STREAM 与语义检查，不能只用一个终止 bit。依据：[RFC9114 §§4.1、4.1.2、7.1](https://www.rfc-editor.org/rfc/rfc9114.html#section-4.1.2)。

### 8.4 单写者、稳定候选与发布回执（P8-L）

假定下载管理器独占 candidate C，所有权绑定唯一 generation g42，只有写者 W 能提交异步写入，其他进程或旧 attempt 不得并发修改 C。g42 是状态表的符号，每次实际尝试使用不同唯一代号，例如 g42-a1、g42-a2，并从写入、核验到回执保持一致。旧前缀须重新证明来自同一可信清单且稳定；文件名/长度相同不是证明。

这是一份应用接口题设，RFC 不替文件系统定义事务。对网络 frame 的解析交付、写任务所用缓冲区的所有权和 I/O 完成责任，必须由实现提供明确合同；本例不隐含一个可供多线程随意共享或提前释放的 frame 对象。以下状态只描述已经满足单写者前提的候选生命周期：

| 状态与证据 | 当前结论 | 动作或停止点 |
| --- | --- | --- |
| L0：HTTP完整，W仍有pending写入 | 网络结果已到，候选仍可能变化 | 停止新入队，等每笔待完成I/O的真实结果；不得先hash、rename或delete |
| L1：取消/STOP_SENDING已发，C仍有引用，W退出未确认 | 只能说取消已请求，不能说写者退场 | 等W退出/任务join和I/O完成回执；无证明则保留候选及ownership unknown，不清理或重用 |
| L2：W明确退出，所有写入成功完成，无外部写者，锁定g42 | 存在稳定候选快照 | 核长度16及可信完整SHA；任何可能修改或generation变化都会使旧校验失效 |
| L3：长度或SHA不符 | 内容验证失败 | 禁止发布/安装；只隔离已拥有且静止的候选供诊断，删除需另满足所有权、无读写者及保留规则 |
| L4：长度/SHA通过，尚未调用publish(C,g42) | 仅验证过的临时内容 | 由同一管理器调用题设发布接口；hash PASS不能冒充已可用 |
| L5：publish明确失败，接口保证未切换可用槽 | 临时内容验证过，槽未变 | 记失败；本地合同允许时重试发布，不自动下载；槽或候选变化则重新核验 |
| L6：publish明确成功，回执绑定build42/g42/可信摘要和受控槽 | 可以宣称build42下载并验证可用 | 交给用户或后续安装流程；不宣称已安装或断电持久性通过 |
| L7：publish超时或进程中断，未得结果 | 发布unknown，不能推槽未切换 | 用本地事务ID和槽状态对账；禁止万能清理、覆盖已发布槽或盲重发布 |

任一写入明确失败时，记录实际错误并停止进入L2/L4；必须先确认W和其余I/O退场，再按本地所有权合同隔离候选。某笔I/O结果仍不明时，停在L0/L1的等待或unknown状态，不能因文件length碰巧为16就提前校验、清理或发布。

成功回执只保证当前进程/应用可见的受控 build42 可用槽。要承诺断电持久性，还需要平台文件/目录持久化等另一个合同及证据，本文未认证。引用计数和文件句柄寿命解决可访问性，不能替 W 的退出证明；关闭网络连接也不取消已提交磁盘 I/O。L7 不能偷换成 L5：结果未知时，错误重试与清理可能破坏已经发布的内容。

### 8.5 一次中断怎样在预算内恢复

合成网络预算为最多2次 attempt，总 deadline t=10s，origin和对象不变。首次 t0 开始，t2 响应中断。管理器停止接收和新写，旧 W 于 t2.05 明确退场，旧候选隔离完成；以中断时刻计150ms退避，t2.15 开始第二次，使用独立 g42-a2 并重新核验可信前缀。t3 收到完整 B01，t3.05 新 W 退出且候选稳定，t3.06 摘要通过，t3.07 得到 publish 成功回执，才确认可用。

如果 t2.15 旧 W 仍未退出，等待150ms本身不构成退出证据，不得重用其候选；等退出后重新检查剩余 deadline 和次数，不足就停止自动网络尝试。第二次失败、身份失败或表示失配也停止。新到的 `ijkl` 若没有可信局部分片验证，仍不能把恢复起点推进到12。网络恢复、候选验证与本地发布各有预算和结果；L5本地失败不应浪费网络重下，L7则先对账。表内时间全是 PAPER_EXPECTED，没有测得3.07秒下载成绩。

## 9. 怎样用静态例子训练排障，而不把猜测写成证据（P11）

### 9.1 先识别观察层，再提出相容原因

遇到“下载很慢”，先把现有证据定位到路径/握手、QUIC恢复与额度、HTTP/QPACK、应用/候选写入、宿主这几个层次。PTO增加可以与数据丢失、ACK丢失、调度延迟相容；一个 HTTP 错误可能来自代理，也可能来自源站；完整响应后仍不“可用”可能是 pending I/O，也可能是 publish unknown。每个假设都应指出能区分它与其他解释的观察，不能从一个计数直接跳到根因。

未来若有授权的 pcap，`UDP443` 过滤只覆盖该抓点、方向和端口，本例 Alt-Svc 的8443就不会因此出现。Wireshark dissector 的版本、配置与解密权限决定可见字段；没有授权密钥或端点日志，不能从加密报文假装看见全部 HTTP。qlog 则依赖实现和 schema/版本、端点、时钟及采样；单点缺少事件不能推断对端没执行。本轮只核固定 logger 接口，没有生成 qlog/pcap，也没有收集解密材料。

ECN 是否被路径清除、PMTU 是否黑洞、防火墙/NAT 是否超时，都要端点、方向、时期和设备合同支撑。地址验证成功不等于MTU验证成功；单次超时同样可能由队列、CPU或应用暂停引起。没有这种证据时记录“未观察”，不读取或修改设备来替论文式例子补一个结论。

错误码要区分 QUIC transport、TLS、H3 与应用层，并关联连接/方向/PN空间/stream/逻辑请求/attempt/实例。CONNECTION_CLOSE 可按目标采样，但必须披露采样和丢采样，不能把没日志写成没关闭。握手 complete/confirmed、客户端首字节 TTFB、丢包恢复进展、稳定候选和发布回执的起止不同；把它们全叫 RTT 或恢复时间会失去定位价值。

### 9.2 把旧弱网与延迟题设改成可手算的对照

若去程40ms、回程10ms、两端处理共5ms，则一次往返是55ms。只给“server egress delay40ms”不能推出 RTT=80ms；挂载方向、基线、另一方向与排队均未知。历史 netem delay/loss/rate 是实验设计输入，不能当作实测 RTT。当前用第3节的包缺口/ACK顺序，以及第5节的 TCP 缺口和 QPACK 依赖，分别表达丢包、乱序和等待，不执行 tc 或 netns。

控制器或协议对照应固定应用处理路径、对象与变体、连接数、流数、路径、MTU、CPU与实现版本，再解释更换 H2/H3、CUBIC/BBR、单/多流、冷/热连接所改变的机制。CPU、系统调用、用户态加密、内核和驱动开销是可能混杂，当前没有 profiling 或容量数字。迁移次数也区分尝试、challenge完成、地址/MTU验证及请求继续结果，不能只数网卡切换。服务重启、NAT变化、key update 分别影响状态，不能揉成“弱网一次成功”。

### 9.3 尾延迟、goodput 和失败都要有分母

给定100个合成完成耗时：98个20ms、一个120ms、一个200ms。nearest-rank 的 P95 是第95项20ms，P99是第99项120ms。这个输入明确没有超时，不能据此推真实网络分布；未来有失败/超时时，应保留全部请求总体，说明截尾和单独报告方式，不能只保成功样本然后称全请求P99。历史“至少30次”或“三次”是设计起点，不自动提供稳定尾分位或置信范围。

有效负载1,048,576 byte在2s完成，对应goodput=524,288 byte/s=4.194304 Mbit/s。这里的有效内容、时间窗口都是题设；链路速率、协议头和加密开销另算，不能把goodput叫带宽上限。平均吞吐、P95/P99、失败/超时、内容校验与可用回执必须一起看。重试提高attempt数，不等于提高成功逻辑请求率。

### 9.4 发布功能开关也必须解释在途结果

灰度设计应明确按用户/请求/连接哪个分母、观察窗口、所选协议与cipher、身份失败、内容校验失败、业务unknown和可感知延迟。代理/CDN升级需要相同对象和应用路径的兼容对照，不能以功能flag存在代替回归。阈值触发后由谁停止新流量、如何处置在途请求、何时恢复H3都须有责任；当前只有这些设计条件，没有真实灰度流量或告警。

关闭 UDP 入口、回滚配置可以阻断后续传输，却可能让在途副作用变成 unknown，不能撤销已发生业务。它也不同于第8节把某一候选发布到可用槽。故任何缓解、永久修复与回归建议，都应写对象、前提、风险、验证条件和停止点，不能靠“回滚了”省略恢复责任。

## 10. 将150条旧记录项用作有条件的证据合同

历史模板逐字保留在后面；下面按判断任务合并解释其当前用途。每个字段只有在对应对象、来源和观察条件具备时才填值，否则写“未观察”。它们不是缺陷数，也不是已采集的150份证据。当前可填的是明示纸面输入和静态字节结果。

### 10.1 让两份记录具有可比的身份

环境记录首先解决“比较的是不是同一条件”。主机型号、内核、网卡、MTU和时钟源用于界定宿主与路径边界；客户端、服务端、代理须分别给精确版本及构建能力，不能从curl版本号推HTTP3可用。CPU governor、进程亲和性影响调度，应与负载时期对应；开始/结束用明确时区关联，耗时用合适的单调时钟。历史Ubuntu等标签只是原方案，本轮这些字段均未观察，不能继承成当前机器配置。

可追溯性记录也有安全边界。公开证书指纹须连同origin、信任校验和ALPN选择使用，指纹单独不是身份成功。只记录影响结果的非秘密环境配置和必要脱敏说明，纠正旧模板“所有环境变量”的过宽口径；不要导出凭据。脚本或实现commit标识实际比较版本，结果文件SHA256确认保存字节一致。二者都不能证明脚本已运行或协议正确；本轮只有固定源码阅读及静态文件指纹。

对象记录把连接数、流数、对象长度和表示变体固定下来，防止把更多连接的吞吐当成多流免费收益。内容SHA-256要区分可信完整摘要、可信分片摘要与仅对收到body自算的值；响应体校验和不自动属于发布清单。断点记录已验证字节集合，ETag/If-Range按相同URL及变体的强比较使用，不能只记文件length。第8节B02/B07/B10分别揭示全量append、编码错配和未验证尾部的失败，字段不全时停止拼接。


### 10.2 先定义结果总体，再计算统计

结果总体应同时保留成功、失败、异常与超时。成功分母区分网络attempt、完整HTTP、验证对象和可用回执；HTTP状态码要连同framing及业务终态解释，206不自动成功。异常原样保留，剔除规则预先定义，并同时报告剔除数量和未剔除口径，不能删除坏样本改善结论。客户端超时是被deadline截尾的观测，超时分位须说明整体期限和截尾处理；只有成功耗时不能代表全部请求。

时间统计要先选事件。DNS耗时区分缓存、真实解析和复用连接；证书验证耗时与完整握手分开，握手RTT注明冷热状态及complete/confirmed端点。TTFB说明从何时起算、首个哪层字节到达，不能全归为服务计算。旧“恢复时间RTO”在当前记录中指明确故障到业务恢复的耗时，QUIC PTO、TCP RTO和恢复SLA不能混用。均值、中位数、标准差标总体及单位，P95/P99注明算法、样本数和超时处理；第9.3节只演示100个合成样本，未产生实测分布。

吞吐记录区分每秒attempt、成功逻辑请求和业务终态；连接复用比例说明分母按请求还是连接，以及冷热状态。goodput只数已验证有效内容和明确窗口，HTTP编码、QUIC/UDP/IP头及加密开销另列，文件byte数不能代替stream或链路byte数。带宽利用率的分母须是真实链路容量且与开销口径匹配；没有容量证据就不报利用率。第9.3节的4.194304Mbit/s只是题设goodput，不是带宽上限。

资源和复现率也需要总体。CPU百分位说明按时间、核心还是进程采样，RSS内存和socket数说明进程范围、协议对象及时期；两者同时增长不自动证明内存泄漏。失败复现概率要给独立尝试次数、相同条件及不确定范围，不能用一次失败估概率。本轮没有CPU、RSS、socket或复现概率观测，故不能据纸面例子推出容量余量。


### 10.3 对照应改变机制，不能同时改掉全部前提

重复对照用于看变异性，次数本身不能补救设计偏差。H2/H3比较需同应用路径、对象和负载；CUBIC/BBR需固定实现版本、路径和连接数；单流/多流仍要固定连接数。冷/热连接须区分握手、会话、缓存和复用，1MiB/100MiB比较须保持表示与完整性口径。历史至少三次、30次和固定预热时长是未来设计材料，不是通用充分门槛；当前16-byte模型只揭示机制，不预测大文件性能。

切换条件也会引入混杂。不同网卡会同时改变驱动、路径和MTU，不能据重复切换就证明CID保证续接；MTU对照区分接口配置与路径支持，小探测成功不能代替足尺寸验证。IPv4/IPv6应分别固定目标、路由和表示，地址族常常不是唯一变化。0/1RTT对照还要记录接受、拒绝与重试，以及资源重复成本，不能仅因方法名相同认为安全。没有对应输入就停止性能或安全比较。

故障模型先写输入和方向。netem的挂点、delay/loss/rate及统计窗口只描述指定方向；5%突发丢包需给突发分布和观察点，200ms抖动需给采样规则和双方向条件，不能转写成已测RTT。服务重启纸图检查连接状态/CID丢失及应用恢复；客户端切网区分主动迁移、NAT变化和新连接；MTU黑洞只是一个候选解释，单次超时也可能来自ACK丢失或队列。当前用第3、7、8节状态追踪，不注入这些故障。

失败位置决定动作。DNS暂时失败只说明解析阶段，已有连接未必需要新解析；证书过期停身份验证，不关闭校验；错误ALPN使应用映射前提不成立。UDP阻断时区分尚未发应用请求和已发unknown，按第6.3节恢复。CID异常要先区分分配、路由和状态问题，不能凭“冲突”一词定位漏洞；小窗口按第4.1、5.2节分别计算stream和connection额度，不能与cwnd混算。这里没有改DNS、防火墙、证书或窗口配置。


### 10.4 状态记录必须带方向、空间和作用对象

恢复记录先绑定路径、方向与PN空间。PTO次数附退避阶段，不能当物理丢包数；cwnd最小值附算法和恢复阶段，不能当接收额度。旧“重传包比例”改为分别记录算法判丢packet、新PN内再次发送的STREAM字节和各自分母，不能把旧packet identity原样重传。ACK delay记录已解码单位和peer报告性质，max_ack_delay、实际delay与confirmed状态分开；RTT扣delay规则不等于PTO的Initial/Handshake零项。pacing包间隔需真实时钟/调度观测，策略名称不证明体验更好。

迁移记录以同一连接串起设备切换时刻、地址变化、CID使用和请求进度。次数分别数尝试、验证成功与继续服务，不能把每次网卡事件都算成功迁移。初始CID长度说明路由及zero-length限制，不是越长越安全。新路径验证结果要写challenge匹配、地址验证和MTU验证，ACK不是PATH_RESPONSE。第7.1节的匹配response证明哪条路径，不能由它的到达路径额外编造验收条件。

网络策略记录注明来源与范围。NAT映射超时须来自实际配置或设备合同，防火墙快照须注明挂点、协议、方向和时间；未知就不归因设备。服务端限流事件另记主体、阈值、窗口和错误，不能将策略拒绝当网络丢失。Retry token过期率应以相关验证尝试为分母，区分地址拒绝、身份失败和业务拒绝。反放大阻塞/拒绝计数带未验证地址的收发预算；Stateless Reset计数需验证token并关联连接，不能拿次数证明此前无业务提交。本轮没有读取这些设备或计数。

额度和取消记录需要方向。流控blocked事件给streamID、两级上限和占用，单流阻塞不表示全部流都停；STOP_SENDING给发起方、接收方向、状态与应用原因；RESET_STREAM给发送方向、error和final size，并核合法性及不退款。两者都不能证明写者退出或业务回滚，需第8.4节回执。DATAGRAM丢失率另外定义生成、发送、接收或消费分母，packet ACK不能替代应用消费，PTO也不能充DATAGRAM丢失分子。

HTTP压缩与关闭记录保留精确依赖。QPACK阻塞时间从某field section的RIC未满足到解除，连同encoder/decoder计数、动态容量、可引用条目和剩余阻塞槽记录；容量不是Insert Count，笼统“插入阻塞流”无法解释谁在等谁。H3控制流错误核首SETTINGS、唯一性和关键流关闭；H2 GOAWAY记录error及Last-Stream-ID，按第6.2节区分可能处理范围。QUIC transport、TLS和H3错误分层统计，普通关闭不是未处理保证。无状态上下文就停止因果归类。


### 10.5 代理、应用和宿主仍共享工作

代理记录把池大小绑定origin、协议、上游实例和并发；池复用不证明同一业务执行者。每跳重试策略需未处理/去重依据与预算，连接、读取和整体deadline分别列出，避免客户端与代理叠加重复。代理协议转换会重新映射连接/stream，一个hop的GOAWAY不能推广到全部副本。应用取消比例以明确请求总体区分本地取消、取消信号已发和服务终态，timeout或取消都不等于事务停止。

应用等待要用自身span证明。服务端排队需请求关联和服务端时钟，客户端TTFB不能全量倒推出队列；磁盘读取时间是独立共享依赖，可让不同QUIC流一起变慢。缓存命中区分CDN、代理与应用，并绑定对象版本；压缩算法和等级同时改变表示字节与CPU成本，Range仍针对编码后octets。不知道缓存层或变体时，停止把两份耗时当同对象的协议性能差。

调度记录先确认实际优先级方案与实现策略，不能盲用已弃用的H2旧信号。优先级反转案例需指出等待的依赖或共享资源；大流拖慢小流时分别检查cwnd、MAX_DATA、QPACK与应用队列，不能笼统宣称H3完全隔离。连接公平性应定义共同瓶颈与每连接分配口径，多连接竞争与同连接多流不同；没有瓶颈和负载证据就不报公平性胜者。

宿主队列记录不能冒充线上丢包。socket buffer是宿主队列，不是MAX_DATA；UDP receive-buffer drop须对应socket与时间增量，不直接等于网络在途丢包。softnet backlog需采集CPU与时间上下文，网卡ring buffer需驱动、设备和队列范围，配置值本身不是丢失证据。这些保留为未来定位用途，本轮未查询内核、sysctl或网卡，更未调整配置。

CPU归因把接收负载分布与内存概念分开：RSS接收队列不是进程RSS内存；中断CPU分布要与亲和性、负载和时期关联。用户态加密CPU另于应用与内核成本，QUIC也不被RFC限定为用户态。GC或allocator暂停可能同时影响多个请求，需真实profiling和相容反例才能归因。本轮这些都未观察，不把相关性写成唯一根因。


### 10.6 身份与会话寿命不可混记

密钥更新次数绑定连接和key phase，1-RTT key update不重置PN，也不等于重新证明业务结果。会话恢复成功率用恢复尝试为分母，另记0-RTT接受/拒绝和之后重试；恢复成功完全可能伴随early data被拒绝。第2.2、7.4节只给状态条件，本轮没有握手或密钥更新事件可填。

证书链长度只是身份输入的一部分，还须名称、信任和握手证明，不能仅从链长判断安全或耗时。OCSP/Stapling记录实际客户端支持和验证策略，不把某客户端行为称为所有TLS实现的强制合同。客户端时钟偏差需有观察来源，可能影响有效期与票据窗口；本文没有访问或校准主机时钟，没有填造吊销检查结果。


### 10.7 采集方法决定哪些事件可以相连

原始qlog须保留而不覆盖，并绑定连接、端点、实现commit、schema和采样；解析工具版本必须与输入对应。本文只静态核到aioquic 1.2.0 logger的0.3常量和目录接口，不认证通用qlog schema或任意解析器兼容性，也没有生成本轮qlog。解析器无输出不能单独证明没有协议事件。

采集原件连同方法保存。pcap附抓点、方向、过滤器和丢采样范围；Wireshark版本、配置及解密权限限制可见性。工具stdout、stderr和退出码共同说明实际发生的结果，缺分流原流就明确缺失，不能按预测输出补造。netem统计只能说明未来指定挂点与窗口，不能从单向参数推出RTT。本文历史工具未运行；已有取源失败按原返回保留，不伪造成目标HTTP测试。

随机化和采样记录用于解释偏差。随机种子要连算法及版本，单独数字不能复现输入；压测顺序影响缓存、热身和时间漂移，需记录实际随机顺序。热身剔除规则应先定并保留原样本，历史60/300秒不是通用标准。日志采样比例及丢采样决定计数和缺事件的含义；本轮固定纸面事件不伪造随机种子、压测次序或采样结果。

跨层关联区分逻辑请求和每次attempt。traceparent是否经过代理保留须逐跳确认，不能假设自动传播；请求关联ID用于连接fallback、重试与候选generation，服务端实例ID用于识别跨节点执行及去重范围。没有这些关联或可靠时钟，就停止跨日志拼接因果链，更不能把节点本地去重推成全局一次。

位置与接入条件只取解释问题所需精度。地域是路径分布条件而非个人精确位置；NAT类型必须有可验证定义，名称不自动推出可迁移。移动网络切换事件应关联IP/端口、路径和请求；Wi-Fi漫游可能改路径，也可能地址不变，SSID变化不能直接算QUIC迁移。本轮不采集真实用户位置或做NAT探针，没有续接成功率。


### 10.8 恢复、发布和证据保管必须有人负责

恢复记录从已知请求状态起步。fallback次数区分尝试和成功，触发条件区分建连未发与已发unknown；恢复启用H3需要新的路径、身份和协议证据及预算，不把一次失败永久缓存为不可用。重试次数、退避上限受同一请求身份、旧结果和总deadline约束，等待不能创造重试权限。用户错误分别说明身份失败、HTTP不完整、业务unknown或本地发布失败；第8.5节最多两次是该题设限制，不是通用参数。

发布记录区分功能灰度和文件可用槽。发布前门槛要覆盖协议语义、身份、内容完整性及真正检查结果；灰度比例注明主体、分母和观察窗口，回滚阈值覆盖错误、unknown、校验失败和延迟。告警触发时刻要说明采样、聚合窗口与通知延迟。回滚记录原配置、目标、责任和复验条件；关闭UDP不撤销业务，候选publish失败/unknown则按L5/L7处理。本轮没有实际灰度、告警或配置回滚。

复盘要能解释正反观察并排除替代原因。临时缓解与永久修复分别列前提、风险和验证条件，新增指标须服务某个未决假设并定义分母，仪表盘不等于修复。回归入口在本轮是P0–P11的静态问题和完整纸例，历史命令留作原件，不新增runner或CI。容量余量只有负载、资源和瓶颈证据齐备才可报告；本文不据字节算术编造容量数。

实验与清理先确认作用对象。未来压力工作需要具体目标、时间窗口、授权与停止条件；tc恢复需精确原状态与授权挂点，netns处置需所有权、引用/进程退出和基线。unknown时不得“统一清理”别人的配置或还活着的资源，清理后也需验证恢复。当前只保留这些历史用途，不创建/删除namespace，不运行或新增网络清理命令。

证据保管应保留关联能力并最小化敏感内容。脱敏规则注明哪些标识仍可连接事件；证书私钥有独立授权保管与清理责任，秘密本身不得进入报告，本轮未生成、读取或删除私钥。pcap访问和分享限定必要对象及已授权接收者，qlog保留期按诊断、隐私和存储用途决定。这里没有pcap或新qlog，不能声称已配置访问权限和真实保留策略。

交付责任记录要诚实。实验责任人对环境、授权和异常停止负责，评审人应区分机械检查、来源支持与独立语义审查；作者自检不叫独立审核。最终结论分别列实际观察、纸面推导和未验证条件，后续任务指定下一责任和停止条件。本文提供静态教学与来源范围，不把历史预测200、摘要算术、篇幅或后续CI绿色写成真实QUIC运行成绩。


## 11. 来源范围与历史材料怎样使用

### 11.1 固定规范支持什么，不支持什么

2026-10-07 的静态核对使用以下11份固定 RFC；正文近旁链接指向具体论证入口，下面区分支持范围。保存完整原文并不等于通读全文。协议状态来自规范选段；build42、字节、时间、候选所有权和本地发布接口是明示题设，不能归为 RFC 的实测或平台保证。

| 固定规范 | 本文采用的选读范围 | 不据此声称 |
| --- | --- | --- |
| [RFC9000](https://www.rfc-editor.org/rfc/rfc9000) | §§2.2、3.5、4.1/4.4/4.5、5.1、8、9、10.3、12.3、13.2.5–13.2.6、13.3选段；尤其路径响应和新peer地址限制 | 已验证某网卡/NAT/LB/ECN/吞吐，或穷尽每种frame恢复实现 |
| [RFC9001](https://www.rfc-editor.org/rfc/rfc9001) | §§4.1.1–4.1.2、4.6/4.6.1–4.6.2、4.9、5.1/5.6、6主段、9.2 | 已实现证书库、全部参数恢复兼容性或反重放部署 |
| [RFC9002](https://www.rfc-editor.org/rfc/rfc9002) | §5、§6.1–6.2（含6.2.2.1/6.2.4）、§7与7.5–7.7、A.7选段 | 已裁决§5.3/A.7更新顺序差异，或已有控制器性能结果 |
| [RFC9113](https://www.rfc-editor.org/rfc/rfc9113) | §§5.2–5.3、6.1/6.8/6.9、8.1/8.7选段 | 当前浏览器push现状、HPACK逐位实现或代理运行通过 |
| [RFC9114](https://www.rfc-editor.org/rfc/rfc9114) | §§3.1–3.2、4.1及子节、5.2、6及control/request流、7.1 | 某CDN实际端口、push/CONNECT/WebTransport部署能力 |
| [RFC9204](https://www.rfc-editor.org/rfc/rfc9204) | §§2.1、2.1.2–2.1.3、2.2/2.2.1、4.2、5 | 完整QPACK字节编码器、任意动态表性能 |
| [RFC9221](https://www.rfc-editor.org/rfc/rfc9221) | §§3、5及子节 | HTTP Datagram映射、可靠文件协议 |
| [RFC9110](https://www.rfc-editor.org/rfc/rfc9110) | §§8.4、8.8.1/8.8.3.2–8.8.3.3、9.2、13.1.5、14.1.2/14.2/14.4、15.3.7/15.5.17 | 发布清单签名或文件系统事务由HTTP保证 |
| [RFC8470](https://www.rfc-editor.org/rfc/rfc8470) | §§2–4、5.2、6.2 | 所有GET可early-data，或425给全局回滚保证 |
| [RFC8446](https://www.rfc-editor.org/rfc/rfc8446) | §8、8.1–8.2选段，含跨zone与重试风险 | 某部署具备全局exactly-once，或全文TLS附录已审 |
| [RFC7838](https://www.rfc-editor.org/rfc/rfc7838) | §2、2.1、2.4选段 | 替代主机名自动改origin，或最新浏览器发现/fallback策略 |

### 11.2 固定实现仅用于核对旧实验接口

aioquic 1.2.0 固定 commit `9bc1e43d13be3f06339841aca7c8560825053371`。官方 [examples/README.rst](https://github.com/aiortc/aioquic/blob/9bc1e43d13be3f06339841aca7c8560825053371/examples/README.rst) 与完整 tree 支持：示例位于顶层 `examples/http3_server.py`，树中没有 `src/aioquic/examples/http3_server.py`；因此历史 `python -m aioquic.examples.http3_server` 不是该上游树提供的模块入口。示例还有 checkout/附加依赖前提，pip 包版本本身不证明整套实验可用。这里只核 pyproject/setup/版本常量、示例 imports 和 main 参数路径，没有安装或制造 ModuleNotFoundError。

固定 [http3_server.py](https://github.com/aiortc/aioquic/blob/9bc1e43d13be3f06339841aca7c8560825053371/examples/http3_server.py) 默认应用为 `demo:app`、拥塞参数 `reno`、端口4433，`--quic-log` 指目录。[logger.py](https://github.com/aiortc/aioquic/blob/9bc1e43d13be3f06339841aca7c8560825053371/src/aioquic/quic/logger.py) 的 QuicFileLogger 要求传入路径已是目录，写 ODCID.qlog，QLOG_VERSION 字符串为0.3。历史 `qlog/server.json` 若被当普通文件名则不满足接口；若本来就是目录，扩展名并不使它非法。这是接口核对，没有启动示例、生成qlog或认证一般schema。

curl 8.5.0 固定 commit `7161cb17c01dcff1dc5bf89a18437d9d729f1ecd`。[HTTP3构建文档](https://github.com/curl/curl/blob/7161cb17c01dcff1dc5bf89a18437d9d729f1ecd/docs/HTTP3.md) 的后端/依赖开头及 [http3选项](https://github.com/curl/curl/blob/7161cb17c01dcff1dc5bf89a18437d9d729f1ecd/docs/cmdline-opts/http3.d)、[http3-only选项](https://github.com/curl/curl/blob/7161cb17c01dcff1dc5bf89a18437d9d729f1ecd/docs/cmdline-opts/http3-only.d)、[version特性标志](https://github.com/curl/curl/blob/7161cb17c01dcff1dc5bf89a18437d9d729f1ecd/docs/cmdline-opts/version.d) 只支持构建能力与fallback选项含义，不能由“curl8.5”推某二进制支持HTTP3。本文没有运行curl版本查询或请求。

[insecure选项](https://github.com/curl/curl/blob/7161cb17c01dcff1dc5bf89a18437d9d729f1ecd/docs/cmdline-opts/insecure.d) 会跳过验证。历史自签/CN=localhost配SERVER_IP的例子未建立本例可信origin身份，预测 `http3=3 code=200` 也不是默认示例或任意路径已成功的事实。当前身份学习入口是第2、6节的正反例，不能把历史安装、证书、抓包、netem/netns或跳过校验命令变为默认步骤。

取源过程曾有三次 GitHub 裸API 403 rate-limit失败，涉及aioquic tag、curl tag、当前路径历史；原失败正文及响应记录均保留，后续只读连接器补到了同一资料。独立范围核对还有一条管道被提前关闭的 Broken pipe 可见片段，缺少完整分流原stdout/stderr，不能补造。它们描述来源读取边界，不是QUIC协议失败或本例网络实验。没有目标请求、探针、抓包、证书生成、安装/编译、设备查询、权限/网络/隔离设置及压力测试，普通字节/SHA/整数核对也不升级为L3。

### 11.3 原成文贡献与保留方式

2026-08-20 已有完整QUIC/H2/H3主篇、实验设计及150项模板；此轮是解释链和边界的重构，不是第一次写成。后来的元数据和导航演进仍保留。原机制与实践的用途已融入当前正文，150条是记录用途，不是150漏洞或150已采集字段。

以下三组是历史原件，日期、环境、命令、占位符、来源和预测结果一字未改，整块保留供理解当时方案。原件中的“按命令执行”和“验证入口”不适用于当前路线。使用前应以当前解释辨别其前提：应用延迟2秒不足以证明网络HOL；单向netem不是往返RTT；CID不保证任意换网；0-RTT不能只凭方法幂等；环境变量不应全量导出；重传应追信息与新PN；RTO不能混为QUIC PTO或业务恢复耗时。这些纠偏不能用修改原命令伪装成当时的记录。

#### 历史前言原件（2026-08-20）

> 验证与基准：按文中命令执行最小实验，记录结果与边界。

> 知识成熟度：L2；以 RFC 与目标实现版本复测。
> 知识基线：RFC 9000/9001/9114、QUIC v1 与 HTTP/2/3。
> 最后更新：2026-08-20。
> 官方参考：https://www.rfc-editor.org/rfc/rfc9000、https://www.rfc-editor.org/rfc/rfc9114。
> 验证入口：使用 qlog、Wireshark、netem 与端到端压测。


#### 历史实验原件（仅供史料阅读，当前不执行）

## 可执行实验与 Benchmark

目标：比较 TCP+HTTP/1.1、HTTP/2、HTTP/3 在高 RTT 和丢包环境下的尾延迟。

```bash
sudo tc qdisc replace dev eth0 root netem delay 80ms 10ms loss 1%
h2load -n10000 -c100 -m10 https://server/api
curl --http3 -w '%{time_starttransfer} %{time_total}\n' https://server/api
```

固定 CPU 频率、证书和响应大小，预热 60 秒后采样 300 秒，报告 p50/p95/p99、握手、重传和 CPU。

| RTT | 丢包 | 并发 | 关注指标 |
|---:|---:|---:|---|
| 1ms | 0% | 100 | 吞吐 |
| 80ms | 1% | 100 | TTFB |
| 150ms | 3% | 50 | 重连 |

构造一个响应延迟 2 秒的资源，对比 HTTP/2 与 HTTP/3 多路复用，验收其他流是否被队头阻塞。

客户端切换网卡并抓包，验证 QUIC Connection ID 迁移不增加握手次数；记录成功率、重复请求率和恢复时间。

工程案例：游戏补丁下载使用 HTTP/3、ETag、SHA-256 分片校验和断点续传；网络切换复用 Connection ID，失败采用指数退避。

排障顺序：UDP/443、防火墙、TLS1.3/ALPN、qlog 的 PTO/拥塞窗口、NAT 映射超时、服务端限流。

进一步练习：比较 BBR/CUBIC；解析 QPACK；实现 0-RTT 防重放令牌；将原始日志写入 evidence/labs。

## 可复现实验附录

固定环境：Ubuntu 24.04、Linux 6.8、Python 3.12、aioquic 1.2.0、curl 8.5、tcpdump 4.99。`SERVER_IP`、`CLIENT_IP`、`SERVER_PORT` 是占位符，必须替换。

```bash
openssl req -x509 -newkey rsa:2048 -nodes -days 2 -keyout server.key -out server.crt -subj '/CN=localhost'
python -m pip install aioquic==1.2.0
python -m aioquic.examples.http3_server --certificate server.crt --private-key server.key --host 0.0.0.0 --port 4433 --quic-log qlog/server.json
curl --http3-only --insecure https://SERVER_IP:4433/ -w 'http3=%{http_version} code=%{http_code} time=%{time_total}\n'
sudo tcpdump -i any -nn -s 0 -w http3.pcap udp port 4433
```

预期输出 `http3=3 code=200`。保存 qlog、pcap、stderr 和 SHA256；失败时先确认 UDP/4433、防火墙、ALPN `h3`、证书和 MTU。

弱网 netns：

```bash
sudo ip netns add qclient; sudo ip netns add qserver
sudo ip link add veth-c type veth peer name veth-s
sudo ip link set veth-c netns qclient; sudo ip link set veth-s netns qserver
sudo ip -n qclient addr add 10.10.0.2/24 dev veth-c; sudo ip -n qserver addr add 10.10.0.3/24 dev veth-s
sudo ip -n qclient link set veth-c up; sudo ip -n qserver link set veth-s up
sudo ip netns exec qserver tc qdisc add dev veth-s root netem delay 40ms loss 1% rate 20mbit
```

对 1/10/100 MiB 文件各执行 30 次，记录握手 RTT、goodput、p50/p95/p99、PTO、拥塞窗口、CPU。HTTP/2 与 HTTP/3 必须使用同一应用处理路径。

|场景|连接|网络|指标|验收|
|---|---:|---|---|---|
|基线|1|20ms/0%|goodput、p99|建立基线|
|弱网|1|80ms/1%|恢复时间、PTO|记录丢包恢复|
|并行|8|80ms/1%|总吞吐、公平性|说明共享拥塞控制|
|迁移|1|NAT 重绑定|PATH_CHALLENGE|不重复握手|

QUIC 每连接维护拥塞窗口，多连接共享瓶颈会竞争；不得把多流等同于无限吞吐。比较 CUBIC/BBR 时固定连接数、对象大小、MTU 与 CPU governor。每项至少 30 次，报告均值、标准差和 p95，异常样本保留而非静默删除。

HTTP/3 诊断检查 ALPN、控制流、QPACK 阻塞、H3_REQUEST_CANCELLED；HTTP/2 检查 SETTINGS、WINDOW_UPDATE、RST_STREAM。0-RTT 只允许幂等请求，写请求默认拒绝并记录重放令牌。

规范依据：RFC 9000（传输）、RFC 9001（TLS）、RFC 9114（HTTP/3）、RFC 9113（HTTP/2）。每次失败复盘保存完整命令、qlog、pcap、内核日志和修复前后差异。


#### 历史150条模板原件（当前口径见第10节）

## 实验记录模板

1. 记录主机型号、内核、网卡、MTU、时钟源。
2. 记录客户端、服务端、代理的准确版本。
3. 记录证书指纹和 ALPN 配置。
4. 记录并发连接、流数、对象大小。
5. 记录 CPU governor 与进程亲和性。
6. 记录 tc netem delay/loss/rate 参数。
7. 记录实验开始、结束时间和时区。
8. 保存原始 qlog，不覆盖历史文件。
9. 保存 pcap 与抓包过滤器。
10. 保存 curl/wrk2 完整 stdout/stderr。
11. 记录成功请求数与失败请求数。
12. 记录握手 RTT 分布。
13. 记录应用首字节时间。
14. 记录连接迁移次数。
15. 记录 PTO 触发次数。
16. 记录拥塞窗口最小值。
17. 记录重传包比例。
18. 记录 QPACK 阻塞时间。
19. 记录服务器 CPU 百分位。
20. 记录服务器 RSS 与 socket 数。
21. 记录客户端网络切换时刻。
22. 记录 NAT 映射超时设置。
23. 记录防火墙规则快照。
24. 记录所有环境变量。
25. 记录脚本 git commit。
26. 记录结果文件 SHA256。
27. 记录异常样本而非删除。
28. 说明剔除样本的客观规则。
29. 报告均值、中位数和标准差。
30. 报告 p95 与 p99 尾延迟。
31. 报告每秒请求数。
32. 报告有效载荷 goodput。
33. 报告协议头与加密开销。
34. 报告连接复用比例。
35. 报告 HTTP 状态码分布。
36. 报告 QUIC 错误码分布。
37. 报告 TCP fallback 次数。
38. 报告 DNS 解析耗时。
39. 报告证书验证耗时。
40. 报告服务端限流事件。
41. 复现实验至少三次。
42. 更换网卡后重复迁移实验。
43. 更换 MTU 后验证 PMTU。
44. 使用 IPv4 与 IPv6 对照。
45. 使用 HTTP/2 与 HTTP/3 对照。
46. 使用 CUBIC 与 BBR 对照。
47. 使用单流与多流对照。
48. 使用 0-RTT 与 1-RTT 对照。
49. 使用冷连接与热连接对照。
50. 使用 1 MiB 与 100 MiB 对照。
51. 校验内容 SHA-256 一致。
52. 校验断点续传字节范围。
53. 校验 ETag 与 If-Range。
54. 注入服务端重启。
55. 注入客户端网络切换。
56. 注入 5% 突发丢包。
57. 注入 200ms 延迟抖动。
58. 注入 MTU 黑洞。
59. 注入 DNS 暂时失败。
60. 注入证书过期。
61. 注入错误 ALPN。
62. 注入 UDP 防火墙阻断。
63. 注入连接 ID 冲突。
64. 注入流控窗口过小。
65. 记录恢复时间 RTO。
66. 记录重试次数上限。
67. 记录指数退避上限。
68. 记录用户可见错误。
69. 记录告警触发时间。
70. 记录回滚动作。
71. 复盘根因而非症状。
72. 给出永久修复与临时缓解。
73. 给出新增监控指标。
74. 给出回归测试命令。
75. 给出容量余量与风险。
76. 记录初始连接 ID 长度。
77. 记录新路径验证结果。
78. 记录 Retry token 过期率。
79. 记录放大限制拒绝数。
80. 记录 Stateless Reset 次数。
81. 记录 ACK delay 分布。
82. 记录最大 ACK delay。
83. 记录流控阻塞事件。
84. 记录 STOP_SENDING 原因。
85. 记录 RESET_STREAM 原因。
86. 记录 DATAGRAM 丢失率。
87. 记录 QPACK 动态表容量。
88. 记录 QPACK 插入阻塞流。
89. 记录 HTTP/3 控制流错误。
90. 记录 HTTP/2 GOAWAY 原因。
91. 记录代理连接池大小。
92. 记录代理重试策略。
93. 记录代理超时配置。
94. 记录应用取消请求比例。
95. 记录客户端超时分位数。
96. 记录服务端排队时间。
97. 记录磁盘读取时间。
98. 记录缓存命中比例。
99. 记录压缩算法与等级。
100. 记录响应体校验和。
101. 记录多路复用流优先级。
102. 记录优先级反转案例。
103. 记录大流对小流影响。
104. 记录连接公平性指标。
105. 记录带宽利用率。
106. 记录 pacing 包间隔。
107. 记录 socket 缓冲区。
108. 记录 UDP receive buffer 丢包。
109. 记录内核 softnet backlog。
110. 记录网卡 ring buffer。
111. 记录 RSS 队列分布。
112. 记录中断 CPU 分布。
113. 记录用户态加密 CPU。
114. 记录 TLS 密钥更新次数。
115. 记录会话恢复成功率。
116. 记录证书链长度。
117. 记录 OCSP/Stapling 状态。
118. 记录客户端时钟偏差。
119. 记录 qlog 解析工具版本。
120. 记录 Wireshark 配置版本。
121. 记录 netem 统计输出。
122. 记录 tc 删除与清理命令。
123. 记录 netns 删除与清理命令。
124. 记录失败实验的复现概率。
125. 记录随机种子。
126. 记录压测工具随机顺序。
127. 记录热身阶段丢弃规则。
128. 记录 GC 或 allocator 影响。
129. 记录日志采样比例。
130. 记录 traceparent 传播。
131. 记录请求关联 ID。
132. 记录服务端实例 ID。
133. 记录客户端地域。
134. 记录 NAT 类型。
135. 记录移动网络切换。
136. 记录 Wi-Fi 漫游。
137. 记录代理协议转换。
138. 记录 fallback 触发条件。
139. 记录 fallback 恢复条件。
140. 记录发布前门槛。
141. 记录灰度比例。
142. 记录灰度回滚阈值。
143. 记录压测合规窗口。
144. 记录数据脱敏规则。
145. 记录证书私钥清理。
146. 记录 pcap 访问权限。
147. 记录 qlog 保留周期。
148. 记录实验责任人。
149. 记录评审人。
150. 记录最终结论与后续任务。

---

## 关联知识与工程落地

- **前置依赖**：
  - [01-网络分层协议与工程实践](01-网络分层协议与工程实践.md)：TCP 与 UDP 传输层机制。
- **同分类与分布式延伸**：
  - [03-IPv6NAT服务发现与负载均衡](03-IPv6NAT服务发现与负载均衡.md)：连接迁移与负载均衡。
  - [14-安全与密码基础/02-TLS认证授权与反作弊](../../08-工程实践与质量/安全与防护/02-TLS认证授权与反作弊.md)：TLS 1.3 握手与 0-RTT 安全。
- **游戏网络协议落地**：
  - [游戏服务端/01-架构与网络/02-网络协议设计与实现](../../07-网络与游戏服务端/服务架构与消息通信/02-网络通信与协议设计.md)：自定义可靠 UDP 与弱网对抗。
- **分类与领域入口**：
  - [09-计算机网络基础 README](../../../00_Index/学习路线/网络与游戏服务端.md)
  - [计算机与工程基础 Domain MOC](../../../00_Index/学习路线/编程与计算机基础.md)
