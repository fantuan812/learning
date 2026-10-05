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
updated: 2026-10-05
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
3. 技能请求被门禁拒绝时，除拒绝计数外，哪些完整Actor字段应保持？接纳过的ID如何去重？同输入的完整状态是否一致，以及单个蓝量纠正与完整客户端预测有什么区别？
4. 属性修正器聚合用"全量重算"相比"脏标记增量重算"差多少？这个差值是否足以支撑服务端每 Tick 的属性刷新预算决策？

## 假设

- 服务端逻辑是**单线程确定性**的：同一请求序列 + 同一初始状态 → 同一结果（对应帧同步/回放的确定性要求）。
- 背包容量与堆叠上限是硬约束，事务必须**全成功或全回滚**，不允许部分写入。
- Buff 的"高层覆盖低级"是压制（suppressed）而非删除，被压制者继续消耗自身剩余时长；高层结束后低级按剩余时长恢复。
- 技能请求的 `requestId` 是每Actor生命周期内的不透明请求身份：只记成功接纳过的ID；重复返回duplicate，失败请求可重试，不提供原结果缓存、意图绑定、单调序号或跨重启幂等。
- 属性聚合保留最后Override，再按列表原顺序累加Add和连乘Mul，最后做加/乘；浮点运算不能任意重关联。当前有限输入/失败cache合同见下方修订。

## 历史环境（2026-09-11）

| 项目 | 值 |
| --- | --- |
| 主机 | Windows（MINGW64_NT-10.0-26200，x86_64，16 逻辑核） |
| 工具链 | MSYS2 MinGW-w64 `g++` 16.1.0，`-std=c++17 -O2` |
| 依赖 | 仅 C++ 标准库（`<chrono>` / `<vector>` / `<unordered_map>` 等），无第三方库 |
| 未使用 | 无 UE5.8、无 Dedicated Server、无网络层、无数据库 |

## 运行方式

2026-10-04 起，两个通用入口必须指定**尚不存在的仓库外目录**；不再写回或覆盖仓库 `build/`、`results/`。它们保留五目标列表、C++17/O2 编译与默认 main，仅把子进程、输出和失败处理集中在 Python driver。需要可用的 Python；`PYTHON` 环境变量可指定单个解释器路径，不接收 shell 命令串。

```powershell
# 保留 Root/Gxx；OutputDir 必填，父目录先存在且目标目录尚不存在
$env:PYTHON = 'python'
& (Join-Path $RepoRoot 'evidence/tests/gameplay-core/scripts/build_run.ps1') `
  -Root $RepoRoot -Gxx 'C:\msys64\mingw64\bin\g++.exe' `
  -OutputDir (Join-Path $env:TEMP 'gameplay-five-models-new')
```

```bash
# CXX 是单个编译器路径；未指定时仍保留 MSYS2 默认路径检测，否则使用 g++
PYTHON=python3 CXX=g++ bash evidence/tests/gameplay-core/scripts/run_all.sh \
  --output-dir /tmp/gameplay-five-models-new
```

新目录中 `build/<target>.exe` 是本次构建，`logs/<target>.txt` 记录源码哈希、实际命令、完整输出与退出状态。已有文件、目录、符号链接、仓库内路径及解析后落回仓库的别名均拒绝；失败不覆盖历史证据。编译失败不运行旧二进制，能继续的后续目标仍尝试，任一构建、运行、超时或日志写失败均返回非零。单个命令默认上限 30 秒，超时是失败；需调整时直接使用 Python 的显式通用模式 `--legacy-five-targets --timeout-seconds N`。

**通用入口会执行各程序当前默认 main，其它模型仍可能运行历史微基准。** 聚焦背包合同验证用下面单独入口，不启动其它四个 C++ 模型的基准；其 fixture 自测只执行合成小程序。PowerShell 脚本入口是否运行于 Windows、Linux pwsh 或仅静态审查，按本次结果分开记录，不由脚本文件名推定。

```bash
python3 -B evidence/tests/gameplay-core/scripts/run_inventory_contract.py \
  --cxx g++-14 --output-dir /tmp/inventory-contract-new
python3 -B evidence/tests/gameplay-core/scripts/run_inventory_contract.py \
  --self-test --pwsh /path/to/pwsh --output-dir /tmp/inventory-runner-check-new
```

## 输入

- 历史背包基准输入：40 槽、单堆叠上限 99、单次批量上限 999；400 000 次增删操作（2/3 为移除）。当前聚焦合同不运行这段基准；其它模型输入仍按各自历史条目理解。
- 历史Buff：11个定义的12场景；当前独立合同另覆盖33场景，详见下方修订，不把旧输入与新覆盖混计。
- 历史技能输入：3个定义（短吟唱攻击 / 不可打断长吟唱 / 免费自增益），4000次固定seed伪随机请求；旧摘要只覆盖部分状态。当前聚焦合同补完整Actor及时间加法边界，见下方修订。
- 历史属性基准：20000实体×16修正器、200轮，每轮2000次有放回mutation尝试，不是恰好10%不同实体变脏；当前聚焦41场景不运行该基准。

## 指标与原始结果

| 程序 | 断言 | 结果 |
| --- | --- | --- |
| `inventory_txn` | T1–T7 | **pass=7 fail=0** |
| `buff_conflict` | R1–R9 + E1–E3 | **pass=12 fail=0** |
| 历史 `skill_pipeline` | S1–S10 | **历史pass=10 fail=0，非完整预测/重放证明** |
| 历史 `attr_modifier_bench` | 首次mutation前400次同求值器抽样 | **历史mismatch=0（pass=401），非变更后合同** |

关键数据（单次运行，原始值见 `results/`）：

```text
[inventory_txn]  slots=40 ops=400000
                 latency_ns p50=100 p95=200 p99=200 max=185600 mean=102
                 throughput_ops_per_sec=9759906

[attr_modifier_bench] entities=20000 mods_per_entity=16 rounds=200 dirty_percent=10
                 full_recompute    p50=1756.0us p95=2060.6us p99=2238.3us
                 dirty_incremental p50=234.9us  p95=316.5us  p99=422.7us
                 p50_speedup=7.47x

[skill_pipeline] 历史部分状态摘要 h1=908cfbcc0f3aa8f1 h2=908cfbcc0f3aa8f1 h3=be53337654527f00
```

原始输出：[inventory_txn.txt](results/inventory_txn.txt) ｜ [buff_conflict.txt](results/buff_conflict.txt) ｜ [skill_pipeline.txt](results/skill_pipeline.txt) ｜ [attr_modifier_bench.txt](results/attr_modifier_bench.txt)

## 结论

1. **历史背包断言只覆盖有限路径**：`T3` 检查容量拒绝时逐槽不变，`T4` 检查同键不再加物品，`T7` 检查超量移除。它们没有覆盖槽位修改后的去重节点/桶分配失败，也没有验证异参冲突和原结果重放；旧实现实际可在 `bad_alloc` 后重试双发。完整异常准备与无抛出发布边界见下方 2026-10-04 背包合同修订，旧 7 项全绿不能推出所有失败路径无副作用。
2. **历史Buff十二场景只覆盖有限交互**：R6/E2仍有剩余时长与不过期复活的价值，但未覆盖受压刷新、三层赢家和可叠child周期事件；旧全绿不能证明九类交互的组合正确。R7只验证本模型按school删除所有匹配条目（含被压制者），不是所有游戏驱散都须全删。2026-10-04修订在下方补出具体反例、组内不变量、全状态oracle和明确政策。
3. **技能旧断言没有证明完整拒绝合同**：S2/S4检查了部分蓝量/冷却，S3只查reason；GCD拒绝偷偷扣蓝的真实变体仍能通过原14项。正常业务拒绝本来会增加rejected，因此应检查完整Actor仅该计数变化。S7是当前Actor中成功接纳ID去重，S9确实演示单个predictedMana标量纠正；两者不构成持久结果重放或完整客户端预测系统。
4. **旧技能摘要只是部分状态投影**：相同seed同build下摘要一致的观察保留，但该hash未覆盖全部冷却、GCD、施法与身份集合，不能用它证明完整状态或跨平台确定性。不同冷却状态可有同一摘要；当前验收按逐步字面结果与完整Actor字段比较，模型仍不含世界快照、网络确认队列和历史重模拟。
5. **属性旧计时不是收益保证**：保留单次p50/p99与7.47x历史标签，撤回据此保证项目收益及“成本只与变更量相关”的结论。旧incremental仍全表扫dirty，O(N+kM)，且400检查在mutation前；当前源码加入新拒绝政策与成功后cache发布，旧计时不代表它的成本。变更后正确性和读屏障由下方独立合同另证。

## 局限

- **不是 UE GAS**：这里验证的是算法与状态机语义，不涉及 `UAbilitySystemComponent`、`FGameplayEffectSpec`、属性捕获与网络预测的具体实现；UE 映射关系见关联文档。
- **单机单线程**：所有断言在单线程内串行执行，未覆盖多线程竞争、跨服迁移、断线重连后的状态对账。
- **背包历史计时的边界**：旧 raw 报告 p50/p95/p99 为 100/200 ns、max 为 185600 ns；它没有独立时钟分辨率测量或 max 样本的调用链证据，不能由分位数取整推定 100 ns 粒度，也撤回“max 由首次分配造成”的未证归因。新代码的成功记录、固定 Result 和异常准备已改变，旧数字不为新实现的性能背书。
- **历史波动结论缺少可复核样本**：先前文字的1750–1760µs、7.4–7.8x范围没有在此附上完整多轮输出，本批不继续以它作为可重复收益结论。现有属性raw保留为历史单次记录；本轮没有重跑旧基准或用功能通过推断性能。
- **模型简化**：背包为槽位模型（无绑定/唯一物品/耐久），Buff 为离散时长模型（无属性快照/快照重算），技能无目标筛选与命中判定，属性无依赖链与脏传播。
- 本目录**不主张**线上容量结论；线上预算仍需在真实 DS 环境复测。

## 2026-10-04 批次：技能接纳、时间边界与证据范围修订

### 原来的绿灯漏掉了什么

S3只检查返回gcd，若这一拒绝分支偷偷扣1蓝，旧14项仍能全部通过。新检查先独立固定正确的前置状态，再比较拒绝前后的全部Actor字段，只有rejected可以增一。另一反例中，两次合法请求得到相同旧摘要，但冷却分别为6000与6100；hash没有覆盖这个字段，不能承担完整状态oracle。

S9原本有真实价值：服务端拒绝后，把一个本地蓝量整数纠正为权威值，删除赋值会失败。它没有客户端世界、pending请求表或历史重模拟。两个未确认请求A耗20、B耗40时，拒绝A后仍应保留B的预测，显示60；直接赋回100会丢掉B。此处是隔离教学反例，并未实现网络预测系统。

### 模型保留的语义与新增拒绝

[模型](src/skill_pipeline.cpp)按调用顺序处理，每Actor只记已经成功接纳过的不透明requestId。低ID晚到仍可能接纳；重复ID返回duplicate，即使意图改变也没有结果/意图校验；被拒ID可重试。它没有lastSeq水位、原结果缓存、epoch、记忆淘汰、跨重启去重或认证。

调用方仍须提供正确选定的Actor、有效状态、执行期间不变的有效定义、非负不倒退的权威时间及可递增计数。距离/目标/阵营/技能习得由可信适配层核验；本例只检查hostile请求targetId非零和有限/范围距离。接纳时立即扣蓝并写CD/cast/GCD、接纳ID；Interrupt只重置可打断施法的两个字段，不退款，也没有Active/Recovery推进或伤害事件接线。

本批明确新增time-overflow拒绝：保持全部旧gate先后，在第一次玩法写入前，分别安全计算本次选定定义的CD、cast和GCD候选。任何一个超出int64范围都只增加rejected；不能先发生有符号溢出再检测，未选定义的极大时长也不能导致误拒。恰好INT64_MAX仍可表示。容器插入仍可能抛异常，故“正常拒绝无玩法写入”不等于分配失败强原子性；不在此批改造事务协议。

### 实际执行与覆盖边界

本地采集为Linux、GCC14.2、C++17、Python3.12；批次跨2026-10-04/05 UTC，逐命令起止以原记录为准。最终同一CI命令块实际运行O0+NDEBUG、O2、UBSan：各保留旧14项，并通过42个合同/教学场景、267个显式检查，每次116条操作账本。42包含算术helper与三个隔离教学练习，不能全称为42种实际游戏流程。[独立字面测试](tests/skill_contract.cpp)按顺序列出输入、理由和完整Actor，map/set排序用于输出，不以DUT的弱hash自证。

另一个提前固定的独立oracle在三构建中各102检查通过，包含三个deadline独立exact-MAX/超1、早期gate优先、未选MAX定义、溢出后同ID重试。不同清单的数字不相加当业务覆盖率。

5个真实编译/运行变体分为**3模型负控**（GCD扣蓝、移除finite guard、把时间溢出错误饱和）与**2教学负控**（移除旧S9标量纠正、新值拷贝练习改为别名）。都要求编译成功后出现指定语义FAIL并exit1；崩溃/编译失败不算检测到语义错误。旧S11–S14及S9自身能发现对应变体的价值保留，两个教学负控不证明DUT拥有预测实现。

[聚焦runner](scripts/run_skill_contract.py)只读导入Inventory的捕获/路径helper，不运行Inventory或五目标默认入口。最终自测146项通过；独立普通Python与-O各146/0，另有optimize=0的实际CLI故障61/0与路径/写失败18/0。覆盖编译失败不运行残留二进制、运行失败、超时、日志open/write失败、拒覆盖、中文/空格路径、非法UTF8/CRLF/NUL原流、缺/重复/畸形清单及FAIL却exit0。

审查还实际发现“全CHECK PASS但账本expected/actual矛盾”的报告曾被接受；当前正常报告显式检查完整字段集合、expected==actual及Handle理由一致，语义负控允许其预期差异并须满足指定FAIL/exit1。这只是报告一致性，不能防测试程序故意伪造。有限文件快照也不保证已逃逸后代的未来输出全部捕尽；未使用的环境flag只记录存在布尔，不公开其值。

v1自测124项有1失败：64KiB文件上限在编译产物写入就触发，未抵达预期日志写故障。该失败原样保留；修正fixture后132项通过，再补账本矛盾用例为146项。后者用128KiB限制，让合成程序先成功运行，再由约240KiB commands.json真实写入触发EFBIG、FINAL_LOG_FAILURE和非零。初次PowerShell缓存目录只读导致未启动的记录也保留，改用隔离HOME/XDG后才取得实际通过结果。

```bash
python3 -B evidence/tests/gameplay-core/scripts/run_skill_contract.py \
  --output-dir /tmp/skill-contract-NEW --cxx g++-14 --mode all --negative-controls --timeout-seconds 120
python3 -O -B evidence/tests/gameplay-core/scripts/run_skill_contract.py \
  --output-dir /tmp/skill-runner-NEW --self-test --timeout-seconds 120
```

输出必须是尚不存在的仓库外目录；失败返回非零。raw的base64/长度/SHA为权威字节，分组日志只索引commands.json，避免嵌套复制整套输出。[Linux-only CI](../../../.github/workflows/knowledge.yml)有8分钟步骤上限、逐命令退出传播与诊断保留；本地真实相同PowerShell块exit0，合成首命令exit23会立即结束且不执行第二命令。未运行Windows/MSVC模型、UE/GAS、真实网络/预测/持久化或新性能基准；远端CI结果以该提交为准。

### 无损原流与可复核归档

[摘要](results/skill_contract_20261004.txt)不是完整raw。[技术原流归档](results/skill_contract_20261004.raw.jsonl.gz)收录所列模型/runner实验的原输入、命令、输出及既有真实失败。1776个逻辑文件共68,821,459原字节，以958个file记录和818个直接alias保存；实际存储55,473,435唯一字节，JSONL 74,356,239字节，确定性gzip 4,878,156字节。alias只去重存储，不减少原路径、原记录或失败，也不是额外执行次数；每个逻辑文件都已逐字节重建并与原文件比对。

- gzip SHA256：`66e93abe5a3b7b53334f124fdaa4fb47be0a34e39a89ce63c1743490dc37a6d2`
- JSONL SHA256：`f8a366d6b0366d26e7cbd3b69179a30adc53fcaf44301d78b8941e25925d4bbc`

file保存原bytes的base64/长度/SHA；alias仅引用此前直接file，保有自己的逻辑路径/长度/SHA，禁止链、环、前向/缺目标、重复或越界路径。下面配方只校验，不解包、不执行归档命令。正常Python与-O共48项实际正负检查通过，覆盖损坏/截断、内外摘要、字段/长度、重复路径及坏alias；内层负例重算外层摘要，避免只撞第一层校验。旧75份raw和282原件原字节保全；原有其他模型结果不因本批通过而获得新的背书。

```bash
python3 -O - evidence/tests/gameplay-core/results/skill_contract_20261004.raw.jsonl.gz <<'PY'
import base64, gzip, hashlib, json, sys
from pathlib import Path, PurePosixPath

GZIP_SHA256 = '66e93abe5a3b7b53334f124fdaa4fb47be0a34e39a89ce63c1743490dc37a6d2'
JSONL_SHA256 = 'f8a366d6b0366d26e7cbd3b69179a30adc53fcaf44301d78b8941e25925d4bbc'
COUNTS = (1776, 958, 818, 68821459, 55473435)

def require(condition, message):
    if not condition:
        raise ValueError(message)

def safe_path(name):
    require(isinstance(name, str) and name and '\\' not in name and '\0' not in name, 'bad path')
    q = PurePosixPath(name)
    require(not q.is_absolute() and '..' not in q.parts and name == q.as_posix() and
            name != '.' and all(':' not in part for part in q.parts), 'unsafe path')
    return name

def verify(path, gzip_sha256=GZIP_SHA256, jsonl_sha256=JSONL_SHA256):
    packed = Path(path).read_bytes()
    require(hashlib.sha256(packed).hexdigest() == gzip_sha256, 'gzip SHA mismatch')
    data = gzip.decompress(packed)
    require(hashlib.sha256(data).hexdigest() == jsonl_sha256, 'JSONL SHA mismatch')
    require(data.endswith(b'\n'), 'missing final newline')
    rows = [json.loads(line) for line in data.decode('utf-8').splitlines()]
    require(rows and isinstance(rows[0], dict), 'missing header')
    header = rows[0]
    require(header.get('kind') == 'archive_header' and
            header.get('format') == 'learning.skill.evidence.v1', 'bad header')
    direct = {}; seen = set(); aliases = 0; total_bytes = 0; unique_bytes = 0
    for row in rows[1:]:
        require(isinstance(row, dict), 'non-record')
        kind = row.get('kind')
        expected = {'kind', 'path', 'bytes', 'sha256', 'data_base64' if kind == 'file' else 'target'}
        require(kind in ('file', 'alias') and set(row) == expected, 'unsupported record shape')
        name = safe_path(row['path'])
        require(name not in seen, 'duplicate logical path')
        seen.add(name)
        require(type(row['bytes']) is int and row['bytes'] >= 0, 'invalid length')
        if kind == 'file':
            raw = base64.b64decode(row['data_base64'], validate=True)
            direct[name] = raw
            unique_bytes += len(raw)
        else:
            target = safe_path(row['target'])
            require(target in direct, 'alias must reference a previous direct file, never an alias')
            raw = direct[target]
            aliases += 1
        require(len(raw) == row['bytes'], 'length mismatch')
        require(hashlib.sha256(raw).hexdigest() == row['sha256'], 'record SHA mismatch')
        total_bytes += len(raw)
    actual = (len(seen), len(direct), aliases, total_bytes, unique_bytes)
    names = ('logical_files', 'file_records', 'alias_records', 'logical_original_bytes', 'stored_unique_bytes')
    require(all(type(header.get(name)) is int for name in names), 'header counters must be integers')
    require(actual == tuple(header[name] for name in names) == COUNTS, 'archive counts mismatch')
    return dict(zip(names, actual))

if __name__ == '__main__':
    require(len(sys.argv) == 2, 'usage: python [-O] verify.py ARCHIVE.raw.jsonl.gz')
    print(json.dumps(verify(sys.argv[1]), sort_keys=True))
PY
```

## 2026-10-04 属性求值与 dirty-cache 合同修订

### 为什么旧400次一致性检查会漏错

旧检查发生在首次mutation之前，并把cache与同一个Evaluate再次调用的结果比较。它既看不到之后漏置dirty/漏Refresh，也可能让“期望输入和DUT一起丢了同一修正器”继续相等。正常有限输入、正确置脏并刷新的原路径本来就是正确的；本批没有把这种覆盖缺口描述成原数学公式全部错误。

当前实验把调用方的意图账本和DUT字段分别更新，再与字面答案核对。添加Add15到base120后应为135；漏掉DUT的那次添加不能同时抹去期望。清dirty前需成功刷新；需要本次变更后的值时先刷新再取结算快照，只有明确的延迟可见设计才能统一等到Tick末。

### 选定合同、代价和边界

保留原列表语义：最后一个Override替代base；Add按原顺序累加、Mul按原顺序连乘，最终 `(basis + add) * mul`。不能混序逐项apply；source只是身份元数据，不提供自动去重/撤销/优先级。正负有限值、零/负Mul与通常binary64舍入/下溢仍允许，玩法数值下限另定。

新增教学政策明确拒绝非有限base/所有operand、未知op与每阶段非有限中间结果，连被后续Override遮蔽的项也拒绝。不能先溢出再用抵消或乘零“救回”。TryEvaluate计算候选，TryRefresh仅在成功时更新cache/清dirty；失败旧cache字节保留，但dirty/error令TryReadCurrent拒读且保持调用者out不变。源字段不回滚，一批可部分成功。公开字段仍要求调用方置dirty，getter无法发现漏标的修改；它不是事件/依赖系统，也不是线程或整批事务。

这三个显式错误/读取入口取代原toy Evaluate/Refresh接口，不是生产工程的无缝替换。默认main只跑10个小例；原计时形状须显式--benchmark才执行，本批未运行该选项。旧原始输出及7.47x标签保留为历史，不能绑定新校验代码：原每轮2000次有放回mutation并非2000个不同实体；增量阶段仍全表扫dirty，O(N+kM)，非O(kM)，且计时不含此前mutation。原固定先full后incremental、数据阶段不同、batch分位与旧p50索引差异都限制收益推断；功能合同通过不为历史性能背书。

构建记录固定实际flags：禁止fast-math、finite-only假设、重关联，关闭FP contraction；源码宏只检查能检测到的两类模式，不能认证全部编译器/运行时浮点环境。依据[GCC14.2优化选项](https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Optimize-Options.html)，这些变换可改变NaN/Inf及舍入语义。本次限定Linux/GCC14.2二进制64位double，不承诺UE/GAS或跨平台逐位一致。

### 实际验证与复现

Linux / GCC14.2 / C++17，O0+NDEBUG、O2、UBSan各41场景/679显式检查零失败，默认main的10例通过。7个真实变体分为3个调用方漏标/漏刷/丢修正器，以及4个求值/错误处理变体；都须编译成功、跑完完整版本化清单，在指定场景失败且exit1，编译失败和崩溃不算杀死语义变体。另有2种故意不兼容的浮点构建，须命中指定宏守卫而编译拒绝，单独统计。

独立预冻结probe保留正常有限路径288检查；旧实现就能通过正常路径，而旧400次自比较见不到随后漏标/漏刷/丢项。新源码三构建各正常288/0、新拒绝政策315/0；另加真实current读取与失败cache的304/0，验证错误时out与旧cache字节保持、修正后恢复、逐属性成功/失败隔离。额外5个模型错误变体在三模式编译后均出现语义失败。各数字来自不同清单，不相加充当业务场景数或“全输入证明”。

最终v3作者与独立审者的focused/runner均实际运行；Python -O runner自测112/0。它实际拒绝低检查数、空/缺/重复/畸形/错版本/FAIL却exit0报告，验证编译失败不运行遗留二进制、运行/超时/写日志失败、路径拒覆盖、原始非法UTF8/CRLF/NUL捕获与中文/空格路径。selected --root的源码身份和真正执行的runner/helper分别记录；被忽略的CXXFLAGS等只记存在布尔，不披露未使用的环境值。v1/v2的旧字段均为null并已核对，旧版本和中间检查失败原样保留。

本地实际执行CI相同PowerShell块，正例exit0；合成首命令exit23即时返回23，第二命令未执行。没有为归档改名再重跑相同套件；后续相同源码哈希的最终门禁引用这次完整运行，旧模型的适用回归另跑。未运行本模型的Windows/MSVC、UE/GAS、事件/网络/真实属性依赖集成或新属性性能基准。

```bash
python3 -B evidence/tests/gameplay-core/scripts/run_attribute_contract.py \
  --output-dir /tmp/attribute-contract-NEW --cxx g++-14 --mode all --negative-controls --timeout-seconds 120
python3 -O -B evidence/tests/gameplay-core/scripts/run_attribute_contract.py \
  --output-dir /tmp/attribute-runner-NEW --self-test --timeout-seconds 120
```

目标须是尚不存在的仓库外目录。只读导入Inventory捕获helper，不运行Inventory suite/五模型默认入口；真实运行/编译/超时/日志错误非零，已有目录/文件/别名拒绝。完整版本化case/check与退出状态须一致；报告协议不防程序故意伪造测试。base64/bytes/SHA为权威原流；有限文件快照不保证已逃逸后代全部终止或未来输出已捕尽。

[Linux-only CI](../../../.github/workflows/knowledge.yml)两条命令即时传播退出码并保留诊断；本地实际执行同一PowerShell块的结果另列，不提前替远端CI背书。Windows仓库检查不会被当成Windows属性模型验证。背包主文仅修属性段，框架08仅补读屏障一段，不把它们重复计为两篇完整重写。

[人读摘要](results/attribute_contract_20261004.txt)不是完整raw；[完整归档](results/attribute_contract_20261004.raw.jsonl.gz)保留必要原技术输入/命令/输出与所有既有失败阶段，旧73raw及282原件原字节不变。归档含1,963个原技术文件（1,964条JSONL含头记录），作者六轮均保留，仅run-v3-focused/run-v3-selftest代表冻结版本。原文件共23,523,877字节，JSONL 31,767,474字节，确定性gzip 5,226,388字节（mtime=0）。包含原独立意图输入、旧反例、新正常/拒绝/可见性测试、真实负控与明确标注的准备/中间链接检查失败；没有把它们改写为通过。归档不包含编译二进制或重复的完整fixture仓源码。

- gzip SHA256：`ece45f29722d9fdcc038fc5caa3355a288317c9eb3dca2a3542999903ea3e5b4`
- JSONL SHA256：`0afb7964ee851596085df75895d049f7b842a5875dc08319dd9da89c41a49ec7`

配方只校验，不解包落盘或执行归档命令。每个file记录以data_base64保存原bytes，并核长度/SHA；外层摘要、记录数、重复/危险路径、坏长度/hash、压缩损坏/截断均显式失败，`-O`不移除。正常/-O共18项真实配方正负检查已通过，内层负例重算外层摘要以避免只测试第一关。

```bash
python3 -O - evidence/tests/gameplay-core/results/attribute_contract_20261004.raw.jsonl.gz <<'PY'
import base64, gzip, hashlib, json, sys
from pathlib import Path, PurePosixPath

GZIP_SHA256 = 'ece45f29722d9fdcc038fc5caa3355a288317c9eb3dca2a3542999903ea3e5b4'
JSONL_SHA256 = '0afb7964ee851596085df75895d049f7b842a5875dc08319dd9da89c41a49ec7'
FILE_COUNT = 1963

def require(condition, message):
    if not condition:
        raise ValueError(message)

def verify(path, gzip_sha256=GZIP_SHA256, jsonl_sha256=JSONL_SHA256, count=FILE_COUNT):
    packed = Path(path).read_bytes()
    require(hashlib.sha256(packed).hexdigest() == gzip_sha256, 'gzip SHA mismatch')
    data = gzip.decompress(packed)
    require(hashlib.sha256(data).hexdigest() == jsonl_sha256, 'JSONL SHA mismatch')
    require(data.endswith(b'\n'), 'missing final JSONL newline')
    rows = [json.loads(line) for line in data.decode('utf-8').splitlines()]
    require(len(rows) == count + 1, 'record count mismatch')
    require(rows[0].get('kind') == 'archive_header' and
            rows[0].get('format') == 'learning.attribute.evidence.v1' and
            rows[0].get('author_attempts') == 6 and
            rows[0].get('accepted_author_runs') == ['author/run-v3-focused', 'author/run-v3-selftest'], 'bad header')
    names = set()
    total = 0
    for row in rows[1:]:
        require(row.get('kind') == 'file', 'non-file record')
        name = row.get('path')
        require(isinstance(name, str) and name and '\\' not in name and '\0' not in name, 'bad path')
        q = PurePosixPath(name)
        require(not q.is_absolute() and '..' not in q.parts and name == q.as_posix() and name != '.', 'unsafe path')
        require(name not in names, 'duplicate record: ' + name)
        names.add(name)
        raw = base64.b64decode(row['data_base64'], validate=True)
        require(type(row['bytes']) is int and row['bytes'] >= 0 and len(raw) == row['bytes'], 'length mismatch: ' + name)
        require(hashlib.sha256(raw).hexdigest() == row['sha256'], 'file SHA mismatch: ' + name)
        total += len(raw)
    return {'files': len(names), 'original_file_bytes': total, 'jsonl_bytes': len(data)}

if __name__ == '__main__':
    require(len(sys.argv) == 2, 'usage: python [-O] verify.py ARCHIVE.raw.jsonl.gz')
    print(json.dumps(verify(sys.argv[1]), sort_keys=True))
PY
```



## 2026-10-04 Buff 组内仲裁与刷新合同修订

旧12场景覆盖单点行为，没有覆盖组合历史：低120→高121→再次120会把受压低级重新置为有效；三层可能只处理首个低级；可叠child的周期重挂会额外加层。旧原始结果仍保留，不能继续据它宣称九类交互已全面正确。

新模型把存活集合和有效集合分开：每个非空组仅最高等级幸存者有效。普通Apply、已有PeriodicReapply、Tick过期与Dispel都在返回前恢复整组不变量。已有低级可刷新但继续受压，新低级遇更高者则拒绝；六种三层施加顺序会留下不同存储历史，均只让最高等级生效。受压时间继续消耗，过期条目不复活。D4“拒绝不执行跨组排除”、D5“驱散返回前恢复”是本教学模型明确选定的行为变更，不是所有游戏的唯一正确政策。

一个BuffSystem隐含单目标，定义ID是身份，source可重绑定；不是按来源多实例。普通Apply可叠层并重复执行一次性有向跨组删除；已有child的PeriodicReapply只刷新时间/source、不加层、不重复排除，缺失child才走完整Apply。定义必须唯一、使用前注册且不变，maxStacks≥1、duration有限、dt有限非负且串行调用，排除配置不含自身group；这些不是已实现的配置校验。没有周期调度、来源死亡、驱散优先级/数量限制、稳定实例句柄或强异常保证。

本例优先让不变量易审查，每次变更重算赢家；当前Def也是线性查找，A个存活条目、D个定义时，单次RecomputeWinners最坏为O(A²D)。这不是生产规模优化方案；索引/排序或增量维护应在保住相同历史语义与测试后另行评估，不能凭功能通过宣称预算达标。

### 实际模型与测试边界

Linux / GCC 14.2.0 / C++17，严格警告下O0+NDEBUG、O2和UBSan三模式各保留12场景，并执行33场景/323显式检查零失败。8个真实源码变体必须编译成功、在指定场景明确FAIL并exit1；编译失败或崩溃不算通过负控。独立预先固定的54检查在旧源码中每模式32通过/22失败，当前三模式均54通过/0失败；探针源码与全部原流在归档中。

测试同时比较完整存活表、有效ID集和字面期望；投影100+有效potency×stacks仅为测试oracle，没有调用GAS或属性计算器。33场景与323检查是覆盖清单，三构建复跑不增加独立业务场景。runner要求本版完整323检查、完整case集合和一致的FAIL/退出状态；这只是报告完整性协议，不防测试程序主动伪造结果。

Python -O作者runner自测89检查通过，独立normal/-O外层各89检查通过；独立真实CLI的28个正负报告场景全部符合预期。早期v1确实把checks=33的截减报告误收为通过（普通与-O两项），修订后拒绝；另一次新增fixture错把未篡改的retained输出也要求失败，selftest-02出现1项真实失败，已修正并保留原输出。空/缺/畸形/重复报告、compile/run/timeout/write故障、缺编译器、拒覆盖与中文/空格路径均有实际隔离测试。未运行Windows C++、UE/GAS、网络、GameClock/真实属性集成、来源死亡或新的CPU基准。

### 聚焦复现与原始记录

```bash
python3 -B evidence/tests/gameplay-core/scripts/run_buff_contract.py \
  --output-dir /tmp/buff-contract-NEW --cxx g++-14 --mode all --negative-controls --timeout-seconds 120
python3 -O -B evidence/tests/gameplay-core/scripts/run_buff_contract.py \
  --output-dir /tmp/buff-runner-NEW --self-test --timeout-seconds 120
```

两个目标须尚不存在、位于仓库外且父目录已存在。入口只读复用已审Inventory捕获助手，不执行Inventory suite或五模型基准；该helper哈希纳入每次provenance。已存在路径/别名拒绝、编译失败不运行遗留二进制、日志写失败/超时非零；超时只保证原进程组清理与有限输出快照，不保证逃逸后代全部终止或未来输出已捕尽。base64/bytes/SHA是权威原字节，展示文本不承担无损合同。

本地Linux模型运行与[CI配置](../../../.github/workflows/knowledge.yml)分开：新增Linux-only步骤执行三构建与runner负控，并即时传播退出码；Windows仍执行仓库通用门禁，不意味着Windows C++或UE运行已验证。

[本次摘要](results/buff_contract_20261004.txt)不是完整raw；[完整gzip JSONL](results/buff_contract_20261004.raw.jsonl.gz)保留各轮实际成功、失败与被替代诊断，历史71个raw及282份书籍/日志原件未改。归档含2,362个原技术文件（2,363条JSONL，含头记录）：作者7轮全部保留，仅run-03/selftest-03代表冻结版本；另外保留独立前后对照/报告负控与本地CI整段验证。原文件共34,701,613字节，JSONL 46,756,938字节，确定性gzip 6,342,146字节（mtime=0）。旧阶段明确标为superseded或实际失败，不倒写成通过。

- gzip SHA256：`914e56f97a58e01625e6f10d65ac8357e8782f2da0e7e38c581d62f3eeced646`
- 解压JSONL SHA256：`565d643ef3c8b47818e382d9dea74a2452fd3cf9ba19d899fc0cbdae913ba30e`

下面只校验，不向磁盘解包，也不执行归档命令。逐文件data_base64为原字节，bytes/SHA单列；外层hash、重复/危险路径、错误长度/hash及压缩损坏/截断均显式拒绝，`-O`不会跳过。正常与-O共18项配方正负检查已实际运行，负例会重算外层digest以确实触发内层验证。

```bash
python3 -O - evidence/tests/gameplay-core/results/buff_contract_20261004.raw.jsonl.gz <<'PY'
import base64, gzip, hashlib, json, sys
from pathlib import Path, PurePosixPath

GZIP_SHA256 = '914e56f97a58e01625e6f10d65ac8357e8782f2da0e7e38c581d62f3eeced646'
JSONL_SHA256 = '565d643ef3c8b47818e382d9dea74a2452fd3cf9ba19d899fc0cbdae913ba30e'
FILE_COUNT = 2362

def require(condition, message):
    if not condition:
        raise ValueError(message)

def verify(path, gzip_sha256=GZIP_SHA256, jsonl_sha256=JSONL_SHA256, count=FILE_COUNT):
    packed = Path(path).read_bytes()
    require(hashlib.sha256(packed).hexdigest() == gzip_sha256, 'gzip SHA mismatch')
    data = gzip.decompress(packed)
    require(hashlib.sha256(data).hexdigest() == jsonl_sha256, 'JSONL SHA mismatch')
    require(data.endswith(b'\n'), 'missing final JSONL newline')
    rows = [json.loads(line) for line in data.decode('utf-8').splitlines()]
    require(len(rows) == count + 1, 'record count mismatch')
    require(rows[0].get('kind') == 'archive_header' and
            rows[0].get('format') == 'learning.buff.evidence.v1' and
            rows[0].get('author_attempts') == 7 and
            rows[0].get('accepted_author_runs') == ['author/run-03', 'author/selftest-03'], 'bad header')
    names = set()
    total = 0
    for row in rows[1:]:
        require(row.get('kind') == 'file', 'non-file record')
        name = row.get('path')
        require(isinstance(name, str) and name and '\\' not in name and '\0' not in name, 'bad path')
        q = PurePosixPath(name)
        require(not q.is_absolute() and '..' not in q.parts and name == q.as_posix() and name != '.', 'unsafe path')
        require(name not in names, 'duplicate record: ' + name)
        names.add(name)
        raw = base64.b64decode(row['data_base64'], validate=True)
        require(type(row['bytes']) is int and row['bytes'] >= 0 and len(raw) == row['bytes'], 'length mismatch: ' + name)
        require(hashlib.sha256(raw).hexdigest() == row['sha256'], 'file SHA mismatch: ' + name)
        total += len(raw)
    return {'files': len(names), 'original_file_bytes': total, 'jsonl_bytes': len(data)}

if __name__ == '__main__':
    require(len(sys.argv) == 2, 'usage: python [-O] verify.py ARCHIVE.raw.jsonl.gz')
    print(json.dumps(verify(sys.argv[1]), sort_keys=True))
PY
```



## 2026-10-04 背包内存合同修订

### 为什么原七项全绿仍不足

旧实现先发布槽位，再为去重集合分配节点/桶。小型受控 `bad_alloc` 会留下“物品已增加、成功键未记忆”的状态：空间足时重试可再发，空间紧张时重试反而被容量守卫拒绝。容量 dry-run 只证明一种业务拒绝路径；它不证明后续任何分配或响应准备都安全。

当前真实实现先准备计划和固定 Result，再完成成功记录的单元素 emplace，最后只做不抛出的槽位标量写入和结果复制。合同依赖单线程、非重入、标准 allocator、固定 noexcept 整数 hash/equality/结果；不是 CPU 原子指令或崩溃事务。原 `bool Add(..., std::string& outReason)` 改为固定 `Result`，字段为 code/requestId/itemId/requested/before/after；旧性能数据不绑定这个新接口。

成功键只绑定一个 `(itemId,count,首次结果)`：同参重放原快照，即使当前库存后来变了；异参（包括非法新参数）明确冲突。仅成功 Add 留记录，容量/参数拒绝不占键，释放空间后可用同键再试。Remove 保留本地按槽扣除的原语，不新加其幂等 API。当前类无 TTL/持久化，成功记录随对象生命周期保留并增长；DB 同事务与可靠日志先行是[主文](../../../知识/05-Gameplay与交互系统/背包装备与存档/05-背包道具完整链路.md)分开讨论、尚未执行的架构。

### 当前实际范围

Linux / GCC 14.2.0 / C++17，严格 `-Wall -Wextra -Werror -pedantic`：O0+NDEBUG、O2、O1+UBSan 非恢复各 2381 个显式检查零失败；保留七场景通过。六种小状态动态发现共 24 个实际分配点，逐点失败后按独立字面量核对全部槽位、成功记录和原 out；恢复后同键提交一次。数量是本工具链与场景观测，不代表任意标准库的分配序号或全部输入空间。

分配 hook 只观察独立 slots/out，成功 map 在方法返回/catch 后检查，避免重入其 emplace 中间态。固定 Result 用静态无抛出条件；外部 128 字节序列化失败是已提交但响应未知的另一个边界，不冒称原短串 `ok` 实际分配失败。

三个真实源码变体分别把记录放回槽位之后、略掉异参冲突、用当前数量重算原结果；各自显式 fail=13/7/3 且 exit1。编译失败或无关崩溃不算通过负控。driver 要求完整非空 RESULT、预定组/分配场景及匹配的 FAIL 标记，不能靠 exit0 判定成功。

Python -O + Bash + Linux PowerShell 7.6.6 的 282 个 runner 断言通过：中文/空格路径、首中尾编译/运行失败、拒覆盖/alias、真实写失败、坏报告/exit0假成功、双流非法UTF8/字面转义/CRLF/NUL逐字节往返。超时采用外部临时常规文件的有限快照，不等 pipe EOF；同组子进程终止、短命 setsid 逃逸持输出两类各五目标被分别测试。超时记录 capture_complete=false，不保证逃逸后代全部终止或未来输出已捕尽；快照非原子，Windows fallback 没有本轮实测。fixture 不运行其余四个模型基准，更不证明它们的业务语义已修好。

### 全部原记录与复算

[人读摘要与最终关键原输出](results/inventory_contract_20261004.txt)不是完整 raw。[完整 gzip JSONL 归档](results/inventory_contract_20261004.raw.jsonl.gz)保留 18 次尝试的 952 个原诊断文件（953 条记录，含一条头记录），包括失败与被替代的旧版本；只有 final-focused/final-selftest 对应本次冻结源码。旧 69 份历史结果未被覆盖。早期版本只有展示解码的地方保留原样，不补造当时未记录的字节证据。

- gzip SHA256：`a3284fb0007cbf0a7f08cde4525e25d0d23f65c8dca046752cde78f2401d7904`
- 解压 JSONL SHA256：`59aa03c26c652165d8a7e724676b416cefc7846d165899f6569ece4d81d341a3`
- 原诊断文件总字节 6,921,981；JSONL 9,412,706 字节；确定性 gzip 844,613 字节，mtime=0

在仓库根运行下面配方；它只验证，不解压写文件、不运行归档中的命令。每条 data_base64 是对应原诊断文件的精确字节，bytes/SHA 单列。去重、路径、长度、摘要与压缩截断检查均用显式异常，`-O` 不会移除；实际正负校验已覆盖损坏、截断、重复记录、错误长度/hash、危险路径和缺记录。

```bash
python3 -O - evidence/tests/gameplay-core/results/inventory_contract_20261004.raw.jsonl.gz <<'PY'
import base64, gzip, hashlib, json, sys
from pathlib import Path, PurePosixPath

GZIP_SHA256 = 'a3284fb0007cbf0a7f08cde4525e25d0d23f65c8dca046752cde78f2401d7904'
JSONL_SHA256 = '59aa03c26c652165d8a7e724676b416cefc7846d165899f6569ece4d81d341a3'
FILE_COUNT = 952

def require(condition, message):
    if not condition:
        raise ValueError(message)

def verify(path, gzip_sha256=GZIP_SHA256, jsonl_sha256=JSONL_SHA256, count=FILE_COUNT):
    packed = Path(path).read_bytes()
    require(hashlib.sha256(packed).hexdigest() == gzip_sha256, 'gzip SHA mismatch')
    data = gzip.decompress(packed)
    require(hashlib.sha256(data).hexdigest() == jsonl_sha256, 'JSONL SHA mismatch')
    require(data.endswith(b'\n'), 'missing final JSONL newline')
    rows = [json.loads(line) for line in data.decode('utf-8').splitlines()]
    require(len(rows) == count + 1, 'record count mismatch')
    require(rows[0].get('kind') == 'archive_header' and
            rows[0].get('format') == 'learning.inventory.evidence.v1' and
            rows[0].get('attempts') == 18 and
            rows[0].get('accepted_runs') == ['final-focused', 'final-selftest'], 'bad header')
    names = set()
    total = 0
    for row in rows[1:]:
        require(row.get('kind') == 'file', 'non-file record')
        name = row.get('path')
        require(isinstance(name, str) and name and '\\' not in name and '\0' not in name, 'bad path')
        q = PurePosixPath(name)
        require(not q.is_absolute() and '..' not in q.parts and name == q.as_posix() and name != '.', 'unsafe path')
        require(name not in names, 'duplicate record: ' + name)
        names.add(name)
        raw = base64.b64decode(row['data_base64'], validate=True)
        require(type(row['bytes']) is int and row['bytes'] >= 0 and len(raw) == row['bytes'], 'length mismatch: ' + name)
        require(hashlib.sha256(raw).hexdigest() == row['sha256'], 'file SHA mismatch: ' + name)
        total += len(raw)
    return {'files': len(names), 'original_file_bytes': total, 'jsonl_bytes': len(data)}

if __name__ == '__main__':
    require(len(sys.argv) == 2, 'usage: python [-O] verify.py ARCHIVE.raw.jsonl.gz')
    print(json.dumps(verify(sys.argv[1]), sort_keys=True))
PY
```

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
