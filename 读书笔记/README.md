---
type: Index
title: "读书笔记"
description: "经典技术著作与行业文献翻译、精读与笔记专栏。"
tags:
  - reading-notes
  - game-engine
  - books
status: stable
verified: []
maturity: L1
updated: 2026-09-07
---

# 读书笔记

本目录收录游戏工程与计算机系统领域的经典著作、前沿论文与权威行业规范的中文翻译、深度精读与工程笔记。

## 专栏导航

| 专栏 / 著作 | 原作者 | 版本 / 来源 | 状态 | 入口 |
| --- | --- | --- | --- | --- |
| **《游戏引擎架构》（Game Engine Architecture）** | Jason Gregory | 第4版（全2卷） | **全书完结**（全2卷共18章已基于 Gemini 3.8 Flash 多模态视觉重构完成） | [游戏引擎架构/](游戏引擎架构/README.md) |
| **《Game AI Pro》工业级游戏AI技术专栏** | Steve Rabin 主编 | 全4卷（全146章完整收录） | **全书完结**（全4卷已基于官网官方 PDF 端到端视觉多模态重构完成） | [GameAIPro/](GameAIPro/README.md) |
| **《游戏编程模式》（Game Programming Patterns）** | Robert Nystrom (Bob Nystrom) | 官方权威中文版 | **全书完结**（全6大体系共27章完整Markdown复刻，含全部高清图表与C++实现） | [游戏编程模式/](游戏编程模式/README.md) |

## 专栏体系与多模态视觉重构管线

当前目录归档了原版英文 PDF 专著与在线权威技术文献：
- **《Game Engine Architecture》（第4版，最新版全2卷，1344页）**：通过 `scripts/translate_engine_architecture.py` 自动化分块重构，还原系统架构分层与图形管线底层；
- **《Game AI Pro》（全4卷，共146章）**：通过 `scripts/translate_game_ai_pro.py` 自动化抓取 [Game AI Pro 官网](https://www.gameaipro.com/) 官方开放获取 PDF，直接通过 Gemini 3.8 Flash 端到端多模态视觉理解重构为标准 Markdown。

重构过程严格摒弃了易导致公式乱码与排版错乱的传统文本抽取机翻方式，完整保全了原书的架构框图、LaTeX 数学公式、行为树/状态机流转图、C++ 源码实现及工业界实战设计权衡。
