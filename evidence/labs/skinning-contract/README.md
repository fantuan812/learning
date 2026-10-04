---
type: Evidence
title: "蒙皮坐标与权重契约数值实验"
description: "零依赖 LBS 教学模型与解析反例，不替代 UE/glTF 资产运行验证。"
status: stable
verified: []
maturity: L4
updated: 2026-10-02
sources:
  - title: "Khronos glTF 2.0 — Skins"
    resource: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#skins
  - title: "Khronos glTF Tutorial — Skins"
    resource: https://github.khronos.org/glTF-Tutorials/gltfTutorial/gltfTutorial_020_Skins.html
---

# 蒙皮坐标与权重契约数值实验

> 知识成熟度：L4，仅限本页实际运行的数学测试；主责知识见 [07-动画资产与骨骼基础](../../../知识/04-图形动画与物理仿真/动画求值与角色表现/07-动画资产与骨骼基础.md)。不是 UE 功能测试，不是 glTF 导入器。

## 问题与假设

为何骨骼旋转看似正确，网格仍会错位？权重和为 1 是否就能保证无形变错误？

这里固定齐次列向量、矩阵从右向左作用；矩阵以 Python 行列表存储。`inverse bind` 将绑定网格局部位置变到绑定骨骼局部，当前骨骼矩阵已累积父链。世界输出为 `Σ w_i G_i I_i p`；若下游还要乘网格 model，则 palette 先乘 `inverse(M)`。两种输出不得混用。

所有矩阵限定为有限的 4×4 仿射矩阵，模型使用 Python 双精度浮点；权重非负、有限、和为 1（绝对容差 `1e-9`）。默认姿态不被假定等于任意真实资产的绑定姿态。模型在构造的绑定姿态中计算 `I_i = inverse(B_i) M_bind`，并不读取或重建实际文件中的 inverseBindMatrices。

## 环境与运行方式

- 实测：2026-10-02，Linux x86_64、Python 3.12.14、Bash 5.2.37；仅标准库，无安装步骤
- 命令（仓库根）：`bash evidence/labs/skinning-contract/scripts/run_all.sh`
- 脚本分别执行 `python3 -B tests/test_skinning.py` 与 `python3 -B -O tests/test_skinning.py`
- 新输出写入忽略目录 `build/python_linux.txt`；不会自动覆盖已归档结果
- 可人工对照归档的输入/源码 SHA-256，确认比较的是同一模型
- 不同宿主的时间、测试耗时可能不同，验收看用例与坐标结果，不要求输出字节一致

## 文件与输入

| 路径 | 职责 |
| --- | --- |
| [src/skinning.py](src/skinning.py) | 仿射矩阵、父链、palette、位置 LBS 与输入拒绝 |
| [tests/test_skinning.py](tests/test_skinning.py) | 16 项 unittest，含解析结果与故意错误路径的对照 |
| [data/cases.json](data/cases.json) | 两骨骼关节/顶点、权重与手算期望值 |
| [scripts/run_all.sh](scripts/run_all.sh) | 记录环境与 SHA-256，默认及优化模式都执行测试 |
| [results/python_linux.txt](results/python_linux.txt) | 未改写的首次完整成功输出 |

核心输入：根骨骼为单位阵，子骨骼绑定在 `(0,1,0)`，顶点为 `(0,2,0)`；子骨骼绕自身 Z 轴转 90°，根/子各占一半。其他用例构造非单位绑定网格、父级旋转、palette 重排、12 影响、坏权重与退化矩阵；全部输入在测试源码中明确列出。

## 验证矩阵与指标

| 测试 | 预期与诊断目的 |
| --- | --- |
| 绑定姿态复原 | 单根与混合权重均还原原顶点 |
| 子骨骼绕绑定关节旋转 | 顶点为 `(-1,1,0)`；关节点 `(0,1,0)` 不动 |
| 两骨骼加权 | `(-0.5,1.5,0)` |
| 父链组合 | 父级旋转/平移必须影响子骨骼全局位置 |
| model 仅应用一次 | 正确世界坐标 `(9.5,1.5,0)`；错误双变换为 `(19.5,1.5,0)` |
| 非单位绑定网格 | `inverse(B_world) M_bind` 复原；漏 `M_bind` 则失败 |
| 漏 inverse bind / 次序颠倒 | 分别错误得到 `(-2,1,0)` / `(-2,0,0)` |
| palette 重排 | 同步重映射正确；沿用旧索引会绑错且不越界 |
| 超过四个影响 | 12 个等权平移平均得到 `x=5.5`，只证明数学定义不限 4 |
| 权重裁剪与归一化 | `(0.9,0,1)` 变成 `(1,0,0)`，形状不保持 |
| 正负旋转平均 | `+90°/-90°` 等权把 `(1,0,0)` 压到原点 |
| 坏权重拒绝 | 零和、和不为 1、负数、NaN、Inf 均拒绝 |
| 坏索引/长度/点拒绝 | 负数、越界、非整型、空/错长输入、非法齐次位置拒绝 |
| 坏层级/palette 长度拒绝 | 父骨骼未先出现、非法父索引、数量不一致拒绝 |
| 坏矩阵拒绝 | 奇异、非有限、错误形状、非仿射矩阵拒绝 |
| 共同世界变换不变性 | 6 个固定旋转角下，网格局部变形不变 |

指标是解析坐标误差、输入接受/拒绝与测试成功数，不是吞吐或 GPU 性能；数值比较采用小数点后 9 位。`unittest` 断言不会像语言级 `assert` 一样被 `python -O` 消除。

## 原始结果与结论

[归档输出](results/python_linux.txt)记录默认模式 **16/16**、优化模式 **16/16** 通过，并附四个输入文件 SHA-256。

这些结果支持三个局部结论：

1. 缺失 inverse bind、父链错误、乘法次序和重复 model 变换有可区分的数值反例，不能全部归因于坏权重
2. 合法索引与归一化权重不足以保证正确绑定：palette 对应关系、裁剪误差仍需验证
3. 正确 LBS 本身也可塌缩；不能把全部扭转伪影当成资产脏数据

## 局限与实践验收边界

- 未加载 glTF/FBX 或 UE 骨骼资产，未跑 UE 编辑器、Cook、C++ 编译、GPU、移动设备或 Windows；UE 影响数/LOD 结论来自主文引用的 Epic 官方资料，不能由本实验推证
- 不测试 glTF 多组 attribute 解码、字节量化、UE BoneMap/compact-pose 映射、实际 LOD 裁剪/重映射、节点启停、Root Motion 或重定向
- 仅位置 LBS，不含法线/切线、非均匀缩放法线变换、负缩放手性、morph、DQS、网格视觉质量和 SIMD/浮点跨平台一致性
- 求逆为教学 Gauss-Jordan，阈值是绝对值；未证明对病态矩阵或极端尺度的数值稳定性。真实实现应依数学库/引擎约定处理
- 修复实际资产后，仍需以真实绑定姿态和多组动作验收，分别检查各目标平台、LOD、渲染路径及极端关节姿态；本文不提供未运行的平台性能结论

## 关联阅读

- [主责正文 §2 与 §9](../../../知识/04-图形动画与物理仿真/动画求值与角色表现/07-动画资产与骨骼基础.md)：坐标空间推导、影响数边界及 LOD 分层
- [动画性能与预算分配](../../../知识/04-图形动画与物理仿真/动画求值与角色表现/04-动画性能与预算分配.md)：把动画求值和蒙皮成本分开测量
- [Evidence 总目录](../../README.md)
