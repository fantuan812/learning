# OKF Bundle Log

> 知识成熟度：L2（OKF bundle 变更记录，只增不改）。

本文件只记录 OKF bundle 格式、入口和兼容级别变化。仓库维护经验仍追加到 [learning/log.md](learning/log.md)，两类日志不互相复制。

## 2026-08-20

- **Initialization**：建立 OKF v0.2 hybrid 兼容层；新增根 `index.md`、profile、Changed/Audit/Strict 校验、Obsidian Base 与知识条目模板。legacy 正文未批量改写。
- **Validation**：Changed 21/21 PASS；Audit 记录 369 个 legacy WARN；Strict 对这些 legacy 返回预期 FAIL；现有仓库门禁 PASS，未宣称全库官方严格合规。
- **Migration complete**：按支持/证据、计算机基础、游戏算法+AI、游戏服务端、游戏知识五批为 369 篇 legacy 文档补最小 frontmatter；每篇仅新增 7 行，正文、路径与链接不变。
- **Profile validation**：Changed/Audit/Strict 均为 Scanned 390、Conformant 390、Legacy 0、Excluded 2、FAIL 0；双 PowerShell 通过，manifest 与磁盘 392/392。该结果仅代表本库 profile 的轻量 lint，不是官方通用 OKF parser 认证。
- **Virtual navigation Phase A**：保留现有 Canonical 路径，新增六个 Domain MOC、跨域主题/生命周期 axes、四个 Obsidian Base 视图与可重复 manifest 生成器；物理目录迁移保持独立试点门禁。

## 2026-09-10

- **Scope exemption**：知识成熟度门禁按 `architecture.json` 的 sources 分层豁免 `读书笔记/`（与 `工作日志/`、`笔记/`、`方案/` 同口径），taxonomy 与架构说明同步登记；结构门禁（围栏闭合、相对链接、README 清单）对来源层继续生效。
- **Structure repair**：修复 42 处未闭合代码围栏与 2 处来源 README 缺链；2 个含乱码的来源文件名规范化为标准名称。
- **Manifest rebuild**：`.kb/manifest.yaml` 机械重建为 585 篇；路径集合、bytes、lines 漂移为 0；上一轮执行计划归档为 `.kb/plans/2026-09-08-architecture-refactor.md`。
- **Boundary**：宿主的 PowerShell 无法调用 git，仓库 PS 门禁脚本未在本机实跑；本轮验收为等价只读复现，未表述为脚本运行结果。未提交、未推送。

## 2026-09-11

- **New evidence**：新增 `evidence/tests/gameplay-core/`（背包事务、Buff 冲突、技能管线、属性聚合基准），4 个程序 29 条断言全通过，原始输出随附环境头；接入报告与链路正文。
- **New content**：新增 `系统实战/05-背包道具完整链路.md`（L4）；`系统实战/03`、`04` 增加本机证据小节。
- **Scan scope**：`.workbuddy/` 从知识扫描范围排除（`.gitignore`、`architecture.json`、`get_kb_markdown.ps1`）；manifest 重建为 589 篇。
- **Toolchain**：确认本机 MinGW-w64 g++ 16.1.0 可用（需将其 bin 目录加入 PATH），上一轮"无工具链"结论撤销。
- **New evidence (2)**：新增 `evidence/tests/damage-core/`（伤害与属性结算：15 条断言 + 单次结算 P50/P95/P99 基准 + 确定性重放），接入 `系统实战/11`。
- **New content (2)**：新增 `系统实战/11-伤害与属性结算完整链路.md`（L4）；`系统实战/03` 补分工声明与反向链接；规划链路 10→11 条。
- **Manifest**：重建为 591 篇，漂移 0。
- **New evidence (3)**：新增 `evidence/labs/profiling/`（插桩开销 6 样式 + 卡顿检测 + 预算降级，17 条断言），接入 `系统实战/10`。
- **New content (3)**：新增 `系统实战/10-性能问题定位完整链路.md`（L4）；`05`/`11` 增加度量口径互链；系统实战共 7 条链路落地。
- **Manifest (2)**：重建为 593 篇，漂移 0。
- **New content (4)**：新增 `系统实战/01-角色进入游戏完整链路.md`（L4，15 步闭环 + 失败矩阵 + 反模式 + 检查清单）；`系统实战` 共 8 条链路落地。
- **New evidence (4)**：新增 `evidence/tests/entry-core/`（登录票据密码学 / 会话状态机 / DS 租约与栅栏令牌 / JIP 状态追赶，**83 条断言全通过**），接入 `系统实战/01`。
- **Crypto self-check**：SHA-256 对 FIPS 180-4、HMAC-SHA256 对 RFC 4231 TC1/TC2/TC3/TC6 逐字匹配（含 56 字节填充边界、分段流式输入、>64 字节密钥先哈希）。
- **Benchmarks**：JIP 追赶 p50 6.510 / 117.960 / 2104.845 µs（2 000 / 20 000 / 200 000 实体），全量快照 1.300 / 25.200 / 1941.100 µs；**追赶 = 快照拷贝 + 重放，CPU 上永不比全量便宜**，带宽节省 1.00× / 1.00× / 10.00×。
- **Defect fixed**：状态机在分配失败后未中断链路，带着空 DS 走完 Travel/Load/Spawn（"Ready 但未连上"），由断言 E18/E19 抓出并修复；另修正 JIP 乱序包"重复即丢弃"的语义错误与基准中的多余排序（66.6µs → 6.5µs）。
- **Manifest (3)**：重建为 596 篇，漂移 0。
