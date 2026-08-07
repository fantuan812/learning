# 维护经验日志

本文件只追加，不回写历史记录。时间戳由维护脚本或真实执行时间产生。
- 2026-08-05 20:55:22 +08:00：本次验收确认：知识库从 8 大分类纠正为游戏知识 13 分类；README 文件清单必须使用真实相对链接；体检脚本不能用 PowerShell -split 的第三个参数 -1 统计行数，否则会把全文当作单行；源码覆盖必须把概念已有与 UE5.8 源码深度完成分开，并固定 Build.version 的 CL 55116800/++UE5+Release-5.8 证据。
- 2026-08-05 21:12:36 +08:00：用户明确纠正：工作日志相关的知识点只沉淀在工作日志目录下（随日志文件存放并注册到工作日志/README.md），不因知识沉淀自动归入游戏算法/游戏知识等知识库分类；已固化进 lessons.md。另：本次沙箱运行器故障（CreateProcessAsUserW 拒绝访问）时，全流程改用 require_escalated 在沙箱外执行完成。
- 2026-08-05 23:16:51 +08:00：本次维护将六篇 UE5.8 P1 源码文章拆成骨架与 60-70 行分段代理任务，使用 fork_context=true 才能稳定落盘；共享 README/路线图按文件拆分避免冲突；最终 check_repo 实际统计为 176 个 Markdown（147 正文、29 README），WARN 仅为短日志/路线图/笔记，FAIL 0。六篇文章均经版本元数据、源码路径、Mermaid、围栏、链接和行数验收。
- 2026-08-06 10:59:56 +08:00：本轮六篇 UE5.8 源码专题已验收通过，体检 FAIL 0，Trace 路径已按本机修正，工作区不提交推送。
- 2026-08-06 14:32:41 +08:00：源码实际目录名是 游戏知识/12-引擎源码分析，派发前应先用 Get-ChildItem/Resolve-Path 核对目标；并发写 main 有推送竞态，所以本轮采用不重叠、串行子代理，子代理本地 commit 后由主代理验收并立即 push；P0 元数据/官方链接/日期门禁归零；P1 新增全栈运行闭环、UI可观测性、GameplayTasks-StateTree-GAS-AI、大世界植被渲染、质量门禁专题；P2 规则固化进 check_repo.ps1。
- 2026-08-06 17:06:16 +08:00：本次维护新增游戏服务端 P1 鉴权限流幂等灾备与可观测性专题：正文 1135 行、三处目标文件 UTF-8 无 BOM；check_repo 191 个 Markdown 检查 PASS/FAIL 0，既有 6 条短正文 WARN；独立内部链接检查需排除代码围栏和行内代码后断链为 0；按用户范围仅提交新分类与服务端根 README，未 push。
- 2026-08-06 17:50:15 +08:00：本轮维护收尾经验：P0 已修正 A* 8 方向曼哈顿启发、RFC793→RFC9293、HPA 条件边界和 ML-Agents 事实边界；AI 8 篇、服务端 16 篇、算法 13 篇补齐领域基线/最后更新/权威来源，补齐 3 篇 AI 验证入口；新增 AI 评测回放与 LLM 安全、服务端鉴权限流幂等灾备可观测性、算法动态寻路确定性与基准工程 3 个 P1 专题；check_repo 已扩展覆盖三领域，最终 193 个 Markdown、三领域 40 篇正文、领域五项门禁全 0、FAIL 0。子代理按不重叠范围执行，每批 commit 后由主代理验收并 push；本条由 append_lesson.ps1 生成时间戳，learning/log.md 只增不改历史。
- 2026-08-06 20:21:13 +08:00：本轮按不重叠目录串行派发 UE5.8 Dedicated Server 完善任务，新增启动/监听源码、Target/UAT 构建烘焙运行、实例生命周期与平台化、联机验收/Gauntlet 四篇专题及导航；主代理逐批运行 check_repo、源码路径/PowerShell 解析/链接验收后再逐批 git push；最终 DS 专项门禁覆盖必需文件、源码/构建/平台/测试锚点、质量说明和 ActorChannel.cpp 旧路径，全部为 0；首次验收发现新源码正文缺“版本基线”和 README 未登记，修复后才推送，说明先验收后推送能阻断质量门禁问题。
- 2026-08-07 10:20:20 +08:00：2026-08-07 完成 UE Dedicated Server 知识体系扩展：新增运行调优（NetServerMaxTickRate/FixedFrameRate/MaxClientRate/Pkt仿真/DDoS，核对 NetDriver/NetConnection/GameEngine/World 源码）、内容裁剪、Linux 部署容器、会话重连（NetDriver.h 超时属性核对）、日志崩溃观测、机器人压测容量、UNetDriver 连接通道源码 7 篇；check_repo.ps1 DS 门禁扩展为 11 篇（新增 ExtendedAnchorMissing 计数）；经验：文件名含空格的 Markdown 链接必须用 <...> 包裹，否则 check_repo 断链与 README 清单校验 FAIL；apply_patch 直接写中文文件名正常；每篇 300+ 行验收通过，check_repo FAIL 0，已提交推送 6008e52。
