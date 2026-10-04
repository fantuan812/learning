---
type: Evidence
title: "Gameplay 核心机制可运行证据（背包事务 / Buff 冲突 / 技能管线 / 属性聚合）"
description: "用可编译运行的 C++ 最小实现验证 Gameplay 服务端四类核心机制的语义与性能基线。"
tags:
  - evidence
  - gameplay
  - inventory
  - buff
  - ability-system
status: stable
verified: []
maturity: L0
updated: 2026-10-01
sources:
  - id: gcc-finite-math
    title: "GCC optimization options"
    resource: "https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html"
  - id: cpp-optional
    title: "C++ working draft: optional objects"
    resource: "https://eel.is/c++draft/optional"
---

# Gameplay 核心机制可运行证据

> 历史证据范围（2026-09-11）：当时本机可编译运行的 4 个测试/基准程序 + 未修改的原始输出已归档（29 条断言全部通过）；不涉及多机与线上环境，也不涉及 UE 运行时。按本仓库约定，`evidence/` 属维护基础设施，其 README 的 maturity 字段不参与知识成熟度门禁；本目录的证据强度在正文中按"已验证事实 / 未验证边界"分别陈述。

本目录原有四个互不依赖的最小 C++ 程序，把 Gameplay 工程师日常最依赖但最容易"只写结论、不留证据"的四类机制固化成可运行断言与可复现基准：**背包事务原子性/幂等、Buff 冲突矩阵、技能请求管线门禁与确定性、属性修正器聚合性能**。

## 问题

1. 背包增删在"容量不足"时是否会留下半成品状态？重复提交（网络重传）会不会重复发放？
2. Buff 的同级刷新、叠加、替换、互斥、高层覆盖低级、低级恢复、驱散、来源变更、周期重触发这九类交互，边界到底如何？
3. 技能请求在冷却/公共冷却/资源/状态/距离/目标等门禁下的拒绝路径是否干净（无副作用）？相同输入流能否确定重放？客户端预测失败能否正确回滚？
4. 属性修正器聚合用"全量重算"相比"脏标记增量重算"差多少？这个差值是否足以支撑服务端每 Tick 的属性刷新预算决策？

## 假设

- 服务端逻辑是**单线程确定性**的：同一请求序列 + 同一初始状态 → 同一结果（对应帧同步/回放的确定性要求）。
- 背包容量与堆叠上限是硬约束，事务必须**全成功或全回滚**，不允许部分写入。
- Buff 的"高层覆盖低级"是压制（suppressed）而非删除，被压制者继续消耗自身剩余时长；高层结束后低级按剩余时长恢复。
- 技能请求携带唯一 `requestId`，重复投递按幂等处理。
- 属性聚合的语义为：`(base 或最后一个 Override) + ΣAdd` 再乘以 `ΠMul`。

## 历史环境（2026-09-11）

| 项目 | 值 |
| --- | --- |
| 主机 | Windows（MINGW64_NT-10.0-26200，x86_64，16 逻辑核） |
| 工具链 | MSYS2 MinGW-w64 `g++` 16.1.0，`-std=c++17 -O2` |
| 依赖 | 仅 C++ 标准库（`<chrono>` / `<vector>` / `<unordered_map>` 等），无第三方库 |
| 未使用 | 无 UE5.8、无 Dedicated Server、无网络层、无数据库 |

## 运行方式

```powershell
# Windows PowerShell 5.1 / pwsh 均可
& (Join-Path $RepoRoot 'evidence/tests/gameplay-core/scripts/build_run.ps1')
```

```bash
# MSYS2 / Git Bash：脚本会把 C:\msys64\mingw64\bin 加入 PATH
bash evidence/tests/gameplay-core/scripts/run_all.sh
```

两个脚本重新编译其固定列表中的五个程序，并把**未经修改的原始输出**写入 `results/*.txt`（`build/` 已被 `.gitignore` 忽略）。

## 输入

- 背包：40 槽背包、单堆叠上限 99、单次批量上限 999；400 000 次增删操作（2/3 为移除）。
- Buff：11 个 Buff 定义，覆盖 8 个互斥组、可叠层与不可叠层、法术/诅咒两种驱散类型、零时长边界。
- 技能：3 个技能定义（瞬发攻击 / 不可打断长吟唱 / 免费自增益）；确定性重放为 4 000 次伪随机请求（xorshift64 固定种子 12345）。
- 属性：20 000 个实体 × 16 个修正器，200 轮，每轮 10% 实体变脏。

## 指标与原始结果

| 程序 | 断言 | 结果 |
| --- | --- | --- |
| `inventory_txn` | T1–T7 | **pass=7 fail=0** |
| `buff_conflict` | R1–R9 + E1–E3 | **pass=12 fail=0** |
| `skill_pipeline` | S1–S10 | **pass=10 fail=0** |
| `attr_modifier_bench` | 400 抽样一致性 | **mismatch=0（pass=401）** |

关键数据（单次运行，原始值见 `results/`）：

```text
[inventory_txn]  slots=40 ops=400000
                 latency_ns p50=100 p95=200 p99=200 max=185600 mean=102
                 throughput_ops_per_sec=9759906

[attr_modifier_bench] entities=20000 mods_per_entity=16 rounds=200 dirty_percent=10
                 full_recompute    p50=1756.0us p95=2060.6us p99=2238.3us
                 dirty_incremental p50=234.9us  p95=316.5us  p99=422.7us
                 p50_speedup=7.47x

[skill_pipeline] 确定性重放 h1=908cfbcc0f3aa8f1 h2=908cfbcc0f3aa8f1 h3=be53337654527f00
```

原始输出：[inventory_txn.txt](results/inventory_txn.txt) ｜ [buff_conflict.txt](results/buff_conflict.txt) ｜ [skill_pipeline.txt](results/skill_pipeline.txt) ｜ [attr_modifier_bench.txt](results/attr_modifier_bench.txt)

## 结论

1. **背包事务可以做到零半成品**：容量不足时先做 dry-run 规划再提交，`T3` 证明失败路径下背包逐槽不变；`T4`/`T7` 证明 requestId 去重与超额移除守卫都无副作用。这条"先规划后提交"的顺序是背包/邮件/奖励发放类写操作应当复用的范式。
2. **Buff 的九类交互可以全部用断言固定**，其中三处最容易写错：`R6`（高级结束时低级按**剩余**时长恢复，而非满时长）、`E2`（被压制者剩余时长耗尽则直接过期，不复活）、`E3`（低级不能覆盖活跃的高级）。`R7` 说明驱散必须把**被压制实例**一并清除，否则会残留幽灵状态。
3. **技能请求管线的拒绝路径必须无副作用**：`S2`/`S4` 显示冷却与蓝量拒绝都不扣蓝、不写冷却；`S7` 显示网络重传只结算一次；`S9` 显示客户端预测在服务端拒绝后能回滚到权威值——这三条共同构成"客户端预测 + 服务端权威"的最小正确性骨架。
4. **技能管线可确定重放**：相同 4 000 次请求流两次运行得到同一状态哈希，换种子则不同，满足回放/帧同步对确定性的基本要求。
5. **属性聚合的增量化收益显著且值得**：在本机 20 000 实体 × 16 修正器下，脏标记增量重算的 p50 是全量重算的约 **1/7.5**（p99 同样约为 1/5.3）。服务端若每 Tick 全量刷新属性，成本随实体数线性上升；改用"修正器变更即置脏 + Tick 末批量刷新"能把属性预算压回与**变更量**而非**实体总量**相关的量级。

## 局限

- **不是 UE GAS**：这里验证的是算法与状态机语义，不涉及 `UAbilitySystemComponent`、`FGameplayEffectSpec`、属性捕获与网络预测的具体实现；UE 映射关系见关联文档。
- **单机单线程**：所有断言在单线程内串行执行，未覆盖多线程竞争、跨服迁移、断线重连后的状态对账。
- **计时精度有限**：背包微基准使用 `steady_clock`，粒度约 100 ns，因此 p50/p95/p99 落在 100/200 ns 量级；`max=185600 ns` 是首次分配造成的离群值，不代表稳态。
- **存在运行间波动**：同一程序重复运行，属性基准 p50 在 1750–1760 µs 之间、加速比在 7.4–7.8x 之间波动；`results/` 中保存的是某一次的真实输出，不取多次最优值。
- **模型简化**：背包为槽位模型（无绑定/唯一物品/耐久），Buff 为离散时长模型（无属性快照/快照重算），技能无目标筛选与命中判定，属性无依赖链与脏传播。
- 本目录**不主张**线上容量结论；线上预算仍需在真实 DS 环境复测。

## 2026-09-30 边界回归补充

本次只重新运行 `skill_pipeline` 和新增 `entity_lifecycle`，不改写上面的 Windows 历史性能数据，也不声称重跑其余三个程序。环境为 Linux x86_64、GCC 14.2.0、C++17；新增程序测试身份协议，不是 UE Actor 实现。

### 缺陷与修正

1. 原 Entity 示例满池返回 `{0,0}`，与首个存活实体身份冲突。改为 optional 空结果；引入 Retiring 状态，在广播前使普通查询失败；代际达到上限时退役槽位而不回绕。
2. 原技能管线只有距离上下界比较，NaN 可绕过范围拒绝。现在先 `std::isfinite`，拒绝 NaN/正负无穷，再做有限距离范围检查；失败路径只更新拒绝计数，不扣蓝、不写冷却或施法状态。
3. 模型中的距离和时间仍是测试输入；线上必须由服务器权威状态计算/验证，不能直接相信客户端上报。修改并不等同完成反作弊系统。

### 复现命令

`entity_lifecycle` 已纳入两个 runner；任一程序编译或运行失败，runner 最终返回非零，并继续尝试后续程序。下列独立命令便于只运行本次两个回归程序。PowerShell 默认 Root 已改为从脚本目录定位仓库根；该脚本修改仅静态审查，当前 Linux 环境没有 pwsh，未声称 Windows/PowerShell 运行通过。Shell 用受控编译器 fixture 验证全成功、编译失败、运行失败及新增 Entity 失败的返回码；同时复现旧版吞掉运行失败的问题，共 5 项检查通过。另在隔离目录实际编译运行全部五个程序，返回码均为 0：背包 7 项、Buff 12 项、技能 14 项、Entity 12 项、属性基准 401 项，均 fail=0；未覆盖或改写历史 Windows 原始结果。

在仓库根执行，不需要 UE 或第三方库：

```bash
mkdir -p /tmp/learning-boundary-tests
g++ -std=c++17 -O2 -Wall -Wextra -Werror evidence/tests/gameplay-core/src/entity_lifecycle.cpp -o /tmp/learning-boundary-tests/entity
/tmp/learning-boundary-tests/entity
g++ -std=c++17 -O2 -Wall -Wextra -Werror evidence/tests/gameplay-core/src/skill_pipeline.cpp -o /tmp/learning-boundary-tests/skill
/tmp/learning-boundary-tests/skill
```

预期分别为 `pass=12 fail=0` 与 `pass=14 fail=0`。程序用显式返回码报告失败，未依赖会被 NDEBUG 去掉的 assert。额外使用 `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer` 重编译运行，结果见原始日志。初次 LeakSanitizer 因宿主 ptrace 报运行环境错误，随后以 `ASAN_OPTIONS=detect_leaks=0` 运行；地址与未定义行为检查保留，内存泄漏检查未通过验证。

不要为这些边界测试开启 `-ffast-math` 或 `-ffinite-math-only`：GCC 官方选项说明允许优化器假定没有 NaN/Inf，这会破坏“先检验非有限值”的前提。该约束属于构建契约，需要在生产编译配置中单独检查，不能只凭单测通过推断。

### 验证矩阵与原始结果

| 程序 | 新增覆盖 | 实际结果入口 |
| --- | --- | --- |
| Entity 身份 | 0/满容量、无效句柄、退出期间隔离、重复清理、迟到清理、代际退役，共 12 项 | [entity_lifecycle_linux_gcc.txt](results/entity_lifecycle_linux_gcc.txt) |
| 技能门禁 | 保留 S1–S10，新增 S11–S13 非有限值及 S14 有限边界，共 14 项 | [skill_pipeline_linux_gcc.txt](results/skill_pipeline_linux_gcc.txt) |

实体源码见 [entity_lifecycle.cpp](src/entity_lifecycle.cpp)，技能源码见 [skill_pipeline.cpp](src/skill_pipeline.cpp)。日志同时记录源码 SHA-256 和实际编译命令，避免以后代码变化却继续引用旧结果。原来的 `skill_pipeline.txt` 仍是旧版十项用例的历史结果，不代表当前版本只有十项。

边界：此次没有运行 Windows/MSVC、UE 自动化、网络乱序或多线程压力测试；关闭 LeakSanitizer 后 ASan/UBSan 对本次模型运行无报错，不证明业务、所有输入或线程安全。实体上限为 2 的测试仅验证溢出保护分支，不是真实循环 2³² 次。

## 2026-10-01 持久化幂等补充

### 问题与假设（2026-10-04 合同修订）

原背包 C++ 模型的进程内 requestId 集合并不证明数据库持久化幂等。[idempotency_sqlite.py](src/idempotency_sqlite.py)单独研究“去重占位、扣款发货、结果快照是否同一次提交”及结果未知后的同键重放。原 C++/PowerShell runner 仍只列五个程序，不执行此 Python 实验。

模型用新建的临时 SQLite 文件库、独立连接和显式 `BEGIN IMMEDIATE`，假设一份权威数据库、无跨库外部副作用。租户/账号应由调用者从认证上下文提供，operation 由服务端业务端点选择，四项 `(tenant, account, operation, key)` 组成唯一域；模型不实现认证。商品固定 potion、单价 100、种子余额 1000。暂时错误回滚；余额不足保存 REJECTED 快照，充值后同键仍重放旧拒绝，新意图用新键。长任务示例另有 job epoch，资源更新与阶段转换同事务。

修订前 `operation=None` 两次返回 SUCCEEDED，却将余额 1000→900→800、物品 0→1→2，并留下两条 NULL operation/PROCESSING/NULL snapshot。普通 SQLite 复合主键允许 NULL 且 NULL 不相等，`operation=?` 绑定 NULL 又匹配不到终态更新，旧代码未检查 0 行就提交。**不是 SQLite 事务失效**：`key=None` 原本已拒绝，合法字符串同键原本仍只扣一次。

2026-10-04 修订闭合三个边界：

- 连接前校验完整 scope：tenant/account 为 exact int、SQLite signed64 范围，拒绝 bool/float/str/整数子类；允许可表示的 0/负数，账号存在性另查，不把类型检查当认证
- operation/key 均为非空且可 UTF-8 编码的字符串；key 最多 128 个 Python 字符。保持大小写、空白、Unicode 原始码点序列与内嵌 NUL，拒绝孤立 surrogate，不 trim/lower/Unicode 规范化/截断；空白字符串有效，v1/v2 继续隔离。原商品/quantity 校验保留
- wallet 身份两列、idem 唯一域四列显式 NOT NULL；这是数据库空值兜底，普通表的类型亲和性仍不能替代 API 合同。终态 UPDATE 限定 PROCESSING/NULL snapshot 并要求 rowcount 精确为 1，否则整笔事务回滚

### 运行方式与版本边界

```bash
# 仓库根；Python 3.11+、内置 SQLite 3.35+，不安装第三方包
# 输出文件必须尚不存在、父目录已存在且位于仓库外；含空格路径加引号
bash evidence/tests/gameplay-core/scripts/run_idempotency.sh /tmp/new-idempotency-run.log
# 可选：PYTHON=/path/to/python3 IDEMPOTENCY_TIMEOUT_SECONDS=60 bash ... /tmp/new.log
# 直接运行模型，不写仓库日志
python3 -B evidence/tests/gameplay-core/src/idempotency_sqlite.py
python3 -B -O evidence/tests/gameplay-core/src/idempotency_sqlite.py
# Linux/Bash runner 自身的永久回归；会实际运行模型及故障子进程
python3 -B evidence/tests/gameplay-core/scripts/test_idempotency_runner.py
python3 -B -O evidence/tests/gameplay-core/scripts/test_idempotency_runner.py
```

[run_idempotency.sh](scripts/run_idempotency.sh)没有默认结果路径，拒绝已存在文件、目录、symlink、旧 raw 及经父目录 symlink 指向仓库内的新输出。日志独占创建，写失败/子进程失败/超时均非零，失败保留部分日志；不会覆盖历史输出。超时按每次模型运行计算，默认 60 秒，Linux 下终止该运行的进程组。环境变量 PYTHON 是一个可执行路径，不是待 eval 的命令串。此工具假定本机输出祖先目录不会被恶意并发替换，不是安全沙箱。

runner 分别记录真实 stdout/stderr、源码与 runner SHA-256、命令、环境和退出码。只有两次退出 0、各有非空标准 unittest 完成报告、普通/优化测试数相同才打印成功尾标；无报告、Ran 0、FAILED 却 exit 0、损坏报告都不是通过。子进程环境仅移除继承的 PYTHONOPTIMIZE，确保第一轮 optimize=0、显式 -O 轮 optimize=1，其他环境保留；继承值 1/2 都有真实模型回归。不硬编码测试总数；输出协议不防测试程序主动伪造报告。测试数为 unittest 方法数，方法内 subTest 不另计数。

Python 3.12 新增 autocommit，isolation_level=None 依赖 legacy transaction control。连接函数在该选项存在时显式传 `LEGACY_TRANSACTION_CONTROL`，保留 SQL BEGIN/COMMIT/ROLLBACK；Python 3.11 用原接口。没有引入 STRICT 或生产库迁移，也未实跑门槛以上的每个组合。

### 实际环境、输入与原始结果

- 2026-10-01 历史：Linux x86_64、Python 3.12.14、SQLite 3.53.1；原 19 项普通与 -O 输出 [idempotency_sqlite_linux.txt](results/idempotency_sqlite_linux.txt)逐字保留，只说明旧实现已测场景
- 2026-10-04 本地：同为 Linux x86_64、Python 3.12.14、SQLite 3.53.1；30 项语义测试普通/-O 各通过，23 项 runner 回归普通/-O 各通过。实际次数、源码哈希、修订前对照、完整输出与退出码见新 [idempotency_scope_contract_20261004.txt](results/idempotency_scope_contract_20261004.txt)
- 每例独立临时库/目录，只有合成身份与商品，不接触真实账号或凭据。unittest 断言在 -O 下仍执行。runner 回归的成功路径运行当前真实 SQLite 模型；失败/超时/写失败也是实际子进程/文件错误，坏版本前检用明确标记的版本元数据模拟
- CI 平台入口与本地证据分开：[knowledge workflow](../../../.github/workflows/knowledge.yml)配置普通/-O 模型的 Ubuntu/Windows 步骤及 Linux runner 回归；各提交是否通过以相应 CI 运行记录为准。本次归档不包含 Windows 本地执行，也不预先宣布远端 CI 通过

| 用例 | 场景 | 断言 |
| --- | --- | --- |
| 01–06 | 首次/重放、异参、账号/租户隔离、新意图、拒绝后充值 | 正确域去重，冲突无副作用；旧快照不被后来状态改写 |
| 07 | 扣款后、发货前抛异常 | 余额、物品、去重占位一起回滚，再试仅提交一次 |
| 08–09 | 子进程在 COMMIT 前/后 `os._exit(86)` | 前者无提交，后者同键回放已提交快照；父进程校验退出码 |
| 10 | 首个连接持有写事务，第二个连接立即尝试 | 精确 SQLITE_BUSY，不把锁竞争当首次执行许可；稍后重放 |
| 11 | 8 个线程、各自连接、屏障同时起跑 | 相同快照，最终余额 900、物品 1、去重记录 1 |
| 12–13 | 故意坏负例：业务与去重分开提交；删尽记忆后旧键重试 | 复现余额 800/物品 2 的坏结果；PASS 表示捕捉到缺陷 |
| 14–15 | 旧 epoch、重复完成、阶段更新后故障 | 旧/已完成执行者不能再加物品；阶段与效果一起回滚 |
| 16–19 | 非法数量、v1/v2 操作域、已提交 PROCESSING、缺键 | 非法/不确定请求不执行业务，操作域独立 |
| 20–24、30 | NULL/空/错类型/不可编码 scope、signed64 端点、键相等性 | 非法输入在连接前拒绝；合法边界同键重放一次，distinct 键不合并 |
| 25 | 不经过 API，直接给六列 INSERT/UPDATE NULL | SQLite IntegrityError；无多插入/空值更新 |
| 26–27 | 隔离库真实 BEFORE UPDATE trigger：RAISE(IGNORE)/RAISE(ABORT) | 终态影响 0 行或 SQL 报错时，钱包与占位全部回滚；不是 mock rowcount |
| 28–29 | hook 用新连接在 commit 前/后重入；显式事务状态 | 前者精确 BUSY 并外层回滚，后者只重放；BEGIN/ROLLBACK 状态正确 |

### 结论与局限

1. 非空唯一域、同库事务与终态精确写入共同闭合提交合同；BEGIN/COMMIT 本身不能修正错误域。终态 0 行有真实故障回归；完好复合主键下不应发生多行，代码 `!= 1` 同样拒绝，但未制造真实多行终态更新。
2. 同库事务保证本模型中的占位、目标效果与终态一起提交或回滚；丢失响应不等于业务失败。先业务后记录仍有双写窗口；删尽记忆仍可让旧键再生效，没有实现墓碑/保留期策略。
3. fencing 在实际提交端检查 epoch 和阶段才有效；本模型没有真实租约、时钟或 Redis 集群，不能宣称分布式锁安全或全局 exactly-once。
4. SQLite 写事务串行化，8 个连接不是生产压力测试；不能外推 PostgreSQL/MySQL 行锁、死锁、隔离、吞吐或 failover。hook 仅供故障注入，新连接重入不代表同连接嵌套事务支持。
5. os._exit 的宿主和文件系统仍运行，不是断电、磁盘损坏/fsync 或真实网络提交不确定性实验。本地本次未运行 UE、Outbox/MQ/第三方支付，没有完整鉴权、退款、审计或既存库迁移；结果也不能替代生产多数据库验证。

一手依据：[SQLite PRIMARY KEY/NOT NULL](https://www.sqlite.org/lang_createtable.html#the_primary_key)、[NULL 比较](https://www.sqlite.org/lang_expr.html)、[SQLite 事务](https://www.sqlite.org/lang_transaction.html)、[SQLite 隔离](https://www.sqlite.org/isolation.html)、[Python rowcount](https://docs.python.org/3.12/library/sqlite3.html#sqlite3.Cursor.rowcount) 与 [legacy transaction control](https://docs.python.org/3.12/library/sqlite3.html#transaction-control-via-the-isolation-level-attribute)。主责原理、表示域细节和生产矩阵见 [幂等重试与消息语义](../../../知识/07-网络与游戏服务端/持久化与分布式一致性/03-幂等重试与消息语义.md)，重试时间预算见 [限流熔断背压](../../../知识/07-网络与游戏服务端/运行调度与过载保护/02-限流熔断背压与过载保护.md)。

## 关联知识文档

- [系统实战/05-背包道具完整链路](../../../知识/05-Gameplay与交互系统/背包装备与存档/05-背包道具完整链路.md)（本证据的主要使用者）
- [系统实战/03-技能释放完整链路](../../../知识/05-Gameplay与交互系统/技能战斗与属性结算/03-技能释放完整链路.md)
- [系统实战/04-Buff系统完整链路](../../../知识/05-Gameplay与交互系统/技能战斗与属性结算/04-Buff系统完整链路.md)
- [游戏服务端/03-业务系统设计/01-背包与道具系统](../../../知识/05-Gameplay与交互系统/背包装备与存档/01-背包与道具系统.md)
- [游戏知识/03-游戏玩法编程/13-背包与装备系统](../../../知识/05-Gameplay与交互系统/背包装备与存档/13-背包与装备系统.md)
- [游戏知识/03-游戏玩法编程/01-GameplayAbilitySystem能力系统](../../../知识/05-Gameplay与交互系统/技能战斗与属性结算/01-GameplayAbilitySystem能力系统.md)
- [游戏测试与质量/01-测试金字塔与测试策略](../../../知识/08-工程实践与质量/测试策略与自动化/01-测试金字塔与测试策略.md)
