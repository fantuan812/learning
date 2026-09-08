#!/usr/bin/env python3
"""
Generate OKF-compliant READMEs and MOC for Game AI Pro collection.
"""

import sys
import os
import re
from pathlib import Path

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".py_libs")))
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from scripts.translate_game_ai_pro import VOLUMES_CONFIG, BASE_OUT_DIR, fetch_site_inventory, slugify

def build_navigation():
    inventory = fetch_site_inventory()
    BASE_OUT_DIR.mkdir(parents=True, exist_ok=True)

    # 1. Main GameAIPro README
    main_readme = BASE_OUT_DIR / "README.md"
    main_content = f"""---
type: Index
title: "《Game AI Pro》工业级游戏AI技术专栏"
description: "Steve Rabin 主编权威巨著《Game AI Pro》系列全4卷中文翻译、多模态精读与工业级工程实践。"
tags:
  - game-ai
  - game-ai-pro
  - reading-notes
  - behavior-trees
  - decision-making
  - navigation
status: stable
verified: []
maturity: L1
updated: 2026-09-07
---

# 《Game AI Pro》工业级游戏AI技术专栏

> 官方权威来源：[Game AI Pro 官网 (gameaipro.com)](https://www.gameaipro.com/)  
> 系列主编：**Steve Rabin**（DigiPen 理工学院教授，资深任天堂北美前首席软件工程师，权威游戏 AI 架构先驱）  
> 专栏定位：汇聚全球一线 AAA 工作室（包含微创、索尼顽皮狗、Square Enix、育碧、暴雪、Crytek 等）顶尖 AI 工程师的实战智慧与工业级源码架构。

---

## 专栏架构全景与分卷导航

本专栏由 **Gemini 3.8 Flash** 端到端多模态视觉直读官网权威 PDF 进行深度技术解构与中文重构，完整保留公式推导、行为树与状态机架构图、代码缩进以及工程权衡。

| 分卷 | 英文原名 | 核心技术侧重 | 章节规模 | 入口导航 |
| :--- | :--- | :--- | :---: | :--- |
| **第4卷** | Game AI Pro 4: Online Edition 2021 | 自动化AI测试、并行事件模拟、战术AI、自适应行为 | **17 章** | [卷4-OnlineEdition2021/](卷4-OnlineEdition2021/README.md) |
| **第3卷** | Game AI Pro 3: Balanced Cuts | 复杂行为树拓展、效用系统深度优化、3D体素寻路 | **42 章** | [卷3-GameAIPro3/](卷3-GameAIPro3/README.md) |
| **第2卷** | Game AI Pro 2: More Collected Wisdom | 战术射击、动态掩体、动态感知滤波、微观移动协调 | **40 章** | [卷2-GameAIPro2/](卷2-GameAIPro2/README.md) |
| **第1卷** | Game AI Pro 1: Collected Wisdom | 经典决策架构、NavMesh网格生成、随机性算法奠基 | **47 章** | [卷1-GameAIPro1/](卷1-GameAIPro1/README.md) |

---

## 核心技术专题索引（Topic MOC）

- **决策架构（Decision Making）**：有限状态机（HFSM）、分层任务网络（HTN）、行为树（Behavior Trees）、效用系统（Utility AI）与目标导向动作规划（GOAP）。
- **移动与寻路（Movement & Pathfinding）**：导向行为（Steering Behaviors）、跳点搜索（JPS/JPS+）、3D连续空间寻路、漏斗算法（String Pulling）。
- **战术与空间推理（Tactics & Spatial Reasoning）**：影响力图（Influence Maps）、遮蔽与掩体评估系统（Cover Queries）、能见度射线投射优化。
- **体系化验证与测试（Testing & Tooling）**：AI 确定性时钟同步、自动化纤程跨帧断言测试、可视化黑板与调试渲染。
"""
    main_readme.write_text(main_content, encoding="utf-8")
    print(f"Generated {main_readme}")

    # 2. Volume READMEs
    for v, info in VOLUMES_CONFIG.items():
        v_dir = BASE_OUT_DIR / info["folder"]
        v_dir.mkdir(parents=True, exist_ok=True)
        v_readme = v_dir / "README.md"
        chapters = inventory[v]

        ch_rows = []
        for c in chapters:
            ch_num = c["ch_num"]
            title = c["title"]
            fname = f"{ch_num:02d}-{slugify(title)}.md"
            target_path = v_dir / fname
            status_badge = "**已重构**" if target_path.exists() else "*待处理*"
            link = f"[{title}]({fname})" if target_path.exists() else f"[{title}]({c['url']})"
            ch_rows.append(f"| **第{ch_num:02d}章** | {link} | {status_badge} |")

        table_str = "\n".join(ch_rows)

        tags_str = "\n".join([f"  - {t}" for t in info["tags"]])
        vol_content = f"""---
type: Index
title: "{info['title']}"
description: "{info['desc']}"
tags:
{tags_str}
status: stable
verified: []
maturity: L1
updated: 2026-09-07
---

# {info['title']}

> 官方权威来源：[Game AI Pro 官网](https://www.gameaipro.com/)  
> 核心定位：{info['desc']}  
> 专栏导航：[Game AI Pro 专栏首页](../README.md) ｜ [读书笔记首页](../../README.md)

---

## 章节全景目录与阅读导航

| 章节编号 | 章节标题 / 核心主题 | 状态 |
| :---: | :--- | :---: |
{table_str}
"""
        v_readme.write_text(vol_content, encoding="utf-8")
        print(f"Generated {v_readme}")

if __name__ == "__main__":
    build_navigation()
