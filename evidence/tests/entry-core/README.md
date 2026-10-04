---
type: Evidence
title: "进入游戏可运行证据（登录票据 / 会话状态机 / DS 租约 / JIP 追赶）"
description: "四个独立串行教学模型：票据、Session、DS租约及有界JIP；严格runner、完整状态oracle与语义负向控制。"
tags:
  - evidence
  - entry
  - login
  - session
  - dedicated-server
  - gameplay
status: stable
verified: []
maturity: L0
updated: 2026-10-04
---

# 进入游戏可运行证据

> **当前范围：四个独立C++17教学模型，不是票据→网关→DS端到端系统。** 2026-10-04票据解析、Session主体/意图隔离和DS座位/写权证据保留；本次增加固定复制scope的JIP快照/增量合同。Auth与身份上下文均合成，不读取真实secret、不调用真实认证服务。
> **JIP零增量漏装、缺口误报成功、身份/实体代次与无界缓存等问题已在有限串行模型中修复。** 默认runner纳入JIP；新JIP strict/UBSan raw与旧安全模型raw分开归档。2026-09-11的83条（24+27+20+12）仅是历史选定用例，原4份raw及旧PASS行原样保留，不能据此证明全部不变量。
> 本README的L0是证据基础设施分类，不是对局部测试深度的分级；对应主文继续区分设计核对与局部运行，不用总PASS提升整篇成熟度。

## 问题

进入过程可能已经占座却还未Ready，因此“重试”“归属仍有效”“这个失败请求有权撤销什么”必须分开判断。本目录回答：

1. 不可信票据如何严格限长/解析，错误如何不消费nonce、不发布半个输出？
2. 同字符串requestId能否跨主体取到旧成功结果？同键变更match/character/region怎么办？
3. 新请求借用旧Ready资源失败，或旧取消迟到，能否误释放新资源？
4. 保留座位与写权到期如何分离？满DS中包含本人的座位时如何重连？
5. 快照已安装、追到固定目标、ACK获准和实时输入可用是不是不同状态？
6. 编译失败、超时、假RESULT或写盘失败能否被runner误记成通过？

## 假设

- 四程序各自有main、各自状态；没有跨程序票据传递或真实会话服务。JIP身份比较只绑定已由可信应用会话提供的上下文，不实现认证
- 串行单进程调用；AuthFn是可信认证层的测试替身，客户端自报player不是可信主体
- `entry_session`检验请求/资源代次补偿，没有租约时钟；`ds_allocator`独立检验时间/容量，不实现认证服务。不能把各自局部保证自动拼成集成保证
- 所有now参数由可信服务端时钟提供；DS模型拒绝观察到的倒退，票据模型仅计算合法窗口
- 下文“失败状态不变”限调用前置条件成立且正常返回错误码的路径。`bad_alloc`、回调抛异常等没有强回滚保证；数字转换无异常不等于整个API noexcept。进程崩溃、持久化事务和线程竞争也未注入
- struct的public状态用于测试观测、故障夹具和受信配置；不允许调用方任意改写容器/epoch/cache并期待合同继续成立。正常调用前复制所需句柄/ID，out必须是独立对象，不能别名allocator/codec内部容器元素；未声称别名、外部篡改或异步重入安全

## 环境

| 记录 | 真实范围 |
| --- | --- |
| 2026-10-04本批 | Linux x86_64，GCC 14.2.0-19，C++17；完整版本/OS/源码SHA在各raw |
| 严格选项 | `-std=c++17 -O2 -Wall -Wextra -Werror -pedantic`，不使用`-fpermissive` |
| UBSan | 同严格选项加`-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer` |
| runner依赖 | Python3标准库、Bash；可选pwsh测试PowerShell wrapper；不安装依赖 |
| PowerShell边界 | Linux pwsh 7.6.6跑wrapper成功/失败fixture；**不是Windows/MinGW C++运行** |
| 历史2026-09-11 | 原raw标为Windows/MSYS2 MinGW g++16.1.0；保留为当时记录，不冒充本批宿主 |
| 未使用 | UE/PIE、真实网关/DS/DB、Redis/etcd/Agones、线上压测、OpenSSL/KMS集成 |

## 运行方式

输出目录必须**不存在**，连已存在的空目录也拒绝；不再默认写旧`results/*.txt`。从仓库根运行：

```bash
RUN_ROOT="$(mktemp -d)"
PYTHONDONTWRITEBYTECODE=1 bash evidence/tests/entry-core/scripts/run_all.sh \
  --output-dir "$RUN_ROOT/strict" --cxx g++-14
PYTHONDONTWRITEBYTECODE=1 bash evidence/tests/entry-core/scripts/run_all.sh \
  --output-dir "$RUN_ROOT/ubsan" --cxx g++-14 --ubsan --with-self-test
```

只跑runner合同与六项真实语义mutation（3项安全模型、3项JIP；另建目录，不嵌套自身self-test）：

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -B evidence/tests/entry-core/scripts/test_runner_contract.py \
  --self-test --output-dir "$RUN_ROOT/runner" --cxx g++-14 --ubsan --pwsh pwsh
```

可省略`--pwsh`，但结果会明确标记PowerShell未运行；Windows须自行准备其实际编译器和Python，本批没有Windows结果。PowerShell入口：

```powershell
./evidence/tests/entry-core/scripts/build_run.ps1 -OutputDir <不存在的输出目录> -Cxx <实际编译器> -Ubsan
```

`--timeout`与`--compile-timeout`分别限制运行和编译（默认15/60秒）；可用`--targets jip_resync`选择单个已定义目标；默认四模型。`--source-dir`只用于显式替代源码/故障夹具。runner：

- 每目标复制固定源字节到全新临时build，记录原SHA；编译失败/未产生binary绝不执行旧binary
- 只看本run stdout的一条规范RESULT，要求exit=0、pass>0、fail=0、PASS逐项数一致且无FAIL；不扫描历史raw
- 缺编译器/Python、编译失败、程序非0、缺失/坏/重复/伪造RESULT、编译/运行超时、输出创建/写入失败均非0
- raw分别记录编译与运行stdout/stderr、退出码、命令、UTC、OS、编译器版本；写入用exclusive创建，错误不打印虚假的“完成”
- 有意破坏的源码在临时目录真实编译；它们必须产生非0和FAIL，才说明oracle确实能发现目标语义回归

## 输入

### 票据：严格grammar与失败原子性

[entry_ticket.cpp](src/entry_ticket.cpp)保留SHA-256空串/abc/56字节向量、1000字节分块一致性、RFC4231 TC1/2/3/6。比较例仅检查相等/首差异/末差异/长度差异，未证明constant-time。

wire固定`v1|player|server|nonce|iat|exp|sig`字段顺序：ID为1–64字节ASCII字母/数字/下划线/连字符，nonce为16–64小写hex，MAC为64小写hex，总长≤1024；iat/exp是0..INT64_MAX规范十进制。`from_chars`要求全量消费；签发也执行相同规则。默认TTL60/skew5，政策上限TTL300/skew30，时间窗为`[iat-skew, exp+skew)`，以差值判断避免溢出。

反例输入包括iat和exp各自空/非数/超范围/尾缀/符号/空白/前导零、重复/乱序/缺失/未知字段、嵌入分隔符/NUL、过长、坏MAC编码及有效MAC下颠倒时间区间。每个测试中的Verify正常错误返回都比较nonce表和独立外部Ticket完整字段不变。有效MAC非法格式来自合成签名者，不表示外人能伪造MAC。

`consume=false`仍拒绝已消费nonce，只是不新增消费记录，不能用作原结果恢复查询。消费键是codec内`(player,server,nonce)`，最多1024项，满表拒绝且无清理；这是有界教学状态，不是部署建议。生产须设计CSPRNG、密钥域/轮换、原子持久化消费、保留期和消费成功但响应丢失后的查询。

### Session：主体、完整意图与资源代次

[entry_session.cpp](src/entry_session.cpp)先运行合成Auth白名单并验证声明player，再访问`(tenant,subject,operation,requestId)`缓存。完整意图`{player,match,character,region}`逐字段比较；同键异意图冲突，跨主体不返回别人的对象。

Session的Auth每次重验当前主体/权限，不能机械接成`Verify(..., consume=true)`：首次消费成功后，同票再验会在缓存前失败；改为consume=false也仍会拒绝已消费nonce。真实集成须提供由独立当前身份凭据认证的原requestId/完整意图查询，首次nonce原子消费与查询分开；三模型没有实现这个跨模块接口。

分配器结果明确区分new和borrowed。只有本请求新取得且完整`{principal,ds,epoch}`仍匹配的资源可被Rollback/Cancel释放。旧Ready存在时对Auth/Allocate回调/Travel/Load/Spawn分别注入Reject、Timeout、Cancel；同时检查旧handle/epoch/DS/load不变和后续回调没有执行。没有旧Ready时同矩阵检查本次新座位被清理、重复补偿无二次减账。

另测早Auth失败的迟到Cancel、旧代次补偿、新requestId合法借用、Ready后Cancel、无容量立即return，以及资源失效后重放旧Ready返回stale。重试配置仅接受1–3；0/4/INT_MAX均在调用回调前拒绝，没有运行巨量循环。没有真实UE Spawn/Destroy：这只是结果回调与座位账，不是实体回收测试。

### DS：座位账和写权分别判断

[ds_allocator.cpp](src/ds_allocator.cpp)使用`{player,ds,epoch}`和allocator全局高水位。Live为`now<expiresAt`；GraceHeld为`expiresAt≤now<holdUntil`。Live有写权，GraceHeld只保留座位；重连经外部重新鉴权后原位换代，满DS也不再加load。请求Release与Commit/Heartbeat采用同样Live限制，过期owner不能清掉保留位。

Expired待清扫的记录可以还占物理账，但不能写。对正常返回码路径，Reap在`now≥holdUntil`清理，Allocate计算候选账后仅在新分配提交分支结清过期账；容器/字符串分配异常没有强回滚保证。每个DS满足`load==座位记录数`及容量上限，玩家唯一归属；不把“活跃”含混地同时用于占座和写权。

固定序列/边界覆盖1029/1030/1031/1039/1040/1041/2000含/不含Reap，1DS×1满座本人重连、跨DS旧句柄、伪造player/ds/epoch、迟到释放、时钟回拨、TTL/grace溢出和UINT64_MAX耗尽。通过输入/配置前检、进入Observe后，即使拒绝旧句柄也推进可信时间水位，以防到期判断后回拨复活；空player/null输出/非法配置的前检失败不采样时间。在上述前置条件成立且正常返回错误码时，除此之外保持资源、load、epoch和独立输出不变；不对bad_alloc等异常或内部容器别名输出作强保证。该水位是时间观察状态，不是新的写授权。

### JIP：身份、连续前缀和有界交接

[jip_resync.cpp](src/jip_resync.cpp)不再复用含另一份rev的World，也不靠量化hash宣称byte-identical。`State`只有一个已提交seq；完整比较installed、world/incarnation/scope/schema/session、seq、实体数量和每条记录的id/generation/x/y/hp/alive，死亡记录也保留。double按数值比较，+0与-0相等；它不是序列化字节比较。期望状态是人工固定向量，不通过同一Apply推导。

**身份与切点。** `Context = {Stream{world, incarnation, scope, schema}, session}`来自可信应用，`Identity`再加transfer。schema只接受1；非零整数只是合成ID，不是密码学能力凭证。`Begin(Plan{identity,R,T})`固定目标T，要求R≤T、T−R≤32。连续的是此复制scope的seq：筛选掉其他AOI事件后应有自己的连续序列，不能拿全世界rev的自然空号当丢包。改变scope/实例生命周期或重启编号必须由应用显式`Rebind`并重建基线，包不能自行切流。Rebind拒绝与当前完全相同的Context，不允许靠清空本地历史重放旧transfer；不同Context还必须在应用生命周期内不复用，这个跨进程/重启唯一性前提由外部保证，本模型不保存无限历史Context。

`Window.first`只在非空日志中表示包含式保留左界，head表示已知上界。空日志的first不参与判断（允许first=head+1；head为UINT64_MAX时不计算这个值）；只有R==T且head≥T才可零增量安装，R<T空窗仍NeedSnapshot。非空日志逐条检查identity及[first,head]范围。**批CatchUp对seq≤R的已覆盖记录在这两项检查后直接忽略payload，不调用Deliver，不保证检查其grammar、field或NaN/Inf等数值。** 这些旧payload不再参与快照状态重建；不要把整批CaughtUp解释为所有历史载荷均通过语法验证。R后的记录（含T后记录）进入Deliver的字段/有限数检查并有界保留，槽/代次/生命周期在实际应用前验证。

**安装与应用。** 固定0..N−1实体槽（N≤16），不支持增量扩容；新增槽需新基线。快照校验集合、全部字段与身份，R==T也完整安装。`Hp/Position`只作用于存活且generation匹配的实体；Despawn保留字段并置dead；Spawn要求dead且generation恰+1，位置取载荷、HP重置100。Install校验快照字段；Deliver对入队记录校验field、数值和未使用载荷，实际应用前还校验槽/代次/生命周期。相应数据域为位置绝对值≤1,000,000、HP在[0,1,000,000]且全部有限，未知field、NaN/Inf及非规范未使用载荷在Deliver拒绝，非法槽/代次在相应检查点拒绝。这些保证也适用于批helper送入Deliver的R后记录，不适用于它跳过的seq≤R旧payload。seq/generation/deadline耗尽拒绝，不回绕。

| 状态/输入 | 已实现行为 | 输入资格 |
| --- | --- | --- |
| ReceivingSnapshot | R之后增量先入有界pending；世界尚未替换 | 关闭 |
| Install成功但缺next | 安装基线或保留已提交合法前缀，WaitingForGap | 关闭 |
| 连续到固定T | CaughtUp，T之后增量仍缓冲，不追逐移动终点 | 关闭 |
| 当前identity+T的ACK | 接入实时流，再排出已收到的T+1及以后；仍有洞返回Buffered | 仅无洞时Live/Ready |
| 实时流出现缺口 | WaitingForGap且输入关闭；补齐恢复Live，超时NeedSnapshot | 关闭 |
| 错world/incarnation/scope/schema/session/transfer | WrongIdentity，不改变世界、游标、合法pending和阶段 | 原阶段不变 |
| Install/Deliver数据或生命周期错误、载荷冲突、缓存/span超限或期限到达 | 错误码并锁到NeedSnapshot；用新transfer恢复 | 关闭 |

`Install`/`Deliver`每次先复制候选世界、队列和最近记录，校验这一次可排出的完整连续段，再发布。正常错误返回不改调用前世界/seq/合法pending；可改变显式错误阶段。**CatchUp批helper采用合法前缀策略**：较早成功的Install/Deliver已提交，随后失败不倒回批次开始，但绝不Ready；它不是全批事务。分配异常、外部直接篡改public状态、别名调用/异步重入不在此保证内，未注入bad_alloc。

**重发不是任意覆盖。** 同transfer/计划Begin、同内容快照Install和相同pending增量都是no-op，不清缓存、不延长期限。迟到旧快照不能回退已提交seq；同baseline身份异内容冲突。pending同seq异payload拒绝且保留原件；已应用的最近8条同样检查。更早或快照覆盖的seq返回Stale且不应用，因不无限保存旧payload，无法检测所有历史冲突；依赖受信来源为每个seq只发布一个内容。

`Begin`是可信控制面请求；参数、旧transfer或同transfer异计划拒绝保留原活动transfer，不把既有Live任意降级（可能观察可信时间）。真正恢复NeedSnapshot必须用更大的新transfer；同计划重复Begin不会清除错误或重新开启等待。

**明确上限。** pending最多8条；未来跨度≤32；已应用重发记忆最多8条；批日志最多64条；每轮目标跨度≤32。满pending时缺失的next仍可直接排出，不要求第9个常驻槽。传输从Begin到ACK总期限为10个合成时钟单位，重复包不延期；实时流新缺口再开10单位期限，恰到期失败。now由可信应用传入；通过当前identity/阶段与时钟前检的调用（包括重复、Stale及随后数据错误）推进观察水位，不能倒退；不延长期限。错误身份/已终止阶段不采样。Begin通过身份、范围、期限前检后也观察时间，后续重复/旧transfer拒绝可推进水位。需要调用方按时Tick，模型没有后台定时器。超限/过期不偷偷丢旧包继续，保留有界诊断状态直到新transfer清理；本地应用负责限流和何时重新发起。

**可手算轨迹。** R10时slot0为gen1/hp100/位置(0,0)，slot1为gen4/hp80/(2,−3)，slot2为dead/gen9/hp0/(7,8)。11改slot0 HP73.5；12移动slot1到(12,−4.5)；13删除slot0；14以gen2在(3,4)重生；15改HP42；16删除slot1。T16预期三个槽依次为`{gen2,(3,4),42,alive}`、`{gen4,(12,−4.5),80,dead}`、`{gen9,(7,8),0,dead}`，逆序/固定seed乱序也必须一致。

| 修复前反例 | 现在的oracle |
| --- | --- |
| 最新快照+零增量返回Ok却保留空世界；窗口回退换了另一个client才验 | 同一个client失败→新transfer→最新快照零增量，逐字段收敛 |
| 中洞/缺尾返回Ok；缺尾pending甚至为空 | 完成必须seq==固定T，缺口NeedSnapshot且ACK不可打开输入 |
| 非法id/field被跳过却推进rev | 数据/生命周期先验，整次连续排出失败不消费游标 |
| 同seq新payload覆盖pending；旧快照清缓存/回退 | 完全重复幂等，冲突拒绝，迟到快照不回退 |
| 没有stream/session/transfer/实体generation | 各身份错配均拒绝；显式应用Rebind才换上下文 |
| hash忽略dead、量化0.001，Inf转整数可UB | 删除hash；独立全状态向量及字段敏感性测试，无浮点→整数状态转换 |

## 指标与原始结果

所有模型使用显式断言，不从断言总数推导安全证明。先前票据/Session/DS的权威运行元数据和源码hash见[run-manifest.json](results/2026-10-04-entry-isolation/run-manifest.json)：

- [票据严格GCC14 + UBSan raw](results/2026-10-04-entry-isolation/entry_ticket-linux.txt)
- [Session严格GCC14 + UBSan raw](results/2026-10-04-entry-isolation/entry_session-linux.txt)
- [DS严格GCC14 + UBSan raw](results/2026-10-04-entry-isolation/ds_allocator-linux.txt)
- [Bash/Linux pwsh故障fixture及语义mutation raw](results/2026-10-04-entry-isolation/runner-contract-linux.txt)

本次JIP单目标`--targets jip_resync --with-self-test --pwsh pwsh`归档为：

- [strict JIP raw](results/2026-10-04-jip-contract/strict/jip_resync-linux.txt)、[strict runner raw](results/2026-10-04-jip-contract/strict/runner-contract-linux.txt)、[strict manifest](results/2026-10-04-jip-contract/strict/run-manifest.json)
- [UBSan JIP raw](results/2026-10-04-jip-contract/ubsan/jip_resync-linux.txt)、[UBSan runner raw](results/2026-10-04-jip-contract/ubsan/runner-contract-linux.txt)、[UBSan manifest](results/2026-10-04-jip-contract/ubsan/run-manifest.json)

正例涵盖零增量、空/非空快照、精确窗口/头中尾缺失、同客户端回退、12组逆序/固定seed置换、安装期间收包、旧/重复快照、完整身份及错误ACK、T后增量、槽生命周期、坏数据、count/span/期限/耗尽边界和oracle字段敏感性。runner自测继续保留全部既有Bash/Linux pwsh故障fixture。

六项真实编译负向控制：移除票据合法区间检查、Session acquired-new判断、grace重连多加load；JIP跳过零增量安装、跳过目标连续性终检、跳过incarnation比较。它们必须编译成功，运行出现FAIL且非0。完整破坏后输出保留在runner raw，不能删掉后只展示绿行。默认四模型回归另外在仓外输出目录运行，不能把这里正例scope只有JIP的manifest说成四模型正例归档。

历史原件保持原字节：[entry_ticket.txt](results/entry_ticket.txt)、[entry_session.txt](results/entry_session.txt)、[ds_allocator.txt](results/ds_allocator.txt)、[jip_resync.txt](results/jip_resync.txt)。旧分组应读为：T1–T8算法/分块功能，T9–T20票据选定行为，T21–T24比较布尔功能。旧A20只判断数字为1，并未证明跨DS隔离；旧E15/E16只清座位，不是实体销毁。

## 结论

1. 严格语法是签名协议的一部分：MAC正确不能替代无歧义解析，签发端也要拒绝非法字段
2. 缓存结果不能绕过当前鉴权；请求键、完整意图和当前资源归属是不同检查
3. 补偿必须证明“本请求新取得且仍拥有这一代资源”；只用Ready判断或done位不够
4. grace保留的是一个座位，不是旧owner写权；无Reap也不能Heartbeat复活，裸数字epoch也不能跨资源授权
5. 真实资源写入需要原子归属条件；本例bool Commit不等于接入生产持久化fencing
6. JIP完成需要正确基线、连续到固定T与身份匹配的ACK；“pending为空”不够，缺尾仍可能未完成
7. 严格runner和负向控制防止证据假绿；有限样本仍不是穷举/形式证明

## 局限

- 自实现SHA/HMAC只作教学；无timing、优化后二进制审计、密钥管理、CSPRNG、TLS、真实bearer持有者绑定
- 无持久化/重启incarnation、分布式消费、并发请求、真实业务事务和异常内存分配故障注入
- Session的AuthFn/步骤回调按返回Outcome合同工作；未覆盖回调抛异常、异步重入或真实实体资源清理
- 没有性能/容量结论；2个玩家/DS只是边界fixture
- JIP只实现固定有限scope的串行内存接收器，没有真实编码/认证/分包/压缩/日志持久化/AOI变更事务；ACK比较只是应用门禁，不是服务端证明客户端诚实的密码学证据
- 旧JIP基准全量路径停在R、追赶路径到T，且析构与采样边界不对称；旧32字节乘数量不是编码后网络字节。fresh JIP必须计S(R)+D(R,T)，只有确有可用基线的warm resume才可讨论只发D。撤回“CPU必然更贵”和泛化10×节省；默认程序移除计时，本批没有新CPU或网络性能结论
- Windows C++、UE/PIE、真实网关→DS、DB、调度平台、弱网、压力、脑裂未运行

## 关联知识文档

- [系统实战/01-角色进入游戏完整链路](../../../知识/07-网络与游戏服务端/会话身份与在线服务/01-角色进入游戏完整链路.md)（本证据的主要使用者）
- [游戏服务端/04-平台与可靠性/01-身份认证与权限模型](../../../知识/07-网络与游戏服务端/会话身份与在线服务/01-身份认证与权限模型.md)
- [游戏服务端/05-UE Dedicated Server平台化/03-DS会话注册与重连实现](../../../知识/07-网络与游戏服务端/会话身份与在线服务/03-DS会话注册与重连实现.md)
- [游戏服务端/05-UE Dedicated Server平台化/01-UE Dedicated Server实例生命周期与平台化](<../../../知识/07-网络与游戏服务端/专用服务器实例与容量/01-UE%20Dedicated%20Server实例生命周期与平台化.md>)
- [游戏服务端/06-世界模拟与运行时/07-EntityOwnership与Authority](../../../知识/07-网络与游戏服务端/世界权威与故障恢复/07-EntityOwnership与Authority.md)（租约与栅栏令牌的上层模型）
- [游戏服务端/06-世界模拟与运行时/13-世界Snapshot与故障恢复](../../../知识/07-网络与游戏服务端/世界权威与故障恢复/13-世界Snapshot与故障恢复.md)
- [evidence/tests/gameplay-core](../../tests/gameplay-core/README.md)、[evidence/tests/damage-core](../../tests/damage-core/README.md)（同样采用"最小可运行 + 原始输出归档"的证据形态）

## 2026-10-01 票据证据复核边界

- 原`src/entry_ticket.cpp`未修改，在Linux/GCC14.2以`-std=c++17 -O2 -Wall -Wextra -Werror -pedantic`重新编译，24条既有断言通过；旧Windows原始输出保留，未冒充本轮结果重写。
- 源码SHA256：`1115b2884fcec02378ce1ddc52f7993dcc25a60602d12149d9a2c556e504ec16`。本轮只复跑票据程序，其他三程序沿用历史83条总计中的原记录，未声称全部重跑。
- **未测timing**。即便增加微基准并得到相近耗时，也不能据此保证所有编译器/硬件/输入与完整服务无侧信道。
- **未证明持有者身份**。MAC保护player/server等声明的完整性，不防有效bearer票据整体被盗用；**未证明分布式一次性**，内存nonce集合没有原子跨进程/持久化合同。
- 选定向量符合[RFC4231](https://www.rfc-editor.org/rfc/rfc4231)只支持相应输入输出；[OpenSSL CRYPTO_memcmp](https://docs.openssl.org/3.0/man3/CRYPTO_memcmp/)给出成熟比较接口合同，[RFC6750](https://www.rfc-editor.org/rfc/rfc6750#section-1.2)解释bearer语义。它们不是本模型已集成这些库/协议的证明。
- 主责解释和后续失败矩阵见[进入游戏链路的票据章节](../../../知识/07-网络与游戏服务端/会话身份与在线服务/01-角色进入游戏完整链路.md#步骤-6网关验签与一次性消费)。生产随机数/密钥管理、解析限制、传输安全和消费恢复仍待独立验收。
