---
type: Index
title: "知识库校验与工具"
status: stable
verified: []
maturity: L0
updated: 2026-10-03
---

# 知识库校验与工具

## 现行门禁

- [get_kb_markdown.ps1](get_kb_markdown.ps1)：统一Git可见Markdown范围
- [check_okf.ps1](check_okf.ps1)：当前OKF子集Changed/Audit/Strict检查
- [check_repo.ps1](check_repo.ps1)：内容、结构、相对链接与证据边界
- [check_architecture.ps1](check_architecture.ps1)：目录登记、别名与快照
- [rebuild_manifest.ps1](rebuild_manifest.ps1)：机械重建文件快照
- [test_architecture.ps1](test_architecture.ps1)、[test_kb_scope.ps1](test_kb_scope.ps1)、[test_okf_changed.ps1](test_okf_changed.ps1)、[test_repo_paths.ps1](test_repo_paths.ps1)：负向回归

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
