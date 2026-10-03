---
type: Plan
title: "八域重构：当前执行合同"
status: stable
verified: []
maturity: L0
updated: 2026-10-03
---

# 八域重构：当前执行合同

> 知识成熟度：L0（组织与导航条目，不代表主题内容已实测）

本批从main ea63344建立八域身份图与可验证导航，不搬动书籍或工作日志。用户已批准八域分类、提交中文PR及合并；结构完善同时修复知识中的具体错误并补可运行边界。

## 写者与范围

- 整合写者：`.kb/knowledge-map.json`、`知识/`八域索引、`00_Index/UE专题.md`、`跨域关系.md`、`学习路线.md`、`实验与案例.md`、scripts/render_knowledge_views.py与test_render_knowledge_views.py、.gitattributes保持跨平台Markdown字节一致、evidence/README.md的新增实验入口、根README/AGENTS、00_Index README/MOC、references/知识库架构、scripts/README、.kb架构/taxonomy/README/manifest/当前计划/decisions
- 校验写者：仅新增scripts/validate_knowledge.py、test_knowledge_validation.py、requirements-knowledge.txt、.github/workflows/knowledge.yml
- 内容写者：仅游戏算法/01-寻路与图论/02-A星算法与优化.md及evidence/algorithms/astar-contract/；补队列与重开合同的反例和可执行测试
- 真实链接/模板修复：游戏知识/README.md的Lyra锚点；references/templates/OKF-知识条目.md移除空的可选字段，保留可实例化日期模板
- 元数据修复：GameAIPro卷1/05及卷4/05的title/description乱码，frontmatter之后正文原字节保留；Scene-Map-Zone maturity按正文明确证据L2对齐

## 保全与验收

原819个跟踪文件hash、HEAD、index和diff已在仓库外保存。书籍正文/PDF/图片、工作日志原文/日期/附件、现有实验代码和原始结果不可丢失。新导航仅链接它们，不复制正文。除精确修复的书籍frontmatter外全部书籍文件原字节；原日志全部原字节。

现行四套回归、OKF Changed/Audit/Strict、repo、architecture、diff检查继续运行；新增真实YAML/完整链接/身份关系负向测试。新内容实验必须实跑并记录实际边界。冻结index后由独立审查者核路径、blob、mode与hash；无通过证据不得提交。

## 后续实际迁移

按本图固定ID生成旧→新映射，原理/源码/案例各归一个主域；对所有入链修复后才删旧空骨架。结构工作同时补前置知识、知识错误与验证练习，不能只换首页。每阶段独立PR、正常门禁合并，不改写历史。
