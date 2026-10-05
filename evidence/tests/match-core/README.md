---
type: Evidence
title: "匹配到对局可运行证据（匹配池窗口 / 房间生命周期 / 分队平衡）"
description: "保存三个匹配教学程序的历史结果；补强固定快照小队同局、未来准入与可复现人口，以及单服务房间全员确认和名额所有权，明确分队与真实性能待修边界。"
tags:
  - evidence
  - matchmaking
  - mmr
  - room-lifecycle
  - team-balance
  - gameplay
status: stable
verified: []
maturity: L0
updated: 2026-10-05
---

# 匹配到对局可运行证据

> 当前修订（2026-10-05）：补强 MMR 固定快照准入、可复现人口与 Room 单服务内存合同。旧三程序的 **55 条断言（17 + 31 + 7）** 是历史测试清单，不证明 party 完整、公平人口对照、所有退出序列或生产结算。旧原始输出保持原字节；新结果另存，不覆盖。`evidence/` README 的 maturity 仍为 L0，不用本次局部测试替整套匹配系统升级。

本目录包含三个独立教学程序，并没有把匹配池、房间和分队接成真实服务管线。当前已修固定快照内拆队/提前准入，以及“同一房间重复退出会扣掉其他房名额”和“有人拒绝后仍可 Ready”的既定合同缺陷。新政策、反例和执行边界见下节；后半部分保留历史输入/数值，并在不成立的旧解释旁标明撤回或限制。

## 当前 MMR 准入合同：候选玩家与完整小队不是同一个集合

### 为什么原17项全过仍然会拆队

候选扫描回答“在这次预算和窗口内看见了谁”，完整小队名册回答“这个单位有哪些成员”。原 TryForm 只收候选中与 seed 同 party 的玩家，再按个人补足人数并 resize 截尾。于是非 seed 小队可能只进去一人，五人小队也可能被截成四人。旧 M6 又把内部数组 index 当稳定玩家 ID；即使补对这行检查，只测恰好一整局仍发现不了窗口、容量与候选截断。

修复要从整个固定人口快照建立非零 partyId 的名册，而不是从 waiting 或 cand 反推名册。候选阶段仍可只看到一部分成员，但组局阶段只能整组选中或整组跳过。partyId=0 是单排身份，不表示所有单排玩家属于一个大组。

### 本模型的三项选择

1. **快照内同局原子性**：同一非零 party 的所有快照成员进入同一个局，或全部等待。有成员尚未到 queuedAt、已 matched、与 anchor 不兼容、不在候选中或剩余容量放不下时，不能拿其中一部分。组太大就等待，不能截尾。调用者必须提供完整且在本次计算期间不变的快照；没有期望人数和party版本，程序不能发现根本没送进来的成员，也不保证并发入队完整性。
2. **保留 anchor 双方窗口**：每名入选者与 seed 的分差同时落在两人的窗口内。tick0 的1000 anchor 可以同时带950和1050（各自窗口50），整局spread100仍符合这一政策。它不是所有玩家两两兼容，也不把party平均分当每个成员的替代。想采用all-pair或必须同队，要另立产品合同。
3. **到点才获得资格**：queuedAt>tick 的玩家不参加本次匹配；等于tick时可参加。若小队中一人未来才入队，全组等待。把负等待时间clamp到0只是在算窗口，不能代替准入检查。

### 三种搜索路径共用的不变量

| 场景 | 必须成立 | 不保证的事 |
| --- | --- | --- |
| 线性、分桶、排序三路径 | 同一快照规则、恰好2×teamSize人、没有重复或提前入场、完整小队同局 | 三策略选同一组人、同样局数、最大吞吐、无饥饿 |
| scanLimit截断候选 | 任何小队不能因截断而拆分；可能因此本轮无解 | 访问次数或CPU时间的硬上限 |
| 输出与状态 | Match.ids解析为输入数组index，再映射稳定ID；只在成功成局后标记相应成员 | 真实队伍划分、跨进程所有权或持久化事务 |
| 分差诊断 | spread从实际入选值计算；relaxed记录spread>baseWindow | relaxed必然意味着等候窗口已实际放宽 |
| 超时 | 窗口有上限，离群玩家可能一直不能成局 | maxWaitTicks自动触发出队、机器人或真实定时器 |

三路径保留各自候选枚举和贪心选择。因此正确性先按不变量验收；不能用“局数相近”代替不拆队，也不能因为更严格的all-pair少匹配就称修复更安全。

### 可复算输入先于性能结论

RNG各次调用按独立语句固定到字段；同seed还需要相同构造规则、配置、时间与策略版本。基准输入先生成一次，再给三个策略各一份独立副本，防止共享推进中的RNG或matched状态让比较换了人口。本批只用小人口检查字节/字段一致和副本隔离，不执行旧大基准，不发布新的提速结论。

原seed2026人口的普通与极端两组必须按生成时的群体身份拆开记录匹配人数、未匹配人数与等待分位；不能从一个总max或极端人数上界反推尾部原因。结果若不支持“极端一定更慢”，应保留这个结果，而不是换人口追认原断言。


### 输入适用域与实际验证

调用者提供完整且固定的人口快照，稳定 ID 唯一；queuedAt/tick 是非负 int64_t，tick 单调推进，人口数不超过 INT_MAX。teamSize 在1到INT_MAX/2之间、scanLimit为正；MMR有限，窗口和增长率有限且非负，baseWindow≤maxWindow，运算结果可表示。分桶宽度为正，桶号和跨度能由int表示。源码没有将任意非法配置转为受控错误；不承诺分配异常/OOM回滚或并发原子性。

源码：[mmr_pool.cpp](src/mmr_pool.cpp)；聚焦回归：[mmr_contract.cpp](tests/mmr_contract.cpp)；隔离入口：[run_mmr_contract.py](scripts/run_mmr_contract.py)。入口仅借用 [Inventory 的捕获和路径 helper](../gameplay-core/scripts/run_inventory_contract.py)，不调用旧 main、旧wrapper或大基准。实际环境为 Linux x86_64、GCC14.2.0、Python3.12.14、pwsh7.6.6；没有第二种C++编译器、Windows/MSVC模型或真实匹配服务的执行证据。

| 实际执行范围 | 结果与解释 |
| --- | --- |
| 最终模型，O0+NDEBUG / O2 / UBSan | 每模式88组1198项通过；原17项仍17/0。覆盖超容量、非seed小队、候选截断/预算不足、未来全员/队友、双向窗口、完整状态和人口副本 |
| 作者16个真实错误变体 | 各自编译成功，以原生exit1命中指定语义用例；编译失败、超时或畸形协议不算语义检出 |
| 独立语义oracle | 23种场景，每编译模式471条记录；普通Python与-O各7098项通过，不调用被测窗口/准入函数生成期望 |
| 独立人口与群体oracle | 三编译模式、两种Python模式各1737项通过，固定黄金人口与群体身份；不与1198相加当覆盖率 |
| 独立12个错误变体 | 实际编译和observer成功，独立checker均exit1命中目标；含只破坏线性/分桶/排序单一路径的三个变体 |
| 同输入重复性 | 六次独立进程重跑逐字相同，三个GCC编译模式相同；不外推为跨平台逐位保证 |
| 最终runner普通Python / -O | 各148项实际CLI检查通过，含NaN/Infinity、重复JSON键、mode类型/一致性、畸形/缺项/假绿、编译/执行失败、超时、raw写入/关闭失败、中文空格路径与拒覆盖 |
| CI块 | 最终Linux PowerShell块exit0；合成第一条Python返回23时整块返回23，第二条不运行。Windows任务跳过本MMR模型步骤 |

原seed2026人口保持1200名普通玩家和20名极端玩家。按输入构造身份分组，到tick200结束时分别匹配1200/1200和20/20，未匹配均为0。普通组等待p50/p95/p99/max为0/0/0/36；极端组为0/1/1/1。这反驳了“原总最大等待全由极端玩家造成”的归因；它只是这份人口和策略的观察，不证明极端玩家一般更快。另一个3000对1000例子仍证明窗口封顶后可能永远无解。

以下输出目录必须尚不存在且位于仓库之外；第二条使用另一个新目录。缺编译器/helper、编译失败、超时、缺证据、日志I/O失败都非零退出：

```bash
python3 -B evidence/tests/match-core/scripts/run_mmr_contract.py --cxx g++-14 --mode all --negative-controls --timeout-seconds 120 --output-dir /tmp/mmr-contract-new-run
python3 -O -B evidence/tests/match-core/scripts/run_mmr_contract.py --cxx g++-14 --self-test --timeout-seconds 120 --output-dir /tmp/mmr-runner-new-run
```

commands.jsonl记录实际argv、cwd、退出/超时和stdout/stderr原字节，原流有base64、长度和SHA；主合同命令另有UTC、构建或二进制指纹，provenance或self-test-summary记录相应源身份。记录flush/fsync且成功close后才允许PASS。严格JSON拒绝重复键与非有限数值，固定清单核对CHECK/CASE/OBS/RESULT和mode，不从输出自报数量推导完整性。捕获仍是有界普通文件快照，不能保证收集或终止逃离进程组的任意后代，也不声称抵御恶意程序完整伪造成功协议。

### MMR原始执行记录与无损复算

[文本摘要](results/mmr_contract_20261005.txt)不是完整raw；按[完整技术原流分片清单](results/mmr_contract_20261005.raw/manifest.json)拼回同一归档，其中保留各逻辑文件的原始字节。file/alias只去重存储，不合并执行次数。

`mmr_contract_20261005.raw.jsonl.gz` 使用确定性 gzip（mtime=0、空 filename）保存 `learning.mmr.evidence.v2` JSONL。共 2,702 个原始逻辑路径：1,265 个直接 file、1,437 个 alias，恢复原始字节合计 332,129,008；唯一内容 264,453,662 bytes。直接 file 的 `encoding` 仅允许 `utf8` 或 `base64`，前者严格 UTF-8 编码回原 bytes，后者严格解码；alias 只指向此前的直接 file，不能成链。每个逻辑路径均带原长度及 SHA-256。

归档大小 29,818,161 bytes，解压 JSONL 流 271,200,124 bytes。归档 SHA-256：`9da3c581cf2a0588338fd63cf9e05c7462effac9ea2531f67a3149b1a0180737`；JSONL SHA-256：`eb3361252f8ac42ba3e704c847b0a49accd3567d82a9757d973c3a55f679c3bd`。归档包括历版源码、独立 fixtures/oracles、真实初次编译失败、旧 runner 接受 NaN/重复 key/mode 不一致的三个假通过、严格 JSON 修复后的 normal/-O 与 CI-equivalent-v2，以及最终独立 78 CLI/2,712 断言原流；7 份真实日志写失败的截断 commands 也按原 bytes 保留。编译 ELF、bytecode、符号链接、仓库门禁和归档自身未纳入。

先按清单恢复同一 gzip。15 个分片是原 29,818,161 bytes 的顺序切片，前 14 片各 2,097,152 bytes，第 15 片 458,033 bytes；没有重压、改写 JSONL 或改变 gzip SHA。清单使用 `learning.mmr.gzip-slices.v1`，SHA-256 为 `7c4f26561bcbcc2efdefa02554da0d28399ae72edb8978620c5621896406b70c`。

在仓库根执行以下 Linux/POSIX 配方，选择仓库外、父目录已经存在的全新输出文件名。配方拒绝路径中的符号链接及任何已有输出（文件、目录、悬空链接均拒绝），在验证清单、顺序和各片后使用排他创建，不覆盖原文件。写入或 close 失败可能留下不完整的新文件，不能当作成功；只有显示 `ASSEMBLED_VERIFIED` 才继续下一步。普通模式和 `-O` 必须使用不同的新输出文件名。

```sh
MMR_ARCHIVE=/tmp/mmr_contract_20261005.restored.raw.jsonl.gz
python -B - evidence/tests/match-core/results/mmr_contract_20261005.raw . "$MMR_ARCHIVE" <<'PY'
#!/usr/bin/env python3
"""Reassemble the pinned MMR gzip into a NEW file outside the repository.

Linux/POSIX: no symbolic links, no extraction, no decompression, no overwrite.
All pieces are validated before exclusive output creation. A write/close failure
may leave a partial new file, but never prints the success result.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys

MANIFEST_SHA256 = '7c4f26561bcbcc2efdefa02554da0d28399ae72edb8978620c5621896406b70c'
ARCHIVE_BYTES = 29818161
ARCHIVE_SHA256 = '9da3c581cf2a0588338fd63cf9e05c7462effac9ea2531f67a3149b1a0180737'
ARCHIVE_NAME = 'mmr_contract_20261005.raw.jsonl.gz'

def require(condition, message):
    if not condition:
        raise ValueError(message)

def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate JSON key')
        result[key] = value
    return result

def reject_constant(value):
    raise ValueError('nonstandard JSON number: ' + value)

def absolute(path):
    path = Path(path)
    require('..' not in path.parts, 'parent traversal is not permitted')
    return path.absolute()

def open_directory(path):
    require(hasattr(os, 'O_NOFOLLOW') and hasattr(os, 'O_DIRECTORY'), 'requires POSIX no-follow directory support')
    fd = os.open('/', os.O_RDONLY | os.O_DIRECTORY)
    try:
        for part in path.parts[1:]:
            child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
            os.close(fd); fd = child
        return fd
    except BaseException:
        os.close(fd)
        raise

def read_regular(directory_fd, name, limit):
    fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory_fd)
    try:
        require(stat.S_ISREG(os.fstat(fd).st_mode), 'input must be a regular file')
        require(os.fstat(fd).st_size <= limit, 'input exceeds expected size')
        chunks = []; size = 0
        while size <= limit:
            block = os.read(fd, min(1024 * 1024, limit + 1 - size))
            if not block: break
            chunks.append(block); size += len(block)
        require(size <= limit, 'input exceeds expected size')
        return b''.join(chunks)
    finally:
        os.close(fd)

def assemble(parts_directory, repository_root, output_path,
             manifest_sha256=MANIFEST_SHA256,
             archive_identity=(ARCHIVE_BYTES, ARCHIVE_SHA256), part_count=15, part_bytes=2097152):
    parts_directory = absolute(parts_directory)
    repository_root = absolute(repository_root)
    output_path = absolute(output_path)
    require(not output_path.is_relative_to(repository_root), 'output must be outside repository')
    require(output_path.name not in ('', '.', '..'), 'invalid output filename')
    fd = open_directory(repository_root); os.close(fd)
    source_fd = open_directory(parts_directory)
    payloads = []; overall = hashlib.sha256(); total = 0
    try:
        encoded = read_regular(source_fd, 'manifest.json', 65536)
        require(hashlib.sha256(encoded).hexdigest() == manifest_sha256, 'manifest SHA mismatch')
        m = json.loads(encoded.decode('utf-8'), object_pairs_hook=unique_object, parse_constant=reject_constant)
        require(isinstance(m, dict) and set(m) == {'format', 'archive_name', 'archive_bytes', 'archive_sha256',
                                                'part_bytes', 'part_count', 'parts'}, 'invalid manifest schema')
        require(m['format'] == 'learning.mmr.gzip-slices.v1' and m['archive_name'] == ARCHIVE_NAME, 'invalid manifest format/name')
        require(all(type(m[k]) is int and m[k] > 0 for k in ('archive_bytes', 'part_bytes', 'part_count')), 'invalid manifest integer')
        require((m['archive_bytes'], m['archive_sha256']) == archive_identity, 'archive identity mismatch')
        require(m['part_count'] == part_count and m['part_bytes'] == part_bytes and
                (m['archive_bytes'] + part_bytes - 1) // part_bytes == part_count, 'invalid slice geometry')
        require(isinstance(m['parts'], list) and len(m['parts']) == part_count, 'part count mismatch')
        names = set()
        for index, row in enumerate(m['parts'], 1):
            require(isinstance(row, dict) and set(row) == {'name', 'bytes', 'sha256'}, 'invalid part schema')
            name = row['name']
            require(isinstance(name, str) and re.fullmatch(r'part-[0-9]{3}\.bin', name), 'unsafe part filename')
            require(name not in names, 'duplicate part filename')
            names.add(name)
            require(name == f'part-{index:03d}.bin', 'part order mismatch')
            length = min(part_bytes, m['archive_bytes'] - (index - 1) * part_bytes)
            require(type(row['bytes']) is int and row['bytes'] == length, 'part length metadata mismatch')
            require(isinstance(row['sha256'], str) and re.fullmatch('[0-9a-f]{64}', row['sha256']), 'invalid part SHA')
            raw = read_regular(source_fd, name, length)
            require(len(raw) == length, 'part length mismatch')
            require(hashlib.sha256(raw).hexdigest() == row['sha256'], 'part SHA mismatch')
            payloads.append(raw); overall.update(raw); total += len(raw)
        require((total, overall.hexdigest()) == archive_identity, 'whole archive SHA/length mismatch')
    finally:
        os.close(source_fd)
    destination_fd = open_directory(output_path.parent)
    try:
        fd = os.open(output_path.name, os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                     0o600, dir_fd=destination_fd)
        try:
            identity = os.fstat(fd)
            for raw in payloads:
                require(os.write(fd, raw) == len(raw), 'short output write')
            os.fsync(fd)
            require(os.fstat(fd).st_size == total, 'written output length mismatch')
            os.lseek(fd, 0, os.SEEK_SET); written = hashlib.sha256()
            while block := os.read(fd, 1024 * 1024): written.update(block)
            require(written.hexdigest() == archive_identity[1], 'written output SHA mismatch')
            linked = os.stat(output_path.name, dir_fd=destination_fd, follow_symlinks=False)
            require(stat.S_ISREG(linked.st_mode) and (linked.st_dev, linked.st_ino) ==
                    (identity.st_dev, identity.st_ino), 'output path identity changed')
        finally:
            os.close(fd)
    finally:
        os.close(destination_fd)
    return {'status': 'ASSEMBLED_VERIFIED', 'bytes': total, 'sha256': overall.hexdigest(), 'parts': part_count}

if __name__ == '__main__':
    require(len(sys.argv) == 4, 'usage: python [-O] assemble-slices.py PARTS_DIR REPO_ROOT NEW_EXTERNAL_OUTPUT')
    print(json.dumps(assemble(*sys.argv[1:]), sort_keys=True))
PY
```

仅在上一步成功后执行以下原归档核验配方，读取 `$MMR_ARCHIVE`，不再写文件，不执行归档中的脚本。两步命令都可将 `python -B -` 改为 `python -B -O -`；所有检查均不依赖 assert。

```sh
python -B - "$MMR_ARCHIVE" <<'PY'
#!/usr/bin/env python3
"""Strict read-only verification; payloads are never executed or extracted.

Each direct file supplies original bytes as strict UTF-8 or canonical base64.
Aliases refer only to an earlier direct file with the same length and SHA-256.
The optional observer receives original bytes for every logical path, allowing
source-byte comparison without extracting archive paths to the filesystem.
"""
import base64
import gzip
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import sys
import unicodedata

GZIP_SHA256 = '9da3c581cf2a0588338fd63cf9e05c7462effac9ea2531f67a3149b1a0180737'
JSONL_SHA256 = 'eb3361252f8ac42ba3e704c847b0a49accd3567d82a9757d973c3a55f679c3bd'
COUNTS = (2702, 1265, 1437, 332129008, 264453662)

def require(condition, message):
    if not condition:
        raise ValueError(message)

def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate JSON key')
        result[key] = value
    return result

def reject_constant(value):
    raise ValueError('nonstandard JSON number: ' + value)

def verify(path, gzip_sha256=GZIP_SHA256, jsonl_sha256=JSONL_SHA256, counts=COUNTS, observer=None):
    packed = Path(path).read_bytes()
    require(hashlib.sha256(packed).hexdigest() == gzip_sha256, 'gzip SHA mismatch')
    require(packed[:8] == b'\x1f\x8b\x08\x00\x00\x00\x00\x00', 'gzip header must have zero mtime and no filename')
    seen = set(); portable_seen = set(); direct = {}; contents = set(); originals = {}
    total_bytes = stored_bytes = aliases = 0
    last_name = None; header = None; digest = hashlib.sha256()
    reserved = {'CON', 'PRN', 'AUX', 'NUL'} | {f'{p}{n}' for p in ('COM', 'LPT') for n in range(1, 10)}
    fields = ('logical_files', 'file_records', 'alias_records', 'original_bytes', 'stored_original_bytes')
    with gzip.GzipFile(fileobj=io.BytesIO(packed), mode='rb') as stream:
        for number, line in enumerate(stream):
            digest.update(line)
            require(line.endswith(b'\n'), 'missing final newline')
            row = json.loads(line.decode('utf-8'), object_pairs_hook=unique_object,
                             parse_constant=reject_constant)
            if number == 0:
                require(isinstance(row, dict) and set(row) == {'kind', 'format', *fields} and
                        row['kind'] == 'archive_header' and row['format'] == 'learning.mmr.evidence.v2', 'bad header')
                require(all(type(row[k]) is int and row[k] >= 0 for k in fields), 'invalid header count')
                header = row
                continue
            require(isinstance(row, dict) and row.get('kind') in ('file', 'alias'), 'unsupported record kind')
            required = {'kind', 'path', 'bytes', 'sha256'} | ({'encoding', 'data'} if row['kind'] == 'file' else {'target'})
            require(set(row) == required, 'unsupported record shape')
            name = row['path']
            require(isinstance(name, str) and name and name != '.', 'bad path')
            q = PurePosixPath(name)
            require(not q.is_absolute() and '..' not in q.parts and name == q.as_posix(), 'unsafe path')
            require(unicodedata.normalize('NFC', name) == name, 'non-NFC path')
            for part in q.parts:
                require(not any(c in part for c in '\\<>:"|?*') and
                        not any(ord(c) < 32 or ord(c) == 127 for c in part) and
                        not part.endswith((' ', '.')) and
                        part.split('.')[0].upper() not in reserved, 'nonportable path')
            require(name not in seen, 'duplicate path')
            require(name.casefold() not in portable_seen, 'case-colliding path')
            require(last_name is None or last_name < name, 'unsorted paths')
            seen.add(name); portable_seen.add(name.casefold()); last_name = name
            require(type(row['bytes']) is int and row['bytes'] >= 0, 'invalid length')
            require(isinstance(row['sha256'], str) and re.fullmatch('[0-9a-f]{64}', row['sha256']), 'invalid SHA field')
            identity = (row['bytes'], row['sha256'])
            if row['kind'] == 'file':
                require(isinstance(row['encoding'], str) and row['encoding'] in ('utf8', 'base64'), 'unsupported codec')
                require(isinstance(row['data'], str), 'invalid data type')
                if row['encoding'] == 'utf8':
                    raw = row['data'].encode('utf-8', errors='strict')
                else:
                    raw = base64.b64decode(row['data'], validate=True)
                    require(base64.b64encode(raw).decode('ascii') == row['data'], 'noncanonical base64')
                require(len(raw) == row['bytes'], 'length mismatch')
                require(hashlib.sha256(raw).hexdigest() == row['sha256'], 'record SHA mismatch')
                require(identity not in contents, 'duplicate content must be alias')
                contents.add(identity); direct[name] = identity; stored_bytes += len(raw)
                if observer is not None:
                    originals[name] = raw
            else:
                target = row['target']
                require(isinstance(target, str) and target in direct, 'alias target must be an earlier direct file')
                require(identity == direct[target], 'alias identity mismatch')
                aliases += 1
                if observer is not None:
                    raw = originals[target]
            if observer is not None:
                observer(name, raw)
            total_bytes += row['bytes']
    require(header is not None, 'missing header')
    require(digest.hexdigest() == jsonl_sha256, 'JSONL SHA mismatch')
    actual = (len(seen), len(direct), aliases, total_bytes, stored_bytes)
    require(actual == tuple(header[k] for k in fields) == counts, 'archive counts mismatch')
    return dict(zip(fields, actual))

if __name__ == '__main__':
    require(len(sys.argv) == 2, 'usage: python [-O] verify-lossless.py ARCHIVE.raw.jsonl.gz')
    print(json.dumps(verify(sys.argv[1]), sort_keys=True))
PY
```

内存边界：命令先整读约 28.4 MiB 压缩包，再流式解析约 258.6 MiB 的 JSONL，不把完整解压流放入内存，也不保留约 252.2 MiB 的全部唯一文件 payload；直接 file 验证后仅保留长度/SHA-256 给后续 alias 校验。实际 Linux 峰值 RSS 为 normal 87,408 KiB（约 85.4 MiB）、`-O` 97,596 KiB（约 95.3 MiB），峰值还包含当前 JSON 字符串、解码 bytes 与解析器开销，会随 Python/平台而变，建议至少 256 MiB 可用内存。另行逐原路径比较的 observer 测试会保留唯一 payload，需要额外约 252.2 MiB，不能套用这里的 RSS。

实际验证：同一严格实现的 100/100 小 fixture 正负测试在 normal/`-O` 下通过；畸形 fixture 重新绑定自己的外层 hash，确实触发内部 schema、codec、alias、路径、长度和 SHA 检查。全包另在 normal/`-O` 各恢复并逐字节比较 2,702 个冻结原路径通过。以相同 gzip 参数重新压缩整个 JSONL，压缩流的长度和 SHA-256 也完全一致。


旧79份结果和282份书籍/日志/附件原件保持原字节。100项小fixture正负测试在普通Python/-O下检查与本配方相同的内部协议，覆盖codec/schema、重复键、非有限数、路径、别名、长度/hash/计数及损坏gzip；最终整包另在两模式逐路径与冻结原文件完整字节回比。逻辑文件数不是测试执行数，归档自身验算与仓库维护门禁不递归入包。



本次恢复后另做分片包装验证：normal/`-O` 均实际恢复相同的 29,818,161 gzip bytes，并运行上方未改变的原归档配方；新小 fixture 重新绑定清单 hash 后验证内部拒绝条件，包含缺片、乱序、重复、越界路径、坏片、已有输出和实际写失败。它们是本次新测试，不是丢失旧门禁日志的重建。拼接阶段先保留约 28.4 MiB 分片 bytes，输出仅在全片校验通过后排他创建；后续原配方的内存边界仍见上文。


## 当前 Room 合同：成员决定与整房资源是两件事

### 先看两个旧测试没问的问题

旧程序的 31 条断言可以全部通过，但以下输入仍破坏原本承诺：

1. A 房三人、B 房一人都 Ready，各占一个名额。A 的第一人掉线，应释放 A 的一份；A 的第二人再掉线，B 仍 Ready，池中就必须还留一份。旧代码每遇到原本 Accepted 的成员就减全局 `used`，第二次会扣掉 B 的名额。`used > 0` 只说明池里还有人持有资源，不能回答“这份是不是 A 的”。
2. 三人房间执行 Confirm(p1) → Decline(p2) → Confirm(p3)。旧 `AllDecided()` 只排除 WaitConfirm，因此两人接受、一人拒绝也被当成可以 Ready。没有人还在等待，与所有人同意，是不同谓词。

旧 R9 只看单房结算后 `used == 0`，漏掉了误扣其他房；R30 的混合序列对每房只走一个终止分支；R31 只检查最后一次容量上界，不能证明每步守恒。新回归同时观察 A、B 的完整状态，并独立写出期望持有者集合，在每个关键操作后核对它与资源计数的关系。

### 本教学模型选择的生命周期政策

以下是这次明确选定的合同，不是所有游戏都必须采用的产品规则：

- **D1：成员集合失效**。首次成功的拒绝或开局前掉线，让原成员集合在该房间整个生命周期中失效。房间回到/保留 Matched，待显式调用 Timeout 且严格超过原确认截止时间才关闭；不能靠剩余玩家再次 Confirm 复活为 Ready。其他产品可以取消或另建 generation 重组，但本模型不实现。
- **D1 的回队边界**：不自动把幸存者全送回队列。显式退出的那个成员按原规则回队一次；超时时，尚未确认者回队，已接受的幸存者变为 Dropped，但不自动回队。
- **D2：赛中 Drop**。InProgress 的 Drop 明确拒绝，完整状态和名额不变；赛中结束由 Settle 释放。真实断线、重连和替补政策不在此模型中。
- **D3：房间 ID 不复用**。`CreateRoom` 保持返回 `Room&` 的 API。任何已存在 ID，包括 Closed，都会在写入前抛 `std::invalid_argument`，同参数也不当作成功重试。调用者必须处理该拒绝；这不等于实现了 generation、持久化幂等或一般分配异常回滚。

### 为什么必须记录房间自己的持有事实

每房单独记录“当前持有一份名额”和“原成员集合是否已失效”。成功 Acquire 才建立持有事实；释放必须消费该房尚存的持有事实。成员 Accepted 的历史记录不因第一次释放立即消失，因此不能拿人数推导所有权，也不能仅用全局计数是否大于零代替它。

Ready/Start 同时需要有效成员集合、每个成员 Accepted、该房确实持有名额。单独检查 Ready 枚举值是弱于这个合同的。直接构造无所有权的 Ready 状态测试 Start 守卫，只证明局部防御检查；这种注入状态不代表正常公开事件一定可达。

| 事件 | 成功条件与状态效果 | 重复/拒绝边界 |
| --- | --- | --- |
| CreateRoom | 新 ID 建立 Matched 和原始确认截止时刻 | 已存在 ID 抛异常，旧房、池和队列均保留 |
| Confirm | 有效集合中的待确认成员在 `now <= deadline` 接受；首次成功占一份，所有人接受才 Ready | 有效活房的已接受成员重复确认保留原幂等行为；失效集合、已退出成员和 Closed 拒绝 |
| Decline | 开局前待确认成员拒绝，单次回队；成员集合失效，只释放自己仍持有的一份 | 已接受成员不得反悔；跨 Drop/Decline 已退出成员不能再次回队 |
| Drop | 开局前符合条件成员掉线，单次回队；成员集合失效，只释放自己的一份 | 重复退出和 InProgress 拒绝，不得消耗其他房名额 |
| Timeout | 严格 `now > deadline` 才关闭；按上述成员政策回队并释放自己的持有份额 | 截止时间恰好相等不超时；Closed/InProgress 不处理 |
| Start | Ready 且所有成员 Accepted、集合有效、拥有名额才进入 InProgress | 守卫失败时不写状态 |
| Settle | 当前进程中推进结算 revision 并关闭，消耗自己的一份 | 再次调用返回同 revision；没有跨进程/崩溃后的恰好一次保证 |
| Backfill | 仅 InProgress，人数未满、ID 未重复 | 没有 MMR 输入或检查，不承诺 party 或公平策略 |

时间是调用者传入的离散 tick。只有显式调用 Confirm/Timeout 才执行相应时间条件；Start 没有 now 参数，本模型没有自动定时器，也不证明真实确认网络延迟或准点超时调度。

这里的名额池是单线程进程内模型，不是 DS 租约。调用者提供本服务拥有的 Room 引用，不能外部任意改写状态；成员 ID 非空且唯一；tick/window 非负、`now + confirmWindow` 可由 int64_t 表示，pool.capacity 非负，计数与容器人数不超相应 int/int64_t 范围，标准库分配异常不在本批原子性保证内。没有真实 DS、Redis、数据库、并发确认、租约到期或 fencing token 的执行证据。

### 实际验证与复现

源码：[room_fsm.cpp](src/room_fsm.cpp)；完整状态回归：[room_contract.cpp](tests/room_contract.cpp)；聚焦入口：[run_room_contract.py](scripts/run_room_contract.py)。新入口只读复用 [Inventory 的捕获/路径 helper](../gameplay-core/scripts/run_inventory_contract.py)，不调用其模型 driver、旧五目标入口或 self-test，不执行 MMR/分队基准。

2026-10-05 实际环境为 Linux x86_64、GCC14.2.0、Python3.12.14；同一 CI PowerShell 块在 Linux pwsh7.6.6 实际运行。没有 Windows/MSVC Room 模型、真实网络/DS/Redis/数据库的运行证据。准备期旧缺陷观察和修复复核的 UTC 时间分别保存在命令流；文件名中的 `20261004` 是批次标识，不代表所有运行发生在那一天。

| 实际执行范围 | 结果与边界 |
| --- | --- |
| 最终源，O0+NDEBUG / O2 / UBSan | 各12组305检查通过；原R1–R31继续31/0。新测试比较已建模全部字段、顺序日志/回队列表与独立持有者集合，原始输出是CHECK/CASE记录，不冒称序列化完整状态账本 |
| 另写的独立期望值 | 合法事件oracle三模式各104/0；谓词/注入状态防御另列12/0，不与305相加声称覆盖率 |
| 9个隔离错误版本 | 真实编译后各以exit1命中指定新回归，旧31却全部仍通过。AllDecided与Start简化守卫的命中含局部防御；其余检查针对具体生命周期错误；不是9个独立生产事故 |
| 变体的适用域 | 单独AllDecided回退在D1保护下，独立合法事件oracle仍104/0；局部防御7/5失败。明确同时回退D1确认守卫与AllDecided的两处变体，合法事件oracle95/9失败，不能伪装成单点命中 |
| runner普通 Python / `python -O` | 最终各115项：真正的编译/执行失败、超时、畸形/重复/假绿报告、EFBIG/关闭FD的EBADF、日志路径碰撞、中文空格路径和拒覆盖；原字节由base64/长度/SHA保留 |
| CI执行块 | 最终Linux块实际exit0；合成首Python进程exit23时整个块返回23，不调用下一条self-test。Windows任务不会执行这个Linux-only模型步骤 |

容量重试专门走真实资源事件：B先占满唯一名额，A确认失败且完整状态回滚；B开局并结算释放后，同一个A再确认成功。保留capacity0→1例，但不拿配置修改代替释放后重试。

以下目录必须尚不存在，且在仓库之外；缺编译器、缺helper、超时、报告缺项和I/O失败都非零退出。选择适当的外部路径，第二条用另一个新目录：

```bash
python3 -B evidence/tests/match-core/scripts/run_room_contract.py --cxx g++-14 --mode all --negative-controls --timeout-seconds 120 --output-dir /tmp/room-contract-new-run
python3 -O -B evidence/tests/match-core/scripts/run_room_contract.py --self-test --timeout-seconds 120 --output-dir /tmp/room-runner-new-run
```

`commands.jsonl` 是每次执行的一份权威命令/原流，摘要仅引用record number；每条记录写完后flush/fsync，成功close在PASS之前。记录实际argv、cwd、UTC、exit/timeout、编译单元/模型/执行runner/helper指纹及二进制身份。捕获继承helper的“有界普通文件快照”限制，不能保证收集或终止自行逃离进程组的任意后代；不把恶意程序伪造完整成功协议排除在外。

### 原始执行记录与无损复算

[文本摘要](results/room_contract_20261004.txt) **不是完整raw**；[完整模型/CLI/CI技术归档](results/room_contract_20261004.raw.jsonl.gz)是确定性gzip（mtime=0、空原文件名），解压为JSONL：1条header和1704条file记录，合计15,994,847个原始文件字节。文件数不是执行次数，合成`.exe`成员是实际运行过的脚本源码，不是编译二进制。没有alias记录；每份逻辑文件都独立保留原字节、长度、SHA-256。

- gzip：3,984,932 bytes，SHA-256 `682285b7c6b7110db2d96ebf672ebcf59a2cc6f62d6a6a398e37d73ad64ba5ff`
- 解压JSONL：21,690,531 bytes，SHA-256 `371818a9efd8c7daffb76e023b7bd479f1cd4cbedaeaf1910a66a11a483ac846`
- 旧77份结果、282保护原件及18份其他模型/helper未改。包括旧房间测试全部通过时，独立基线oracle仍51/50、exit1的真实失败；50是检查失败数，含连带后果，不是50个独立缺陷
- 初版18条`*-old31-*`记录的附加model hash误指主源，实际argv/TU hash始终指正确变体；原流保留，在`author/room-author-verification/technical-corrections.json`逐record追加纠正。当前runner已修；早期113项与先报PASS后close的版本不冒充最终115项版本
- I/O负控造成的部分/截断命令流也按原字节保留，不把它们当成功执行。归档范围是本模型/CLI/CI原始技术材料；归档校验自身和仓库维护门禁不会递归打包进自身

以下配方不解包写文件，也不用会被`-O`移除的assert；验证整包、JSONL、每个成员、路径唯一性与精确数量。该代码已在普通/-O下实际做42次配方正负验算（2个正例、40个预期拒绝），包含坏包/截尾、重复JSON键与路径、错误长度/hash、越界路径和非法记录：

```bash
python3 -O - evidence/tests/match-core/results/room_contract_20261004.raw.jsonl.gz <<'PY'
import base64, gzip, hashlib, json, sys
from pathlib import Path, PurePosixPath

GZIP_SHA256 = '682285b7c6b7110db2d96ebf672ebcf59a2cc6f62d6a6a398e37d73ad64ba5ff'
JSONL_SHA256 = '371818a9efd8c7daffb76e023b7bd479f1cd4cbedaeaf1910a66a11a483ac846'
COUNTS = (1704, 15994847)

def require(condition, message):
    if not condition:
        raise ValueError(message)

def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate JSON key')
        result[key] = value
    return result

def verify(path, gzip_sha256=GZIP_SHA256, jsonl_sha256=JSONL_SHA256):
    packed = Path(path).read_bytes()
    require(hashlib.sha256(packed).hexdigest() == gzip_sha256, 'gzip SHA mismatch')
    data = gzip.decompress(packed)
    require(hashlib.sha256(data).hexdigest() == jsonl_sha256, 'JSONL SHA mismatch')
    require(data.endswith(b'\n'), 'missing final newline')
    rows = [json.loads(line, object_pairs_hook=unique_object) for line in data.decode('utf-8').splitlines()]
    require(rows and isinstance(rows[0], dict), 'missing header')
    header = rows[0]
    require(set(header) == {'kind', 'format', 'created_utc', 'file_records', 'original_bytes'} and
            header['kind'] == 'archive_header' and header['format'] == 'learning.room.evidence.v1', 'bad header')
    seen = set(); total_bytes = 0
    for row in rows[1:]:
        require(isinstance(row, dict) and set(row) == {'kind', 'path', 'bytes', 'sha256', 'data_base64'} and
                row['kind'] == 'file', 'unsupported record shape')
        name = row['path']
        require(isinstance(name, str) and name and '\\' not in name and '\0' not in name, 'bad path')
        q = PurePosixPath(name)
        require(not q.is_absolute() and '..' not in q.parts and name == q.as_posix() and
                name != '.' and all(':' not in part for part in q.parts), 'unsafe path')
        require(name not in seen, 'duplicate path')
        seen.add(name)
        require(type(row['bytes']) is int and row['bytes'] >= 0, 'invalid length')
        raw = base64.b64decode(row['data_base64'], validate=True)
        require(len(raw) == row['bytes'], 'length mismatch')
        require(hashlib.sha256(raw).hexdigest() == row['sha256'], 'record SHA mismatch')
        total_bytes += len(raw)
    actual = (len(seen), total_bytes)
    require(type(header['file_records']) is int and type(header['original_bytes']) is int, 'invalid header count')
    require(actual == (header['file_records'], header['original_bytes']) == COUNTS, 'archive counts mismatch')
    return {'file_records': actual[0], 'original_bytes': actual[1]}

if __name__ == '__main__':
    require(len(sys.argv) == 2, 'usage: python [-O] verify.py ARCHIVE.raw.jsonl.gz')
    print(json.dumps(verify(sys.argv[1]), sort_keys=True))
PY
```


## 历史三程序记录与待修边界

以下环境、输入、输出记录属于旧报告。当前MMR与Room功能合同以上文为准；分队实现及历史性能仍未复核，新的功能回归不能为旧速度比例背书。

## 问题

1. 等待时间放宽匹配窗口，这个放宽有上限吗？放宽到上限还匹配不上怎么办？
2. 一个等了很久的玩家和一个刚入队的玩家，可接受分差应该按谁的窗口算？
3. 小队会不会被拆到两个对局？
4. 同一套人口输入，匹配结果可复现吗（出问题能不能回放）？
5. 极端 MMR 玩家的队列时间到底有多长？他们和普通玩家是同一个问题吗？
6. 房间确认阶段：重复确认、拒绝、掉线、超时，各自的名额怎么处理？会不会泄漏？
7. 结算重复请求会不会结算两次？
8. "每队至少一个坦克和治疗"这个硬约束，会让平衡度变差多少？
9. 匹配器的 CPU 成本由什么决定——池多大，还是别的？

## 假设

- **准入窗口取双方交集**（`diff ≤ min(window_a, window_b)`）：只看等待方的窗口，会让刚入队的玩家被拖进不公平对局。
- **窗口放宽必须有硬上限**：否则"等久了质量就无限下降"，等于把问题推给玩家。
- **放宽上限之外还需要独立的超时出口**：机器人填充 / 明确提示 / 特殊队列，因为放宽永远覆盖不了极端离群值。
- **一个房间只占一个名额**。本次实现用房间自己的持有事实决定释放；开局前退出、超时和结算的具体政策见当前合同。InProgress Drop 拒绝，不因成员掉线自动释放。
- **角色约束是硬约束**：不可满足时必须显式失败，而不是产出一个"看起来平衡但阵容残缺"的阵容。

## 环境

下表是旧报告记录的 Windows 环境，本次没有重跑 Windows 模型；当前 MMR/Room 的 Linux/GCC14 执行单列于上方。

| 项目 | 值 |
| --- | --- |
| 主机 | Windows（MINGW64_NT-10.0-26200，x86_64，16 逻辑核） |
| 工具链 | MSYS2 MinGW-w64 `g++` 16.1.0，`-std=c++17 -O2 -Wall` |
| 依赖 | 仅 C++17 标准库 |
| 未使用 | 真实匹配队列/Redis/DB、真实玩家分布、线上压测 |

## 运行方式

以下为历史三目标入口，会写已有 `results/`，**不是当前 MMR/Room 的复现命令**。原 shell wrapper 已用合成 exit7 程序复现最终仍 exit0的问题，待独立修复；PowerShell wrapper本次未运行，不能据此推定同样失败。请使用上方新 runner 的仓库外、全新输出目录。

```powershell
& (Join-Path $RepoRoot 'evidence/tests/match-core/scripts/build_run.ps1')
```

```bash
bash evidence/tests/match-core/scripts/run_all.sh
```

保留这些历史命令供识别旧结果来源；本次未调用它们，也未重跑 MMR/分队基准。

## 输入

- `mmr_pool`：teamSize=5；baseWindow=50、growthPerTick=4、maxWindow=600、maxWaitTicks=300、scanLimit=200。
- `room_fsm`：名额池容量按各用例设定；确认窗口 30 tick。
- `team_balance`：5v5；角色约束为"每队 ≥1 坦克、≥1 治疗"。
- 基准人口：MMR 1400±250（跨度 500），池 10 000 / 100 000，两种工况（刚入队 window=50 / 放宽 window≈450）。

## 指标与原始结果

**历史断言输出：`mmr_pool` pass=17 fail=0；`room_fsm` pass=31 fail=0；`team_balance` pass=7 fail=0**。新独立反例表明旧测试存在盲区；这三个数字不作为当前全合同通过标记。

窗口与公平性：

```text
window(t) = min(600, 50 + t * 4)         t=0 -> 50, t=10 -> 90, t=1000 -> 600（封顶）
diff=200 的两人：等待方窗口 450 > 200，但新入队方窗口 50 < 200 -> 不匹配
（若只看等待方窗口就会匹配 —— 这就是"交集规则"要防的 bug）
```

队列时间与公平性：

```text
[queue] wait_ticks p50=0 p95=0 p99=1 max=36        （1200 人常规人口 + 20 个极端高分）
matchmaker avg spread=15 vs random=333             （同人口下随机配对的跨度是 22 倍）
```

极端玩家与超时出口：

```text
3000 MMR 玩家 + 9 个 1000 MMR 玩家：t=0 不成局；t=300（窗口已达上限 600）仍不成局
-> 放宽机制永远覆盖不了 diff=2000 的离群值，必须靠独立的等待上限出口
```

房间生命周期：

```text
成功路径转移：Matched -> Allocating -> Ready -> InProgress
确认 4 人 -> allocCount=1（整房只占 1 个名额）
结算两次 -> revision=1 settles=1（第二次幂等返回同一版本）
拒绝 1 人 -> 释放名额 + 房间回退到 Matched + 该玩家回队列 1 次
确认超时 -> 房间 Closed + 名额释放 + 未确认者全部回队列
混乱混合序列 20 个房间：池内占用 == 存活的 Ready 房间数（5 == 5，无泄漏）
```

分队平衡：

```text
avg |mmr diff|：蛇形+交换 = 36   vs   随机分队 = 419
角色约束 200 局全部满足"每队 ≥1 坦克 + ≥1 治疗"
[balance] avg diff with role constraint=37.8 without=17.6 (cost=20.2)
```

匹配器成本（pool / 工况 / 三种实现）：

```text
pool=10000   window=50     linear=19896us  bucketed=11469us(1.73x)  sorted=5624us(3.54x)
pool=10000   window~=450   linear=13192us  bucketed=8945us (1.47x)  sorted=10492us(1.26x)
pool=100000  window=50     linear=635304us bucketed=179314us(3.54x) sorted=129255us(4.92x)
pool=100000  window~=450   linear=568587us bucketed=790981us(0.72x) sorted=2039803us(0.28x)
```

原始输出：[mmr_pool.txt](results/mmr_pool.txt) ｜ [room_fsm.txt](results/room_fsm.txt) ｜ [team_balance.txt](results/team_balance.txt)

## 结论

以下逐项区分仍有价值的设计原则、旧观察与本次已撤回的推论；新 MMR/Room 结论以上方完整合同为准。

1. **放宽窗口必须同时具备"上限"和"独立出口"**。窗口 `min(600, 50 + 4t)` 在 t=138 就封顶；封顶后一个 3000 MMR 的玩家即使等到 t=300 也匹配不上（实测 `diff=2000 > 600`）。这说明**"等得久就一定能匹配到"是错的**，必须另有等待上限出口（机器人填充 / 明确提示 / 特殊队列），否则会静默地永久卡在队列里——玩家看到的是"一直在匹配中"。
2. **可接受分差必须取双方窗口的交集**。用等待方的窗口做判据时，一个等了 100 tick 的玩家（窗口 450）会把刚入队、窗口只有 50 的新玩家拖进 200 分差的对局。本证据用一组对照断言把这个 bug 显式地"演示"出来（M4 正确、M5 反例成立），而不是只声称正确。
3. **旧小队完整性证明已撤回，当前按固定快照同局合同修复**。原 M6 的 partyTogether 检查把内部数组 index 当稳定玩家 ID；M7 只核恰好一局六人，这项检查仍有价值。二者没有覆盖非 seed party、候选窗口与容量边界。已复现三实现只选中同 party 的一部分成员。party 同局、party 同队、anchor↔candidate 双方窗口与 all-pair 公平是不同合同；当前选择快照party同局、anchor双方窗口和到点准入；同队与all-pair不在合同内。
4. **同 seed、同 build 的重复结果仍有价值**（M8）；M9 是不同 seed 人口产生不同结果的对照，不是重复性验证。两者都不是跨编译器逐位保证。原人口构造把两个 RNG 调用放在同一实参列表，没有固定参数求值顺序；当前已分语句固定消费顺序并让三策略复用同一人口的独立副本。本次未执行第二种 C++ 编译器。
5. **“离群玩家是长队列的全部来源”的旧归因已撤回**。原总分位数字不提供普通/极端两组的等待归因，旧M14上界检查也不足以支持这个结论；当前保持原人口并按构造身份分组补证，普通组max36、极端组max1，完整分位见上文。独立的3000对1000分差例子仍说明窗口有上限后需要另一种等待出口，但不能拿另一个输入解释此人口的尾部。
6. **房间应只消费自己仍持有的一份名额**。旧 R30 的单终止分支序列与最终数值不能覆盖跨成员/跨退出重入；本次新增两房逐步独立状态预期和资源事实，修复全员确认、失效集合与重复释放。具体回队、重复 Create、赛中 Drop 和超时边界以上方 D1–D3 为准。
7. **37.8 与17.6 是那次启发式输出的历史均值，不是“约束必然让该算法结果变差20.2”的定理**。可行域收紧对最优值的关系，不能直接套到两次不同局部搜索的结果。角色人口生成器还存在请求人数与实际人数不符的问题，分队实现和独立最优值对照待另批修复。
8. **分队修复不能要求"一次交换就满足全部约束"**：当角色全挤在对面（一队缺坦克又缺治疗）时，任何单次交换都补不齐两个缺口。本实现第一版正是这么写的，导致"能匹配但阵容残缺"；改为**按缺口逐步贪心**后 200 局全部满足。
9. **旧“索引收益/反噬”和100k队列不可行的普遍结论暂不采纳**。上方四组时间数字及原raw仍保留，但三种方法使用推进中的RNG，人口没有固定为同一份；`scanLimit` 计接受候选数，不是所有访问次数或时间预算。窗口大小可能改变工作量，仍需在相同输入上记录正确性、扫描工作和真实耗时，不能由这些旧比例宣布某队列规模必然不可行。

## 局限

- **不是真实匹配系统**：没有 Redis/DB 队列、没有多进程分片、没有客户端确认的真实网络往返；房间状态机是单线程模型，未覆盖并发确认的竞态。
- **MMR 模型是简化版**：没有段位/赛季重置、没有连败补偿、没有新手保护、没有位置偏好与"避免重复同队"这类社交约束。
- **分队只覆盖一种角色模型**：实际项目可能是"位置 + 英雄池 + 语音意愿"的多约束组合，且多为软约束加权而非硬约束。
- **历史基准是纯 CPU 单线程**：不含序列化、锁竞争、跨进程通信；输入对照未固定，不能从0.57 s/tick推导100k队列普遍不可行。
- **未覆盖**：拒绝惩罚与信用分、组队 MMR 加权、跨区匹配、匹配取消的竞态、机器人填充的质量标准。
- 历史单机单线程数值的绝对值与比例都依赖输入和测量条件；原raw是档案，当前MMR/Room功能测试没有重新验证这些性能比例。

## 关联知识文档

- 系统实战/06-匹配到对局完整链路（规划中，见 [系统实战 README](../../../00_Index/学习路线/端到端案例.md)；本证据的主要使用者）
- [游戏服务端/03-业务系统设计/04-匹配与房间系统](../../../知识/07-网络与游戏服务端/会话身份与在线服务/04-匹配与房间系统.md)
- [游戏服务端/06-世界模拟与运行时/09-动态分线与负载均衡](../../../知识/07-网络与游戏服务端/世界权威与故障恢复/09-动态分线与负载均衡.md)（房间分配之后的分线问题）
- [系统实战/01-角色进入游戏完整链路](../../../知识/07-网络与游戏服务端/会话身份与在线服务/01-角色进入游戏完整链路.md)（匹配成功之后的入场与租约）
- [evidence/tests/entry-core](../../tests/entry-core/README.md)（票据、Session、DS租约和JIP是独立串行教学合同；JIP已补stream/entity generation与连续目标版本的局部正反例，未做真实网络/DS端到端集成）
