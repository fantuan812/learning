---
type: Evidence
title: "进入游戏可运行证据（登录票据 / 会话状态机 / DS 租约 / JIP 追赶）"
description: "验证进入游戏链路的四组核心机制：密码学票据验签、进入状态机的幂等与回滚、DS 分配租约与栅栏令牌、断线重连的状态追赶与带宽/CPU 权衡。"
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
updated: 2026-10-01
---

# 进入游戏可运行证据

> 证据范围：本机可编译运行的 4 个程序，**83 条断言全部通过**（24 + 27 + 20 + 12），含 SHA-256/HMAC-SHA256 对 RFC 4231 与 FIPS 180-4 的选定标准向量校验、状态机回滚不变量、租约栅栏判定、以及 JIP 追赶的 P50/P95/P99 与带宽对比。按本仓库约定，`evidence/` 属维护基础设施，其 README 的 maturity 字段不参与知识成熟度门禁。

进入游戏是**唯一一条"每一步都还没进游戏、却已经把玩家状态写进线上系统"的链路**：票在网关签、名额在调度器占、角色档在 DB 读、状态在 DS 建。它会同时踩到密码学、幂等、分布式租约和状态同步四类问题。本目录把这四类各做一个最小可运行模型，并给出可判定的输出。

## 问题

1. 登录票据怎么签、怎么验？签名、过期、时钟偏移、跨服使用、重放，分别由哪一步拦下？
2. "验签"这一步到底该用什么比较方式？提前返回的字符串比较会泄漏什么？
3. 客户端进入过程中断线重发（换 requestId / 不换 requestId），会不会多占一个 DS 名额？
4. 某一步失败（无容量 / 超时 / 被拒）之后，链路是否立刻终止？占用的名额是否释放？
5. DS 名额怎么保证不超卖？玩家掉线后名额何时回收？"僵尸 owner"回来提交状态会怎样？
6. 断线重连/JIP 时，客户端状态怎么追上权威？乱序包该丢弃还是缓冲？
7. 增量追赶与全量快照，在 CPU 和带宽上分别差多少？

## 假设

- **票据是网关签、DS 验**：验签方不做时间回拨检测，只做「过期 + 时钟容差 + 一次性消费」三件事。
- **进入游戏由客户端驱动、服务端判定**：客户端可重发，因此入口必须幂等；requestId 是幂等键。
- **名额在"分配成功"那一刻即被占用**，而不是"进服成功后"。
- **租约 + 栅栏令牌**：任何状态提交都必须带当前令牌，令牌不匹配即拒绝，防止过期持有者写回。
- **JIP 走「快照 + 增量」**：增量日志按版本追加，客户端有重排缓冲；日志窗口不足时回退全量快照。

## 环境

| 项目 | 值 |
| --- | --- |
| 主机 | Windows（MINGW64_NT-10.0-26200，x86_64，16 逻辑核） |
| 工具链 | MSYS2 MinGW-w64 `g++` 16.1.0，`-std=c++17 -O2 -Wall` |
| 依赖 | 仅 C++17 标准库（SHA-256/HMAC 为自实现，不引入 OpenSSL） |
| 未使用 | 真实网关/Redis/etcd/Agones、UE NetDriver、线上压测 |

## 运行方式

```powershell
& (Join-Path $RepoRoot 'evidence/tests/entry-core/scripts/build_run.ps1')
```

```bash
bash evidence/tests/entry-core/scripts/run_all.sh
```

脚本重新编译并把**未经修改的原始输出**写入 `results/`。

## 输入

- `entry_ticket`：4 组密码学标准向量 + 20 条票据语义断言（TTL 60s、时钟容差 5s）。
- `entry_session`：4 台 DS × 容量 2 = 8 个名额；每步最多重试 3 次。
- `ds_allocator`：3 台 DS × 容量 2；租约 30s + 宽限期 10s。
- `jip_resync`：三种规模（2 000 / 20 000 / 200 000 实体），缺口 2 000 / 20 000 条增量。

## 指标与原始结果

**断言：`entry_ticket` pass=24 fail=0；`entry_session` pass=27 fail=0；`ds_allocator` pass=20 fail=0；`jip_resync` pass=12 fail=0**

密码学自校验（对标准向量，不是自证）：

```text
SHA-256("")                     e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
SHA-256("abc")                  ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
SHA-256(56B padding boundary)   248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1
HMAC-SHA256 RFC4231 TC1         b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7
HMAC-SHA256 RFC4231 TC2("Jefe") 5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843
HMAC-SHA256 RFC4231 TC3         773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe
HMAC-SHA256 RFC4231 TC6(>64B key) 60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54
```

进入状态机（成功路径的完整转移）：

```text
Idle -> Authenticated -> Allocated(ds-1,new) -> Traveling(ds-1) -> Loaded -> Ready
```

失败与回滚路径：

```text
Travel 连续超时 3 次  -> reason=Travel:timeout-after-3   load=0（名额已释放）
Auth 被拒             -> auth attempts=1                 load=0（不重试、不占名额）
Spawn 被拒            -> reason=Spawn:reject             load=0（侧效应后仍回滚）
第 9 个玩家           -> reason=no_capacity              load=8（不超卖）
```

DS 租约与栅栏：

```text
首次分配 token=1 -> 释放后再分配 token=2 -> 过期回收 -> 新玩家拿到 token=2
旧持有者以 token=1 提交 -> 被拒绝（A6）；旧持有者 token=1 续约 -> 被拒绝（A7）
玩家回归重新分配 -> token=3，且 token=1 永久失效（A10）
```

JIP 追赶（p50 / p95 / p99，微秒）：

```text
entities=2000   gap=2000    catch_up 6.510 / 7.990 / 11.005   snapshot 1.300 / 1.400 / 1.500
                            带宽节省 1.00x   CPU 额外开销 +5.210 us
entities=20000  gap=20000   catch_up 117.960 / 172.680 / 176.740  snapshot 25.200 / 33.500 / 35.600
                            带宽节省 1.00x   CPU 额外开销 +92.760 us
entities=200000 gap=20000   catch_up 2104.845 / 2718.270 / 2718.270  snapshot 1941.100 / 2328.500 / 2357.600
                            带宽节省 10.00x  CPU 额外开销 +163.745 us
```

原始输出：[entry_ticket.txt](results/entry_ticket.txt) ｜ [entry_session.txt](results/entry_session.txt) ｜ [ds_allocator.txt](results/ds_allocator.txt) ｜ [jip_resync.txt](results/jip_resync.txt)

## 结论

1. **功能检查与副作用分开**：本模型顺序为格式→MAC→时间窗→归属→消费；T10–T19覆盖列出的输入，不证明解析器/所有异常安全。时间由签发/验证服务器解释，客户端时钟不是信任源；分类错误码用于诊断，不是唯一根因证明。
2. **功能断言不证明常量时间**：T21–T24只断言相等/不等结果，没有采集首字节/末字节差异的耗时，也未审计优化后二进制。源码异或累加不能单独证明侧信道性质；生产采用有明确合同的成熟库并评估完整协议。
3. **入口必须幂等，且幂等键要覆盖"换 id 重试"**：同一 requestId 重放走幂等表直接返回原会话（E4/E5）；更隐蔽的是客户端重试时**生成了新 requestId**——若只按 requestId 去重就会多占一个名额，必须叠加"玩家维度"的归属复用（E6 实测复用了同一台 ds-1）。
4. **失败必须立刻终止链路，而不是继续走**：本轮实现里分配失败只置了状态、没有中断循环，于是链路带着**空 DS** 继续走完 Travel/Load/Spawn，产出"状态是 Ready 但从未连上服务器"的会话。测试把它抓了出来（原 E18/E19 失败），修复后 `no_capacity` 的转移日志止于 `Failed(no_capacity)`。这是这类状态机最典型的生产事故形态。
5. **回滚要幂等、且不能误伤已就绪的会话**：失败时释放名额并打 `rolledBack` 标记，重复回滚不二次释放（E12）；而已经 Ready 的会话在客户端"取消"时必须**不做任何清理**——玩家已在局内，清掉名额会造成对局中突然掉线（E21）。
6. **租约 + 栅栏令牌是"僵尸 owner"的唯一解**：掉线过宽限期后名额被回收并交给别人，旧持有者带着旧令牌回来提交/续约**必须被拒**（A6/A7）；玩家重新进入拿到新令牌后，旧令牌**永久失效**（A10）。注意令牌是**按 DS 分配**的，ds-1 的令牌不授权 ds-2（A20）。
7. **容量只能靠单一写者守**：3 台 × 2 名额，第 7 个玩家被 `no_capacity` 拒绝而不是超卖（A14），且任意混合操作序列后"服务器负载 == 活跃租约数"始终成立（A15/A16）。
8. **JIP 的乱序包必须缓冲重排，不能直接丢**：直接丢弃会永久丢失那次写。本实现用重排缓冲，缺口未补齐前不推进版本（J5），缺口补齐后连续落地并收敛到与权威完全一致的指纹（J1/J2/J9–J11）。
9. **增量追赶在 CPU 上永远不便宜，它买的是带宽**：追赶 = 快照拷贝 + 重放，所以 p50 必然 ≥ 全量快照。实测缺口与实体数同量级时带宽零收益、CPU 白付（+5.2µs / +92.8µs）；只有缺口远小于世界规模时才有意义——200 000 实体、20 000 条增量时带宽省 **10 倍**，CPU 多付 **163.7µs（约 +8%）**。**给单个进场玩家发最新全量快照通常是最优解**；增量是为"已经在场、只是落后"的客户端省带宽的。
10. **日志窗口不足必须能回退**：快照版本早于日志左边界时判 `kNeedFullSnapshot` 走全量路径（J6/J7），这条降级路径不做，JIP 会出现静默的状态不一致。

## 局限

- **密码学部分是自实现、仅对标准向量校验**：覆盖 SHA-256 与 HMAC-SHA256 的正确性，但**未做**侧信道审计、密钥管理、HSM/KMS 集成、票据吊销列表；生产必须用成熟库（OpenSSL/libsodium/平台 KMS）。
- **状态机是单线程模型**：未覆盖真实并发下的竞态（两个线程同时为同一玩家建会话），只验证了逻辑上的幂等键与单写者语义。
- **分配器未接真实平台**：没有 Agones/GameLift 的 Pod 生命周期、没有真实心跳网络、没有 Redis/etcd 作为租约存储；未验证分布式时钟不确定性与脑裂。
- **JIP 为内存模型**：未接真实序列化/压缩/加密与 MTU 分片，字节数按 `Entity 32B / Delta 32B` 估算而非实测线上包体。
- **未覆盖**：平台登录（OAuth/渠道 SDK）、角色档案加载与 DB 事务、反外挂校验、跨区路由、排队系统、进入过程中的资源预下载。
- 单机单线程、`-O2`、16 核 x86_64 下的 p50；**结论应看比例与量级，而非绝对值**。

## 关联知识文档

- [系统实战/01-角色进入游戏完整链路](../../../系统实战/01-角色进入游戏完整链路.md)（本证据的主要使用者）
- [游戏服务端/04-平台与可靠性/01-身份认证与权限模型](../../../游戏服务端/04-平台与可靠性/01-身份认证与权限模型.md)
- [游戏服务端/05-UE Dedicated Server平台化/03-DS会话注册与重连实现](../../../游戏服务端/05-UE%20Dedicated%20Server平台化/03-DS会话注册与重连实现.md)
- [游戏服务端/05-UE Dedicated Server平台化/01-UE Dedicated Server实例生命周期与平台化](<../../../游戏服务端/05-UE Dedicated Server平台化/01-UE Dedicated Server实例生命周期与平台化.md>)
- [游戏服务端/06-世界模拟与运行时/07-EntityOwnership与Authority](../../../游戏服务端/06-世界模拟与运行时/07-EntityOwnership与Authority.md)（租约与栅栏令牌的上层模型）
- [游戏服务端/06-世界模拟与运行时/13-世界Snapshot与故障恢复](../../../游戏服务端/06-世界模拟与运行时/13-世界Snapshot与故障恢复.md)
- [evidence/tests/gameplay-core](../../tests/gameplay-core/README.md)、[evidence/tests/damage-core](../../tests/damage-core/README.md)（同样采用"最小可运行 + 原始输出归档"的证据形态）

## 2026-10-01 票据证据复核边界

- 原`src/entry_ticket.cpp`未修改，在Linux/GCC14.2以`-std=c++17 -O2 -Wall -Wextra -Werror -pedantic`重新编译，24条既有断言通过；旧Windows原始输出保留，未冒充本轮结果重写。
- 源码SHA256：`1115b2884fcec02378ce1ddc52f7993dcc25a60602d12149d9a2c556e504ec16`。本轮只复跑票据程序，其他三程序沿用历史83条总计中的原记录，未声称全部重跑。
- **未测timing**。即便增加微基准并得到相近耗时，也不能据此保证所有编译器/硬件/输入与完整服务无侧信道。
- **未证明持有者身份**。MAC保护player/server等声明的完整性，不防有效bearer票据整体被盗用；**未证明分布式一次性**，内存nonce集合没有原子跨进程/持久化合同。
- 选定向量符合[RFC4231](https://www.rfc-editor.org/rfc/rfc4231)只支持相应输入输出；[OpenSSL CRYPTO_memcmp](https://docs.openssl.org/3.0/man3/CRYPTO_memcmp/)给出成熟比较接口合同，[RFC6750](https://www.rfc-editor.org/rfc/rfc6750#section-1.2)解释bearer语义。它们不是本模型已集成这些库/协议的证明。
- 主责解释和后续失败矩阵见[进入游戏链路的票据章节](../../../系统实战/01-角色进入游戏完整链路.md#步骤-6网关验签与一次性消费)。生产随机数/密钥管理、解析限制、传输安全和消费恢复仍待独立验收。
