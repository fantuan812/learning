---
type: Evidence
title: "伤害与属性结算可运行证据（修正器聚合 / 减免 / 护盾 / 过量 / DOT / 上限）"
description: "C++17单线程模型的阶段分账、数值拒绝、整数DOT与失败传播；保留历史原始结果，不提供当前性能基准。"
tags:
  - evidence
  - gameplay
  - damage
  - attribute
  - combat
status: stable
verified: []
maturity: L0
updated: 2026-10-04
---

# 伤害与属性结算局部合同证据

本目录用合成输入验证一个局部 C++17 模型。它不是 UE/GAS、技能服务、网络同步或生产战斗系统。模型与独立测试分离；本次不运行 CPU 基准。`evidence/` README 的 L0 是基础设施元数据约定，不把局部实验上推为关联全文的成熟度。

## 当前证据与历史证据

- 实现：[src/damage_pipeline.cpp](src/damage_pipeline.cpp)，没有内嵌测试/benchmark main
- 独立测试：[tests/damage_contract.cpp](tests/damage_contract.cpp)，常量期望、逐字段状态/RNG比较和独立整数事件时间轴；不靠assert，NDEBUG不能禁用
- 统一runner：[scripts/run_damage_contract.py](scripts/run_damage_contract.py)；[Bash](scripts/run_all.sh)与[PowerShell](scripts/build_run.ps1)只做参数/退出码转发
- 真实故障夹具及源码mutants：[scripts/test_runner_contract.py](scripts/test_runner_contract.py)，自身检查不靠Python assert，`-O`下仍执行
- 本次归档：[before](results/2026-10-04-damage-contract/before.txt)、[strict](results/2026-10-04-damage-contract/strict.txt)、[ubsan](results/2026-10-04-damage-contract/ubsan.txt)、[runner-contract](results/2026-10-04-damage-contract/runner-contract.txt)、[manifest](results/2026-10-04-damage-contract/run-manifest.json)

归档文本是带明确阶段边界的原始stdout/stderr转录，manifest记录每个流的原始长度、SHA256与字节范围，可提取核对。旧 [results/damage_pipeline.txt](results/damage_pipeline.txt) 原字节保留；旧15绿没有覆盖同实例DOT分块丢相位、HP40/100虚报toHp、NaN污染、极大护甲和runner失败假绿。before保留原15绿与新增反例红，不能把修订后语义倒填为旧实验已验证。

## 构建与复现

本轮实际使用 Linux x86_64、g++-14、Python标准库，Linux pwsh只验证wrapper。具体版本、源/测试SHA、命令、UTC与阶段退出码在manifest。没有安装依赖，没有读取真实服务或凭据。Windows/MinGW、UE、网络、多线程、跨平台浮点确定性和生产容量未验证。

从仓库根执行，`--out`的父目录须已存在，输出目录自身必须不存在且位于仓库外：

```bash
python3 -B evidence/tests/damage-core/scripts/run_damage_contract.py \
  --out /tmp/damage-contract-new-run --cxx g++-14 --mode strict
python3 -B evidence/tests/damage-core/scripts/run_damage_contract.py \
  --out /tmp/damage-contract-new-ubsan --cxx g++-14 --mode ubsan
bash evidence/tests/damage-core/scripts/run_all.sh \
  --out /tmp/damage-contract-new-bash --cxx g++-14 --mode all
python3 -O -B evidence/tests/damage-core/scripts/test_runner_contract.py \
  --out /tmp/damage-runner-new --cxx g++-14
```

```powershell
./evidence/tests/damage-core/scripts/build_run.ps1 -Out /tmp/damage-contract-new-pwsh -Gxx g++-14 -Python python3 -Mode strict
```

每次换一个新目录；不要删历史结果来让命令重跑。`--mode strict`运行O0+NDEBUG及O2，`ubsan`运行sanitizer，`all`运行三者。公共选项为 `-std=c++17 -Wall -Wextra -Werror -pedantic`，没有fast-math；UBSan追加 `-O1 -g -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all -fno-omit-frame-pointer`。旧“直接编译src为程序”的入口已改成独立tests入口，runner负责构建它。

故障测试若传 `--pwsh <可执行路径>`，会额外实跑Linux PowerShell；不传则manifest明确not-run。本轮使用的临时HOME/XDG隔离仅是本机执行约束，不是用户安装指南。

## 结算合同

`Settle(const Combatant&, Combatant&, const DamageSpec&, XorShift&)`先完整校验攻守快照与配置，计算临时结果和临时RNG，最后提交目标盾/HP/alive与RNG。返回status区分accepted、dead、immune、invalid、range；blocked兼容字段对所有未接受结果为true。无效输入优先于死/免疫短路。

| 项目 | 接受域/政策 |
| --- | --- |
| amount/type/K/cap | amount有限>0；type=0/1/2；K有限>0；cap有限≥0，0不限 |
| 属性 | base/modifier值有限、op合法；最后Override胜，Add/Mul各按源顺序；每次中间加乘可表示；attack≥0，bonus/taken≥-1 |
| 战斗状态 | 双方hpMax有限>0，0≤hp≤hpMax，alive等于hp>0；所有盾有限非负；armor有限（负值按0减伤） |
| 暴击 | chance有限[0,1]、倍率有限≥0；chance=0不消耗RNG，chance>0的合法命中消耗一次，包括chance=1或完全被抗性抵消 |
| 抗性 | 所有类型槽有限，有限越界仍clamp至[0,1] |
| 错误 | invalid/range不改变目标、源属性或RNG，不返回半成品阶段值；危险运算前做范围检查 |

负Add/减益不被全面禁止；只要最终因子合法即可。`(Override或base+ΣAdd)×ΠMul`是分组规则，不是任意列表重排不变：多Override次序会变结果，同类double加法重排也会变。两次同值请求是两次命中，模型没有网络去重ID。

### 字段的冻结时刻

`raw`为攻击/暴击/增伤后；`mitigated`为护甲/类型抗性/受伤加成后；`capped`为上限后；`absorbed`为分流给对应类型盾的名义量；`requestedHpDamage=capped-absorbed`；`toHp=HPbefore-HPafter`；`overkill=max(request-before,0)`。

raw1000、护甲100/K100、cap150、盾50，得到mitigated500、capped150、absorbed50、HP请求100。cap位置限制的是减伤后的总命中；前置cap或HP-only cap是其它政策，不是数学上必然失效。真伤仅跳护甲，仍有抗性、受伤加成、cap和对应盾。

HP40/命中100实际扣40且overkill60；盾30/HP40/命中100则分流30、请求70、实际40、过量30。HP1e20/请求1可能实际扣0；HP1/请求3×2^-54可能存储差为2^-52。`roundingResidual=(requestedHpDamage-toHp)-overkill`允许有符号。盾也可能舍入，`actualShieldLoss`与`shieldRoundingResidual=absorbed-actualShieldLoss`区分名义分流与存储盾差。这些式子仍是浮点运算，不保证任意输入逐位精确守恒，不新增最小伤害政策。

### 稳定护甲

正K下直接求剩余系数：A≥K时q=K/A后取q/(1+q)，A<K时q=A/K后取1/(1+q)。不先做 `1-A/(A+K)`，避免1e20护甲消减成0以及两个DBL_MAX相加溢出。极端比值自身下溢仍允许为0；模型按binary64阶段求值，未用高精度中间数挽救后续乘法。有限输入的中间溢出返回range，即使后面抗性为1也不能掩盖。

## DOT合同与兼容层

整数核心 `AdvanceDotUnits(vector<DotTicks>&, uint64_t dtUs)`：1单位=1微秒；interval>0，0≤phase<interval，过期时phase=0，perTick有限>0，type有效。每次保存相位，恰到期的完整跳计入，末尾不足整跳丢弃。dt=0不变、overshoot只消费剩余生命期。先规划整批，再统一提交；计数、原额乘法或总和超界时整批拒绝。

跳数uint64且不逐跳循环；2^31跳有实际用例。多个DOT的计数相加前检查，phase合并避免直接计算可能溢出的phase+active。计数/相位精确，原额为double并可能舍入。整数接口不能发现调用方已把负有符号数隐式变成uint64，调用方转换前必须校验。

`SecondsToUnits`按传入binary64数值的精确值×1e6，最近微秒、恰半向上；双limb精确积避免long double二次舍入。非有限/负秒拒绝，舍入值≥2^64拒绝，检查先于float→integer转换。旧 `Dot/AdvanceDots`只作薄适配：创建时量化并保存整数时钟，remainingSec仅供读取；镜像不一致拒绝，但不保证检测任意协同篡改。旧int*输出超界抛overflow_error，坏输入抛invalid_argument，目标和输出指针不变，null指针允许。

分块不变性仅属于同一整数时间轴：一次1.2微秒量化为1，0.4微秒分三次各为0，两者不等价。`.25×4`、`.6+.4`与`.3/.1`等原反例保留；不要添加epsilon把边界前一单位误当到期。

批DOT只报告原额与跳数，未生成每跳事件队列、未接入逐跳Settle。原两DOT第一秒16、全程62不证明类型盾/逐跳cap；两次100且每跳cap150应合计200，先合并再cap则150。

## 断言、负向控制与runner故障

当前作者合同为185项，严格O0+NDEBUG、O2、UBSan同一套输入；原准备门禁28项通过薄适配保留其期望。数量表示本套有界用例，不是全域形式证明。

| 测试族 | 区分的错误 |
| --- | --- |
| raw/mitigated/capped与HP/盾存储差 | 不能只让最终HP正确却日志虚报；正负残差都覆盖 |
| 护甲0/100/900/1e9/1e20/DBL_MAX及subnormal | 区分可避免数值不稳定与真实表示下溢 |
| invalid/range与后续合法命中 | 非法类型、非有限、坏状态、中间溢出不污染目标或随机流 |
| cap、真伤、三个盾池、死/免疫、重复调用 | 保留玩法并明确不包含网络幂等 |
| lastOverride、类别交错、同类浮点重排 | 不虚构普遍顺序无关性 |
| 整数DOT事件时间轴/宽计数/整批拒绝/秒转换 | 不丢相位，不产生float→int或signed-count UB |
| 固定5000命中序列逐字段比较 | 同构建可重复；不以量化hash代替完整结果 |

四个实际源码mutants分别丢相位、虚报实际HP、恢复不稳定护甲、把cap移到护甲前，均真实编译运行且被失败断言捕获，未伪造FAIL输出。独立审查也可增加不同变异，但不能代替这些原反例。

runner要求全新独占输出，拒绝现存空目录、旧结果、现存/损坏符号链接与仓库内输出；构建和raw都在新目录。编译失败不运行旧binary；缺编译器/Python、编译/运行非零、信号、超时、缺binary、缺失/坏/重复RESULT、PASS计数不符、FAIL文本与输出创建/写失败均向上返回非零，不打印COMPLETED。manifest中每阶段有命令、返回码、时间、stdout/stderr哈希；输入源码/测试SHA识别工作树内容。不存在的结果、超时或失败都不能计为通过。

## 历史性能数字的正确用法

旧Windows/MinGW raw的500×1000次样本报告批均摊P50=14.2ns、P95=16.3ns、P99=16.8ns、mean=13.0ns与吞吐77161685。这些数字保留为历史观察；每批时长除1000得到的是批均值分位数，不能改称单次P99，也不能据此证明1万次线上结算耗时0.14ms或“公式通常不是瓶颈”。旧量化重放哈希也只保留历史身份，不要求纠正toHp语义后仍相同。

20Hz对应50ms Tick。真实预算要分别测完整调用、快照/分配、AOE、日志、广播与负载分布。本轮没有新性能结论，没有优化器审计，也没有生产容量结论。

## 关联知识

- [11-伤害与属性结算完整链路](../../../知识/05-Gameplay与交互系统/技能战斗与属性结算/11-伤害与属性结算完整链路.md)：设计、分项数表与策划对账
- [03-技能释放完整链路](../../../知识/05-Gameplay与交互系统/技能战斗与属性结算/03-技能释放完整链路.md)
- [04-Buff系统完整链路](../../../知识/05-Gameplay与交互系统/技能战斗与属性结算/04-Buff系统完整链路.md)
- [05-背包道具完整链路](../../../知识/05-Gameplay与交互系统/背包装备与存档/05-背包道具完整链路.md)
- [gameplay-core](../gameplay-core/README.md)、[战斗结算与验证](../../../知识/05-Gameplay与交互系统/技能战斗与属性结算/06-战斗结算与验证.md)
- [GameplayAbilitySystem能力系统](../../../知识/05-Gameplay与交互系统/技能战斗与属性结算/01-GameplayAbilitySystem能力系统.md)、[GAS能力系统源码](../../../知识/05-Gameplay与交互系统/技能战斗与属性结算/05-GAS能力系统源码.md)：未作为本轮私有引擎源码核验

## 提取历史复现夹具与28项薄适配

manifest的 `bundles[].entries[]` 给每个原始流的 `offset/bytes/sha256`；标有 `extract_as` 的项同时携带可复现夹具原字节。以下只提取到全新仓库外目录，先验证哈希再写文件，避免依赖归档命令里的本机绝对路径：

```bash
python3 -B - /absolute/path/to/repo/evidence/tests/damage-core/results/2026-10-04-damage-contract /tmp/damage-extract-new <<'PY'
import hashlib, json, pathlib, sys
archive, out = map(pathlib.Path, sys.argv[1:])
out.mkdir()
manifest = json.loads((archive / 'run-manifest.json').read_text())
for bundle in manifest['bundles']:
    data = (archive / bundle['file']).read_bytes()
    for entry in bundle['entries']:
        if 'extract_as' not in entry:
            continue
        relative = pathlib.Path(entry['extract_as'])
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('unsafe extraction path')
        raw = data[entry['offset']:entry['offset'] + entry['bytes']]
        if hashlib.sha256(raw).hexdigest() != entry['sha256']:
            raise ValueError('payload hash mismatch')
        target = out / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as stream:
            stream.write(raw)
PY
python3 -B /tmp/damage-extract-new/before-src/reproduce.py \
  --output-dir /tmp/damage-extract-new/before-src/new-run --cxx g++-14
```

最后一条命令复现旧问题，预期原15绿、候选28中13红、旧runner隔离副本返回0但记录程序23；该复现脚本成功表示缺陷被复现，不表示旧模型正确，也不会调用旧benchmark。所有旧runner写入仅在新提取目录内的复制夹具。

复跑原28期望与新模型（将REPO设为实际仓库绝对路径）：

```bash
REPO=/absolute/path/to/repo
OUT=/tmp/damage-extract-new

g++-14 -std=c++17 -O0 -DNDEBUG -Wall -Wextra -Werror -pedantic \
  "-DDAMAGE_SOURCE=\"$OUT/after/acceptance_adapter.cpp\"" \
  "-DDAMAGE_MODEL_SOURCE=\"$REPO/evidence/tests/damage-core/src/damage_pipeline.cpp\"" \
  "$OUT/before-src/acceptance_gate.cpp" -o "$OUT/accepted28"
"$OUT/accepted28"
```

预期 `RESULT pass=28 fail=0`。适配仅引入命名空间、原测试所需构造助手与旧回放调用，没有删改28条期望；它是必要回归，不替代185项新合同。运行当前模型的优先入口仍是统一runner。
