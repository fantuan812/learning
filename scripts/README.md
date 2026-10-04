---
type: Index
title: "知识库校验与工具"
status: stable
verified: []
maturity: L0
updated: 2026-10-04
---

# 知识库校验与工具

## 现行门禁

- [get_kb_markdown.ps1](get_kb_markdown.ps1)：统一Git可见Markdown范围
- [check_okf.ps1](check_okf.ps1)：当前OKF子集Changed/Audit/Strict检查
- [check_repo.ps1](check_repo.ps1)：内容、结构、相对链接与证据边界
- [check_architecture.ps1](check_architecture.ps1)：目录登记、别名与快照
- [rebuild_manifest.ps1](rebuild_manifest.ps1)：机械重建文件快照
- [test_architecture.ps1](test_architecture.ps1)、[test_kb_scope.ps1](test_kb_scope.ps1)、[test_okf_changed.ps1](test_okf_changed.ps1)、[test_repo_paths.ps1](test_repo_paths.ps1)、[test_quality_evidence.ps1](test_quality_evidence.ps1)：隔离正负向回归

## 可重复生成的导航

[render_knowledge_views.py](render_knowledge_views.py)从身份图生成八域索引、概念主责与UE专题页；运行后用 `python scripts/render_knowledge_views.py --check` 检查漂移。生成器写入前检查输出白名单、根内路径及重解析点，不修改正文、书籍或日志；[test_render_knowledge_views.py](test_render_knowledge_views.py)覆盖这些拒绝路径。

## 增强检查

- [validate_knowledge.py](validate_knowledge.py)：真实YAML、Markdown链接/锚点、稳定身份与概念关系图
- [test_knowledge_validation.py](test_knowledge_validation.py)：新检查器的正负向用例
- [requirements-knowledge.txt](requirements-knowledge.txt)：固定版本依赖

安装依赖后运行 `python scripts/validate_knowledge.py` 和 `python scripts/test_knowledge_validation.py`。现有PowerShell门禁仍需完整运行，Python绿灯不代替旧检查。完整扫描与变更范围扫描分别报告，不把后者描述成全库通过。

## 保留的材料处理工具

[translate_engine_architecture.py](translate_engine_architecture.py)、[translate_game_ai_pro.py](translate_game_ai_pro.py)、[build_game_ai_pro_nav.py](build_game_ai_pro_nav.py)与现有授权书籍流程关联，本次保留不执行。它们不是质量门禁，不应覆盖已经人工整理的知识正文。

[append_lesson.ps1](append_lesson.ps1)是既有记录工具；历史日志本次保留，不与知识检查混用。

[仓库首页](../README.md)

## 可逆正文迁移

- [migrate_knowledge.py](migrate_knowledge.py)：默认只读干跑，按已审迁移计划改标准链接目标并校验逆变换；显式`--apply`才写仓库，不提交Git
- [test_migrate_knowledge.py](test_migrate_knowledge.py)：隔离Git夹具检验链接语法、保护路径、冲突、并发变化和恢复边界

迁移前保持工作区无并发写入，确认精确范围与原文快照；报告和原文恢复副本保存在仓库外。迁移后更新稳定身份的path/legacy_paths，重建导航与清单并运行全部检查。书籍与工作日志不作为搬迁源；来源保护不能由一个alias取消。

## 教学与证据检查边界

```powershell
pwsh -NoProfile -File ./scripts/test_quality_evidence.ps1
pwsh -NoProfile -File ./scripts/check_repo.ps1 -Root <仓库绝对路径>
pwsh -NoProfile -File ./scripts/check_repo.ps1 -Root <仓库绝对路径> -UeInstallRoot <实际checkout绝对根>
```

`-UeInstallRoot` 指含 `Engine` 的实际只读源码根，不带默认机器、UE 版本或 CL。未提供时明确报告源码路径核对未运行；显式提供无效根或缺失路径时失败。`Engine/...` 定位在选定根内检查；另一台机器的绝对来源标签不会自动重映射。越界和符号链接/重解析点一律失败，省略路径不算文件证据。文件存在不等于版本、符号或代码行为已核对。支持正文行内代码中的路径以及无空白的裸 `Engine/...` 或原生绝对路径；含空格路径必须用行内代码包围。其他语法、项目外路径和未提取到的引用不在覆盖范围，零条已检查不能描述成源码验证通过。

新回归在独占临时目录运行真实检查器，覆盖短而完整与长但缺元数据、非 UE、静态-only 声明、非代码来源、不同平台源码根、缺失/越界/链接跳转、编码与 Canonical 负例。它使用合成文件，不下载私有源码，不写真实仓库或历史结果；输出每例实际退出码。原有四套 PowerShell 回归、Python 检查与 Linux 实验仍须完整运行。

`check_repo` 不再用行数或全局占位词判定质量；显式“源码核对状态：已核对/已完成/已验证”至少要有具体文件定位，诚实的“未核对、待补充”和 API 的“预留”概念可以保留。此规则只发现可机械识别的声明缺口，不能证明未被匹配的声明真实。现有 metadata 标签、来源、日期、README、Canonical/DS 覆盖与路径安全要求继续生效。

L3/L4/L5 的关键词仍只是最低证据入口 lint，不证明实验已执行或整篇达到该等级。教学验收另按[写作规范](../references/写作规范.md)逐项独立复核原理因果、正反例、失败边界、来源与运行范围；CI 通过不自动产生 `verified`，更不等于人类审核。交付时分别报告机械检查、内容审查、实际实验和未运行边界。

Markdown 过滤是有明确边界的轻量实现，不是完整 CommonMark parser。围栏按字符和长度配对，支持引用块及直接列表项围栏；短于开围栏的同字符行不能提前结束代码块。来源与链接共用该过滤，代码中的假 URL 不能满足来源门禁；多反引号行内代码也不计来源。复杂嵌套列表、HTML、引用式链接等仍由现有 Python/CommonMark 检查与独立审阅补足，不能据轻量输出声称已解析所有 Markdown 语法。


## 受保护来源的已知缺陷

保护原件与格式欠账分开处理。经明确授权的 `.kb/preserved-source-defects.json` 仅登记原封保留的指定来源缺陷，包含精确路径、整字节 SHA256、缺陷类型和开围栏行号；它不修改或修复原件。检查器固定该控制文件的整字节指纹，不提供生产覆盖参数或自动刷新。增加条目或更改指纹必须有新的明确授权和独立审核，不能靠追加路径扩大豁免。

只有路径、文件字节、`unclosed_code_fence` 类型与当前检测行号全部相符才逐项输出 `KNOWN_SOURCE_DEFECT`，并单列计数。此时无其他失败可返回 exit 0，但结果必须写 `PASS_WITH_KNOWN_SOURCE_DEFECTS`，不能报告零缺陷或全质量通过。原件任何字节变化、新文章同类缺陷、同文其他编码/链接/元数据错误仍失败；孤儿、重复条目或控制文件指纹变化也失败。

回归测试只在独占临时目录复制检查器，并替换唯一的固定指纹以构造合成基线；其余检查逻辑必须逐字一致。生产脚本没有该覆盖开关，真实授权基线还须在实际仓库逐条核对。
