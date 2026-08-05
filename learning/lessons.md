# 稳定维护规则

这些规则来自实际维护中已确认、下次仍应直接应用的经验；一次性事项写入 `log.md`，不要把未验证的猜测固化为规则。

## 仓库与协作

1. 目标仓库固定为 `C:\project\git`，先检查 `git status --short`，不覆盖用户已有修改。
2. 默认不提交、不推送；只有用户明确授权时才执行 `git commit` / `git push`。
3. 并行修改必须按目录或文件切分，避免多个执行者同时改同一个 README；完成后立即做一次全库体检。
4. 不修改 `C:\Program Files\Epic Games\UE_5.8`，引擎安装目录只作为只读证据源。

## 编码与文件

5. 所有 Markdown 使用 UTF-8 无 BOM；PowerShell 写文件必须调用 `System.IO.File.WriteAllText` 并传入 `UTF8Encoding($false)`。
6. 修改 README 时同时检查当前目录的实际文件清单、上级导航和相对链接；不能只修正文不修导航。
7. 正文通常不少于 300 行；README、日志、实验记录和路线图可短，但必须保持用途清晰并接受 WARN。

## UE5.8 事实边界

8. 当前基准固定为本机 `Build.version` 的 UE 5.8.0 / CL 55116800 / `++UE5+Release-5.8`；版本敏感结论必须带源码路径或官方 5.8 链接。
9. UE 4.27 和早期 UE5 只作为兼容性或迁移差异，不得出现在无条件的当前适用版本声明中。
10. 同名概念文档不等于源码深度覆盖；源码矩阵必须单独标注“已完成、简述、待补、规划”，并列出代表文件与验证命令。
11. 旧版 `docs.unrealengine.com/<旧版本>` 链接必须替换为 `dev.epicgames.com` 的当前 UE5.8 页面、API 模块页或总页。

## 体检与学习

12. 交付前运行 `scripts/check_repo.ps1 -Root C:\project\git`；FAIL 必须归零，WARN 必须在报告中解释。
13. `learning/log.md` 只增不改，用 `scripts/append_lesson.ps1` 追加真实发生的经验；重复出现两次或用户明确纠正的规则才提炼到本文件。
14. PowerShell 行数统计使用 `$text -split "`r?`n"`；不要把第三个参数 `-1` 写成逗号参数，否则在当前 PowerShell 解析下可能把全文误报为 1 行。
