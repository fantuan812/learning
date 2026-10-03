---
type: Reference
title: "perf 性能分析（采样 Profiling）—— 耗时点测试方法二"
status: stable
verified: []
maturity: L0
updated: 2026-10-03
---
# perf 性能分析（采样 Profiling）—— 耗时点测试方法二

> 知识成熟度：L0（参考导航，不代表新增运行证据）。
> 知识基线：本页为主文的导航参考，不另立技术结论。
> 最后更新：2026-10-03。
> 验证建议：见主文的[练习与边界](03-性能工具：插桩与perf采样.md#10-验证与基准建议)；本页不另列运行结果。

本页保留旧笔记入口；命令、判读与修订统一维护在[性能工具：插桩与 perf 采样](03-性能工具：插桩与perf采样.md)。

- [事件、命令与采集流程](03-性能工具：插桩与perf采样.md#4-perf-采样)
- [调用链模式、Self/Children 与火焰图](03-性能工具：插桩与perf采样.md#43-调用链与火焰图怎么看)
- [指标口径](03-性能工具：插桩与perf采样.md#44-关键指标perf-stat)与[权限、符号、线程范围](03-性能工具：插桩与perf采样.md#45-权限符号与采集边界)
- [验证练习与证据限制](03-性能工具：插桩与perf采样.md#10-验证与基准建议)

先修：[Socket/Epoll 与 Reactor](../../01-编程与计算机基础/操作系统与系统I-O/01-Socket-Epoll与Reactor.md)。相关实践：[性能问题定位完整链路](10-性能问题定位完整链路.md)。

原笔记的命令与服务器分析要点已收敛；99Hz、免改源码和权限设置不再被写成准确性、零开销或可直接采样的保证。本页不声称新增 perf 执行结果。

来源核对入口：[Linux v6.12 perf record](https://raw.githubusercontent.com/torvalds/linux/v6.12/tools/perf/Documentation/perf-record.txt)、[Linux perf 安全](https://docs.kernel.org/admin-guide/perf-security.html)。命令、权限与回溯方式须按目标机版本验证；具体练习及未执行边界见上列主文入口。
