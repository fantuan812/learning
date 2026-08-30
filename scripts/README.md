---
type: Index
title: "scripts —— 校验与维护脚本"
description: "知识库门禁与维护脚本说明：OKF/仓库校验、链接检查、manifest 重建、经验追加与一键校验。"
tags:
  - scripts
  - validation
  - knowledge-base
status: stable
verified: []
maturity: L2
updated: 2026-08-30
---

# scripts —— 校验与维护脚本

> 知识成熟度：L2（维护脚本目录说明）。

| 脚本 | 用途 | 运行环境 |
| --- | --- | --- |
| [check_okf.ps1](check_okf.ps1) | OKF v0.2 轻量门禁：`-Mode Changed/Audit/Strict` | Windows PowerShell 5.1 / pwsh |
| [check_repo.ps1](check_repo.ps1) | 仓库全量门禁：UTF-8/BOM/围栏/断链/README 清单/成熟度/领域与 DS 专项 | Windows PowerShell 5.1 / pwsh |
| [check_links.py](check_links.py) | 跨平台 Markdown 健康检查（与 check_repo 链接/编码语义等价） | Python 3.10+（WSL/Linux/CI） |
| [validate.sh](validate.sh) | 一键校验入口：健康检查 + `git diff --check` + 有 pwsh 时跑 OKF/仓库门禁 | Git Bash / WSL |
| [rebuild_manifest.ps1](rebuild_manifest.ps1) | 按磁盘机械重建 `.kb/manifest.yaml`（path/kind/bytes/lines/maturity） | Windows PowerShell 5.1 / pwsh |
| [append_lesson.ps1](append_lesson.ps1) | 向 `learning/log.md` 追加带时间戳的维护经验条目 | Windows PowerShell 5.1 / pwsh |

## 快速开始

Windows PowerShell 5.1 / pwsh：

```powershell
& .\scripts\check_okf.ps1 -Root (Get-Location) -Mode Strict
& .\scripts\check_repo.ps1 -Root (Get-Location)
& .\scripts\rebuild_manifest.ps1
```

WSL / Linux / Git Bash（无 PowerShell 时的等价校验）：

```bash
bash scripts/validate.sh
python3 scripts/check_links.py .
```

GitHub Actions（`.github/workflows/validate.yml`）：push / PR 时自动运行 OKF Strict、check_repo 与断链检查；CI 无本机 UE 安装时，`check_repo` 的源码证据路径校验降级为 WARN（与本地未装 UE 时的行为一致）。