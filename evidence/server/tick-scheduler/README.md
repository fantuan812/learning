---
type: Evidence
title: "Evidence · tick-scheduler：单位、上限与相位守恒"
status: stable
verified: []
maturity: L0
updated: 2026-10-04
sources:
  - resource: https://eel.is/c%2B%2Bdraft/time.duration
    title: C++ duration count and period
  - resource: https://eel.is/c%2B%2Bdraft/time.clock.steady
    title: C++ steady_clock contract
---
# Tick策略实验：可执行的时间账本，不是CPU基准

> 旧[Windows原始结果](results/tick_scheduler_win_x64_msvc.txt)整字节保全，SHA-256 `7a8a5e6022f06840b4cf445889a0788a5e944849fbdfaeab2fc57a208b887051`。本次不重新生成MSVC历史记录，不把新模型结果倒填成原项目实测。
> 元数据L0保持既有Evidence标记，verified为空；下文逐项报告实际可执行合同，不能由本页局部测试推定关联正文整体L4或生产容量。

## 1. 为什么要重做这份实验

旧代码把`entities × unitCost × jitter`写进成本数组，却只用外部frameLoad推进accumulator。成本涨一亿倍时，ticks/drops仍不变；它没有模拟“工作成本→落后→追帧”的反馈。旧版“实体成本线性增加”是输入公式，不能称性能发现。

另外两个计量错误：cap=3在后置`++executed > max`判断下可以执行4次；catch只计执行多个Tick的外层轮数，不是额外Tick总数。清零accumulator又会丢掉不足一Tick的余量，drop事件数无法解释丢失时间。

本次选择更小但闭合的合同：只输入离散dt，计算有限Tick决策和完整时间分账。**没有真实CPU/睡眠/网络，也没有服务成本模型。** 若以后加入服务成本，必须真的进入模型时钟并另行验收。

## 2. 唯一实现与单位

- [tick_policy.hpp](src/tick_policy.hpp)：纯策略核心；只持有配置和分数相位
- [tick_scheduler.cpp](src/tick_scheduler.cpp)：功能、故障与固定场景驱动；不嵌第二套正常策略
- [主文完整例](../../../知识/07-网络与游戏服务端/运行调度与过载保护/01-ServerMainLoop与TickScheduler.md)：runner实际抽取、编译、运行

输入为`int64_t`非负纳秒，phase为`uint64_t`的ns×Hz credits，1e9 credits恰好一Tick。配置Hz/cap均1～1000，最大接纳dt为1ns～60s。cap=0、负/倒退elapsed和越界配置拒绝；先检查符号与范围，再在乘法/加法之前检查溢出，最后提交新相位。

```text
raw_ns = accepted_ns + clamped_ns
phase_before + accepted_ns * Hz
  = (executed + dropped_ticks) * 1e9 + phase_after
0 <= phase_after < 1e9
```

`executed`是模型选出的逻辑推进次数，不表示真实业务回调完成。只丢完整欠债Tick，保留余量；clamp丢失时间与drop分开。丢弃时长以`dropped_ticks / Hz`有理秒保存，不把30/60Hz分数周期再截成整数ns。JSON纳秒总量用十进制字符串避免大整数被浮点解析。

状态可按值复制为独立快照。此处没有链表迭代器/外部资源；不支持并发共享或真实回调回滚。频率固定；变更Hz的相位转换不是这个接口的一部分。

## 3. 三个读者可手算的场景

| 场景 | 输出合同 | 说明 |
| --- | --- | --- |
| 20Hz/cap1，125ms再25ms | 第一轮选1、丢1、留0.5Tick；第二轮选1、余量0 | 检验只丢整Tick |
| 30Hz/cap3，一次1秒 | 选3、丢27、余量0 | 上限约束一次调用的总Tick |
| 同配置，十次100ms | 共选30、丢0、余量0 | 分块改变执行机会，但总时间账仍守恒 |

CSV中的`fractional_drop`将表中125ms/25ms两步重复两次，摘要因此选择4Tick、丢2Tick；表格只展示一次两步的因果。

没有clamp/drop时，整段与分块的执行总数和相位应一致。有每次cap时，只能在接纳时间相同的条件下比较`executed+dropped`与相位。每次clamp还可能改变接纳总时间；不能错误地要求任意分块都等价。

## 4. 功能与负对照

正常构建有17个命名测试：20/30/60Hz单位、1ns阈值、零输入、cap、分数余量、计数分母、clamp、非法配置、负值且状态不变、INT64_MAX、溢出辅助函数、三种分块边界、独立值快照，以及10000步确定性随机守恒。

两种故障由明确的测试宏分别编译；不是生产选项，也不能输出正式场景：

- `TICK_POLICY_TEST_MUTANT_CAP_AFTER`复刻后置cap错误：恰好5个指定测试失败，12通过，退出1
- `TICK_POLICY_TEST_MUTANT_DROP_FRACTION`在丢完整债务时同时清零分数：恰好2个指定测试失败，15通过，退出1
- 两种故障版若请求`--scenarios`，必须在建文件前退出2；任意崩溃/任意非零不算负对照成功

Python runner另外有15项合同测试，包括已有目录保全、缺编译器、启动失败、超时/非零、缺/重复正文块、整数精度、缺行/重复行、错误相位/计数和错误失败原因。

18组1074行固定场景保留每个输入与输出，包括INT64_MAX原始dt和固定PRNG序列。独立Python Fraction按有理Tick单位复核每个数值，再检查守恒与全部fixture顺序；不是只看C++自己打印PASS。当前测试范围不等于一般证明，也不涉及现实任务执行。

## 5. 复跑与失败语义

依赖Python3标准库与支持C++17的GCC兼容编译器；不用第三方Python包。仓库根执行：

```bash
python3 -B evidence/server/tick-scheduler/scripts/run_policy.py \
  --output-dir /tmp/tick-policy-new-run --compiler g++ --timeout 120
```

目录必须尚不存在，原子创建。预检已有目录/缺编译器/旧结果hash错误时直接拒绝，不写结果；创建目录后失败保留已执行命令与FAIL状态。不能把缺工具、编译失败、超时、正文未运行、故障未按预期失败或缺场景标成通过。

构建产物在系统临时目录，结束后清理；新持久目录只有五文件：

| 文件 | 内容 |
| --- | --- |
| provenance.json | 系统/编译器/语言标准、源和受测正文hash、合同、未验证范围 |
| commands.json | 每条argv、cwd、UTC、stdout/stderr、退出码和超时标记 |
| scenarios.csv | 全部模型输入/输出、相位、clamp/drop分账 |
| summary.json | 逐场景模型总量与精确有理丢弃时长，没有CPU分位数 |
| validation.json | 正常/故障/正文/runner/独立参考验证结果 |

实际记录位于[2026-10-04结果](results/tick-policy-2026-10-04/provenance.json)，可读[命令](results/tick-policy-2026-10-04/commands.json)、[CSV](results/tick-policy-2026-10-04/scenarios.csv)、[摘要](results/tick-policy-2026-10-04/summary.json)、[验证](results/tick-policy-2026-10-04/validation.json)。命令elapsed_seconds是工具执行经过时间，不是被建模的Tick工作成本。

Windows保留[PowerShell入口](scripts/build_run.ps1)，要求显式新目录、Python及GCC兼容编译器，不写死VS路径，不安装工具。runner使用GCC兼容flags，明确拒绝cl；**本批本地没有执行Windows/MSVC**，不能沿用旧报告证明新实现通过MSVC。

```powershell
.\evidence\server\tick-scheduler\scripts\build_run.ps1 -OutputDirectory 'D:\Results\tick-policy-new-run'
```

## 6. 不重新运行程序，也能复核CSV

从仓库根用同一校验入口重算摘要与CSV可派生的校验字段；这不替代重新执行C++/正文/故障测试。证据校验使用显式条件抛错，不能使用会被`python -O`或`PYTHONOPTIMIZE`删除的assert：

```bash
python3 -B - <<'PY'
import csv, hashlib, importlib.util, json
from pathlib import Path
base = Path("evidence/server/tick-scheduler")
result = base / "results/tick-policy-2026-10-04"
spec = importlib.util.spec_from_file_location("tick_runner", base / "scripts/run_policy.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
provenance = json.loads((result / "provenance.json").read_text(encoding="utf-8"))
if hashlib.sha256((result / "scenarios.csv").read_bytes()).hexdigest() != provenance["scenarios_sha256"]:
    raise RuntimeError("CSV SHA-256 mismatch")
with (result / "scenarios.csv").open(newline="", encoding="utf-8") as source:
    reader = csv.DictReader(source)
    if reader.fieldnames != runner.FIELDS:
        raise RuntimeError("CSV header mismatch")
    summary, validation = runner.validate_rows(list(reader))
if summary != json.loads((result / "summary.json").read_text(encoding="utf-8")):
    raise RuntimeError("summary mismatch")
saved = json.loads((result / "validation.json").read_text(encoding="utf-8"))
if any(saved.get(key) != value for key, value in validation.items()):
    raise RuntimeError("CSV-derived validation mismatch")
print("PASS: exact policy records reproduce summary and CSV-derived validation")
PY
```

## 7. 边界与关联

这份实验只承诺列出的整数单位、前置cap、分数保留与有限输入合同。没有建立CPU容量、wake-up精度、固定P99、网络突发、线程安全Stop或UE整合结论。

- [ServerMainLoop与TickScheduler](../../../知识/07-网络与游戏服务端/运行调度与过载保护/01-ServerMainLoop与TickScheduler.md)：原理、六阶段职责、真实运行时缺口
- [AI与寻路时间预算](../../../知识/07-网络与游戏服务端/运行调度与过载保护/11-AI与寻路时间预算.md)：分片、队列、过期结果与端到端预算
- [旧原始输出](results/tick_scheduler_win_x64_msvc.txt)：历史过程材料，不与新场景拼成性能A/B
