---
type: Evidence
title: "Protobuf 新旧 schema：presence、未知字段与 ProtoJSON 实验"
description: "用官方 protoc 生成两版消息，在固定 Python runtime 上保存可复现的兼容性反例。"
status: stable
verified: []
maturity: L4
updated: 2026-10-02
sources:
  - id: presence
    title: "Application Note: Field Presence"
    resource: https://protobuf.dev/programming-guides/field_presence/
  - id: unknown-fields
    title: "Proto3: Unknown Fields"
    resource: https://protobuf.dev/programming-guides/proto3/#unknowns
  - id: protojson
    title: "ProtoJSON Format"
    resource: https://protobuf.dev/programming-guides/json/
  - id: protoc-release
    title: "Protocol Buffers v36.0"
    resource: https://github.com/protocolbuffers/protobuf/releases/tag/v36.0
  - id: python-release
    title: "protobuf 7.36.0 on PyPI"
    resource: https://pypi.org/project/protobuf/7.36.0/
---

# Protobuf 新旧 schema 实验

> 知识成熟度：L4，仅指本目录的 schema 演进测试；不是性能基准或生产认证。
> 实测日期：2026-10-02。Linux x86_64 / Debian 13 / Python 3.12.14 / protoc 36.0 / protobuf 7.36.0 / `python` backend。
> “新旧”指两版教学 schema，**不是两个历史 runtime 版本**。本实验没有使用 C++ protobuf runtime、upb backend、网络进程或 UE。

## 问题与假设

待验证的问题来自 [网络通信与协议设计](../../../游戏服务端/01-架构与网络/02-网络通信与协议设计.md)：解析成功是否足以证明更新语义与透传完整？

三条可被测试推翻的假设：

1. 显式零值经识别该字段但不跟踪 presence 的旧 schema 中转后，数值仍为零，但更新意图丢失。
2. 旧消息对象保留未知字段，修改已知字段或 `CopyFrom` 后仍可恢复；逐字段重建、主动丢弃或 JSON 中转会丢失。
3. 新 JSON 进入旧 schema 默认拒绝，忽略未知字段只解决解析失败，不能赋予旧业务新能力。

## 输入与文件

| 文件 | 责任 |
| --- | --- |
| [src/old.proto](src/old.proto)、[src/new.proto](src/new.proto) | 教学旧版 `int32 x=1`、新版 `optional int32 x=1`；两版都有 `sequence=2`，新版新增 `sprint=3` |
| [src/generated/old_pb2.py](src/generated/old_pb2.py)、[src/generated/new_pb2.py](src/generated/new_pb2.py) | protoc 36.0 原样生成的 Python 代码；每次运行重新生成并逐字比较 |
| [tests/test_evolution.py](tests/test_evolution.py) | 12 个 unittest，用真实消息 API 检查值、presence、重新解析和教学业务结果 |
| [scripts/run_all.sh](scripts/run_all.sh) | 显式依赖路径、版本/backend 门禁、两次运行、哈希与原始观察输出；不联网安装 |
| [data/toolchain.json](data/toolchain.json) | 重新核对过的官方发布元数据入口、下载 URL、SHA-256 |
| [results/python_linux.txt](results/python_linux.txt) | 本次原始输出，包括版本、源文件哈希、12+12 个测试结果与观察值 |

为让两份 descriptor 在同一 Python 进程共存，package 分别为 `learning.evolution.old/new`。普通消息的本次二进制测试依赖相同字段编号/类型，不包含包名；不能把这个技巧推广成 gRPC 服务名、`Any` type URL 或反射 API 的重命名方案。

## 运行方式

### 一次性准备隔离工具

需要 Python 3.10+、Bash、网络和 Linux x86_64。下面在仓库根运行，只下载并解压到指定目录，不使用 pip，不修改系统包或 shell PATH。若已有这些工具，直接跳到下一节。二进制下载与执行应遵循所在环境的安装审批规则。

```bash
export TOOLS="$PWD/.kb_work/protobuf-tools"
python3 - <<'PY'
import hashlib, json, os, pathlib, urllib.request, zipfile
root = pathlib.Path(os.environ["TOOLS"])
manifest = json.loads(pathlib.Path("evidence/labs/protobuf-evolution/data/toolchain.json").read_text())
root.mkdir(parents=True, exist_ok=True)
for key, folder in (("protoc", "protoc-36.0"), ("runtime", "python")):
    item = manifest[key]
    payload = urllib.request.urlopen(item["url"], timeout=60).read()
    if hashlib.sha256(payload).hexdigest() != item["sha256"]:
        raise SystemExit("SHA-256 mismatch: " + key)
    archive = root / item["url"].rsplit("/", 1)[1]
    archive.write_bytes(payload)
    with zipfile.ZipFile(archive) as package:
        package.extractall(root / folder)
(root / "protoc-36.0/bin/protoc").chmod(0o755)
PY
```

两份包分别来自官方 [GitHub release 元数据](https://api.github.com/repos/protocolbuffers/protobuf/releases/tags/v36.0) 与 [PyPI 版本元数据](https://pypi.org/pypi/protobuf/7.36.0/json)，本次实际下载后 SHA-256 均匹配。固定版本只是复现基线，不表示永久推荐或最新版本；更换工具链时应重新核对官方来源并重新生成、复测。

### 重新生成并测试

```bash
PROTOC="$TOOLS/protoc-36.0/bin/protoc" \
PROTOBUF_PYTHON_ROOT="$TOOLS/python" \
bash evidence/labs/protobuf-evolution/scripts/run_all.sh
```

默认结果写入被 Git 忽略的 `build/python_linux.txt`，不覆盖已提交证据。需要记录一轮新证据时显式指定 `RESULT_LOG`。每次测试都运行默认和 `-O` 两种 Python 模式，unittest 断言不会因优化模式消失。失败返回非零；生成代码与已提交文件不一致时在测试前退出。脚本对 `PYTHONPATH` 和 backend 的设置仅存在于子进程。

## 原始结果与解读

指标是状态与断言是否符合预期，不是吞吐或耗时。两个测试模式分别 **12/12 通过**；输出中耗时仅是测试框架附带值，不作性能结论。

| 测试编号 | 实际观察 | 含义 |
| --- | --- | --- |
| 01–02 | implicit `x=0` 编码为空；explicit 编码 `0800`，`HasField` 为真；`ClearField` 后变假 | 读到零不能证明发过零；未设置与清零是不同操作 |
| 03 | 目标值为 9，合并 implicit 零后仍为 9；explicit 零后为 0 | `MergeFrom` 的默认值语义影响 patch |
| 04 | 旧 writer 的 0 在新 reader 无 presence；±9 有；新增 `sprint=false` | 缺失新增能力须由业务明确降级 |
| 05 | `08001007` 经旧端变 `1007`，新端 `x=0` 但无 presence；原目标 99 保持不变 | 值相同，更新意图已经改变 |
| 06–07 | 改已知 sequence 后未知 sprint 仍为真；CopyFrom 保留，重建/DiscardUnknownFields 后为假 | 必须覆盖真实中转代码路径 |
| 08–09 | 旧对象转 JSON 只剩 `{"x":9}`；新 JSON→旧端默认 ParseError，忽略后 sprint 为假 | 容错解析不等于无损透传 |
| 10 | 新 schema 的 `{"x":0}` 保留 presence；`{"x":null}` 无 presence | 本例的 null 不表示将业务值清零 |
| 11 | `08001801` 经旧端变 `1801`：未知 sprint 保留，但已知 x 的 presence 丢失 | “保留未知字段”无法保护旧 schema 已认识字段的语义 |
| 12 | 教学旧服务走 1 单位、新服务冲刺 2 单位；显式能力集合为空时拒绝冲刺 | 最后一层是业务策略模型，不是 protobuf 自动协商 |

这里使用 deterministic 序列化让固定样例更容易检查，主要断言仍针对重新解析后的语义。不要将全部消息的字节相等或哈希相等当作跨语言/跨版本规范合同：[序列化并非 canonical](https://protobuf.dev/programming-guides/serialization-not-canonical/)。

## 结论、局限与下一步

工程验收应同时保存输入、最终字段值、presence、未知字段和业务结果。仅检查 Parse 成功、旧端认识的字段相等，都会漏掉本次反例。对于中转节点，先选择原始字节路由、消息对象修改或 JSON 转换，再分别写回归测试。

本次只覆盖 scalar `int32/uint32/bool`、普通消息、两个 schema、同一 Python runtime。未覆盖 C++ runtime/ABI/Arena、真正跨进程滚动发布、int64→int32 溢出、新枚举、oneof、嵌套消息、FieldMask、字段/枚举重命名、恶意输入/模糊测试、历史存档回滚和性能。尤其不能用生成器是 protoc 就声称 C++ runtime 已验证；C++ 生成代码/runtime 的匹配规则应另按 [官方版本合同](https://protobuf.dev/support/cross-version-runtime-guarantee/) 验证。

能力集合和步行/冲刺只是显式输入的教学模型，没有网络能力协商实现。项目迁移仍需按主文的未覆盖矩阵锁定生产语言、schema、runtime、降级策略和回滚数据再测试。

## 关联阅读

- [网络通信与协议设计](../../../游戏服务端/01-架构与网络/02-网络通信与协议设计.md)：主责原理、所有权与迁移验收
- [Evidence 索引](../../README.md)：实验与原始结果入口
- [字段存在性](https://protobuf.dev/programming-guides/field_presence/)、[未知字段](https://protobuf.dev/programming-guides/proto3/#unknowns)、[ProtoJSON](https://protobuf.dev/programming-guides/json/)：官方规则
