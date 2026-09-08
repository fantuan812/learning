#!/usr/bin/env python3
"""
Game Engine Architecture (4th Edition) PDF Chunk Split & Gemini 3.8 Flash Direct Multimodal Translation Pipeline.
Directly reads PDF pages via PyMuPDF, chunks them, sends base64 PDF to Gemini 3.8 Flash via OpenAI-compatible endpoint,
and synthesizes high-quality, comprehensive Chinese Markdown chapters conforming to OKF specifications.
"""

import os
import sys
import json
import time
import base64
import argparse
import traceback
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed

# Add local libraries path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".py_libs")))
import pymupdf
import requests

ENDPOINT = "http://127.0.0.1:10100/v1/chat/completions"
MODEL = "google-antigravity/gemini-3.8-flash-high"
CACHE_DIR = Path(".kb_work/cache")
CACHE_DIR.mkdir(parents=True, exist_ok=True)

CHAPTERS_CONFIG = {
    1: [
        {"num": 1, "name": "01-导论.md", "title": "第1章 导论（Introduction）", "start": 18, "end": 75,
         "desc": "游戏与游戏引擎的本质、软实时模拟特征、各类型引擎架构差异、商业引擎巡礼、运行时分层架构全景图及资产调节管线（ACP）。",
         "tags": ["game-engine", "architecture", "runtime", "soft-real-time", "asset-pipeline", "jason-gregory"]},
        {"num": 2, "name": "02-专业工具.md", "title": "第2章 专业工具（Tools of the Trade）", "start": 76, "end": 109,
         "desc": "现代游戏工业级专业工具链：版本控制系统与大文件流送、C++编译器/链接器深入剖析、构建系统、现代IDE、代码分析工具与内存诊断利器。",
         "tags": ["game-engine", "tools", "version-control", "compilers", "profiling", "memory-debugging"]},
        {"num": 3, "name": "03-游戏软件工程基础.md", "title": "第3章 游戏软件工程基础（Fundamentals of Software Engineering for Games）", "start": 110, "end": 202,
         "desc": "游戏底层软件工程基石：现代C++最佳实践、异常与断言系统、数据与代码的内存布局、硬件基础、CPU缓存体系、虚函数开销与内存对齐。",
         "tags": ["game-engine", "software-engineering", "cpp", "memory-layout", "cache", "simd", "performance"]},
        {"num": 4, "name": "04-并行与并发编程.md", "title": "第4章 并行与并发编程（Parallelism and Concurrent Programming）", "start": 203, "end": 349,
         "desc": "高并发与多核并行编程体系：隐式与显式并发、操作系统线程调度内核原理、同步原语与锁竞争、无锁并发（Lock-Free）、原子操作、SIMD向量化与GPGPU通用计算。",
         "tags": ["game-engine", "concurrency", "multithreading", "lock-free", "simd", "gpgpu", "job-system"]},
        {"num": 5, "name": "05-游戏中的3D数学.md", "title": "第5章 游戏中的3D数学（3D Math for Games）", "start": 350, "end": 403,
         "desc": "游戏核心三维数学原理：坐标系与空间变换、向量代数几何意义、仿射变换矩阵、四元数（Quaternions）数学推导与球面线性插值（SLERP）、硬件矩阵流水线。",
         "tags": ["game-engine", "math", "linear-algebra", "quaternion", "matrix-transform", "3d-math"]},
        {"num": 6, "name": "06-引擎支持系统.md", "title": "第6章 引擎支持系统（Engine Support Systems）", "start": 404, "end": 462,
         "desc": "游戏底层通用支持子系统：子系统生命周期与启动关闭拓扑、自定义内存分配器（Stack/Pool/Frame）、字符串哈希（StringId）、全局通用数据结构与随机数发生器。",
         "tags": ["game-engine", "subsystems", "memory-allocator", "string-hash", "engine-core", "architecture"]},
        {"num": 7, "name": "07-资源与文件系统.md", "title": "第7章 资源与文件系统（Resources and the File System）", "start": 463, "end": 504,
         "desc": "资产管道与运行时资源管理：虚拟文件系统（VFS）、异步I/O流送、资源生命周期管理、资源注册表与元数据缓存、打包与压缩（Pak/Bundle）、热重载机制。",
         "tags": ["game-engine", "resource-management", "vfs", "async-io", "asset-streaming", "pak-system"]},
        {"num": 8, "name": "08-游戏循环与实时模拟.md", "title": "第8章 游戏循环与实时模拟（The Game Loop and Real-Time Simulation）", "start": 505, "end": 537,
         "desc": "核心主循环与时间调度：变步长与固定步长主循环对比、时间度量与高精度时钟、半固定步长物理累积积分、多线程与多阶段流水线主循环拓扑结构。",
         "tags": ["game-engine", "game-loop", "time-step", "simulation", "framerate-independence", "clock"]},
        {"num": 9, "name": "09-人机交互设备.md", "title": "第9章 人机交互设备（Human Interface Devices）", "start": 538, "end": 565,
         "desc": "人机交互与输入子系统：键盘/鼠标/手柄驱动抽象、模拟量死区与滤波曲线、手柄震动与力反馈、输入上下文映射（Enhanced Input）与手势识别。",
         "tags": ["game-engine", "input-system", "hid", "gamepad", "deadzone", "input-mapping"]},
        {"num": 10, "name": "10-调试与开发工具.md", "title": "第10章 调试与开发工具（Tools for Debugging and Development）", "start": 566, "end": 595,
         "desc": "开发诊断与性能遥测设施：运行时屏幕文本与图形绘制、控制台控制变量系统（CVar）、内置性能剖析器与微秒级时间轴抓取、崩溃转储与内存追踪。",
         "tags": ["game-engine", "debugging", "cvar", "profiling", "telemetry", "in-game-tools"]},
    ],
    2: [
        {"num": 11, "name": "11-渲染.md", "title": "第11章 渲染引擎（Rendering）", "start": 18, "end": 152,
         "desc": "三维渲染引擎核心原理：渲染管线软硬件演进、现代图形API（DirectX 12/Vulkan）、几何处理与网格着色、裁剪与剔除技术、延迟着色与集群前向渲染（Clustered Forward）。",
         "tags": ["game-engine", "rendering", "graphics-pipeline", "vulkan", "dx12", "culling", "mesh-shader"]},
        {"num": 12, "name": "12-光照与后处理.md", "title": "第12章 光照与后处理（Lighting and Post-Processing）", "start": 153, "end": 252,
         "desc": "物理光照与后处理系统：辐射度学基础与光传输理论、渲染方程推导、PBR双向反射分布函数（BRDF）、阴影图算法、随机路径追踪与全局光照、HDR与后处理管线。",
         "tags": ["game-engine", "lighting", "pbr", "brdf", "ray-tracing", "global-illumination", "post-processing"]},
        {"num": 13, "name": "13-动画系统.md", "title": "第13章 动画系统（Animation Systems）", "start": 253, "end": 351,
         "desc": "骨骼与角色动画体系：骨骼层级与局部/全局姿态空间、四元数插值与动画剪辑、线性混合蒙皮（LBS）与双四元数蒙皮（DQS）、动画状态机、动作匹配（Motion Matching）与IK约束。",
         "tags": ["game-engine", "animation", "skeletal-animation", "skinning", "motion-matching", "inverse-kinematics"]},
        {"num": 14, "name": "14-碰撞与刚体动力学.md", "title": "第14章 碰撞与刚体动力学（Collision and Rigid Body Dynamics）", "start": 352, "end": 441,
         "desc": "物理与碰撞子系统：碰撞体图元与GJK/EPA分离轴算法、BVH空间加速结构、连续碰撞检测（CCD）、牛顿刚体运动学方程求解、约束与冲量接触模型、布料与载具动力学。",
         "tags": ["game-engine", "physics", "collision-detection", "rigid-body", "gjk", "contact-manifold", "constraints"]},
        {"num": 15, "name": "15-音频.md", "title": "第15章 音频系统（Audio）", "start": 442, "end": 538,
         "desc": "游戏音频工程与三维音效：声学物理基础、PCM与音频编解码器、3D空间化音频与HRTF头部传递函数、环境混响与多普勒效应、音频中间件（Wwise/FMOD）架构原理。",
         "tags": ["game-engine", "audio", "spatial-audio", "sound-engine", "dsp", "hrtf", "wwise"]},
        {"num": 16, "name": "16-玩法系统导论.md", "title": "第16章 玩法系统导论（Introduction to Gameplay Systems）", "start": 539, "end": 559,
         "desc": "玩法基础理论与技术基石：游戏机制剖析、玩法代码解耦原则、世界状态表示模型、软实时玩法逻辑的生命周期与可扩展性原则。",
         "tags": ["game-engine", "gameplay-systems", "architecture", "game-design", "state-machine"]},
        {"num": 17, "name": "17-运行时玩法系统.md", "title": "第17章 运行时玩法系统（Runtime Gameplay Systems）", "start": 560, "end": 674,
         "desc": "工业级玩法系统工程实现：游戏对象模型演进（传统OOP vs 组件模型 vs ECS架构）、事件分发与消息传递系统、脚本语言嵌入（Lua/Python）与虚拟机交互、游戏状态保存与加载、玩法AI系统。",
         "tags": ["game-engine", "gameplay-runtime", "ecs", "component-system", "event-system", "scripting", "save-load", "ai"]},
        {"num": 18, "name": "18-还有更多.md", "title": "第18章 还有更多？（You Mean There’s More?）", "start": 675, "end": 679,
         "desc": "未尽的技术前沿：网络同步（Replication/Rollback）、现代UI框架与HUD系统、大规模开放世界流送与前沿技术展望。",
         "tags": ["game-engine", "networking", "ui-system", "open-world", "future-tech"]},
    ]
}

PDF_FILES = {
    1: "读书笔记/Game Engine Architecture Volume I Foundations and Core Engine Systems Fourth Edition (Jason Gregory) (z-library.sk, 1lib.sk, z-lib.sk).pdf",
    2: "读书笔记/Game Engine Architecture Volume II Graphics, Motion, and Sound Fourth Edition (Jason Gregory) (z-library.sk, 1lib.sk, z-lib.sk).pdf"
}

OUTPUT_DIRS = {
    1: Path("读书笔记/游戏引擎架构/卷1-基础与核心引擎系统"),
    2: Path("读书笔记/游戏引擎架构/卷2-图形动作与声音")
}


def extract_pdf_chunk(doc, from_page, to_page):
    """Extract pages [from_page, to_page] (1-based) into in-memory PDF bytes."""
    sub_doc = pymupdf.open()
    sub_doc.insert_pdf(doc, from_page=from_page - 1, to_page=to_page - 1)
    pdf_bytes = sub_doc.tobytes()
    sub_doc.close()
    return pdf_bytes


def call_gemini_translation(pdf_bytes, chunk_desc, retries=3):
    """Send base64 PDF directly to Gemini 3.8 Flash for professional multimodal technical reconstruction."""
    b64_pdf = base64.b64encode(pdf_bytes).decode("utf-8")
    prompt = f"""你是一位资深游戏引擎系统架构师与权威技术专家。
当前研读对象：《Game Engine Architecture, 4th Edition》（Jason Gregory著）原版英文专著附件页面（{chunk_desc}）。
请对附件 PDF 页面所涵盖的核心系统架构、技术原理、算法流程、数据结构、代码实现与工程设计模式进行深入研读，并将其重构为一份详尽、深入、严谨的高级工程技术文档（Markdown 格式）。

【重构与技术规范要求】：
1. 全面深度覆盖：完整解构页面中所有的技术概念、子系统拓扑、算法推导、底层机理与作者工业界实战经验，保持工业级深度，不要概括缩写或省略细节；
2. 行业标准专业术语：所有计算机图形学、游戏引擎、系统工程专业术语采用业界公认的中文译名，并在首次出现时标注英文原文（例如：面向数据设计（Data-Oriented Design, DOD）、双四元数蒙皮（Dual Quaternion Skinning, DQS））；
3. 代码与数学公式：所有原书代码段、伪代码、数据结构定义原样保留，并美化缩进与关键行注释；所有数学符号与方程必须使用严格规范的 LaTeX 语法（行内公式 $...$，独立公式 $$...$$）；
4. 架构图表全景还原：原书中的系统架构框图、数据流图、对比表格，请使用清晰的 Markdown 表格、ASCII 文本结构图或结构化分层列表进行完整还原；
5. 层级排版：使用清晰的 Markdown 标题层级（##, ###, ####）。由于本篇属于章节内部的一部分，请不要在开头输出大标题（# ），直接进入对应小节与正文论述。

请直接输出重构后的 Markdown 格式技术正文，不要输出任何额外的客套话或外部包裹说明。"""

    payload = {
        "model": MODEL,
        "messages": [
            {
                "role": "user",
                "content": [
                    {"type": "text", "text": prompt},
                    {
                        "type": "image_url",
                        "image_url": {
                            "url": f"data:application/pdf;base64,{b64_pdf}"
                        }
                    }
                ]
            }
        ],
        "max_tokens": 8192,
        "temperature": 0.2
    }

    for attempt in range(retries):
        try:
            res = requests.post(ENDPOINT, json=payload, timeout=240)
            if res.status_code != 200:
                print(f"[{chunk_desc}] HTTP {res.status_code}: {res.text[:200]}")
                time.sleep(3 * (attempt + 1))
                continue
            data = res.json()
            content = data["choices"][0]["message"]["content"].strip()
            # If wrapped in ```markdown ... ```, unwrap
            if content.startswith("```markdown"):
                content = content[len("```markdown"):].strip()
            if content.startswith("```md"):
                content = content[len("```md"):].strip()
            if content.endswith("```"):
                content = content[:-3].strip()

            # Check if refused or excessively short
            if "抱歉，我无法" in content or len(content) < 1000:
                print(f"[{chunk_desc}] Warning: content too short ({len(content)} chars) or refused, retrying...")
                time.sleep(2 * (attempt + 1))
                continue

            return content
        except Exception as e:
            print(f"[{chunk_desc}] Attempt {attempt + 1} failed: {e}")
            time.sleep(3 * (attempt + 1))

    raise RuntimeError(f"Failed to translate {chunk_desc} after {retries} attempts.")


def process_single_chunk(task):
    vol, ch_num, chunk_idx, total_chunks, c_start, c_end, pdf_bytes, overwrite = task
    cache_file = CACHE_DIR / f"v{vol}_c{ch_num:02d}_chunk{chunk_idx:02d}_{c_start}_{c_end}.md"
    chunk_desc = f"Vol {vol} Ch {ch_num} Chunk {chunk_idx}/{total_chunks} (pp.{c_start}-{c_end})"

    if cache_file.exists() and not overwrite:
        with open(cache_file, "r", encoding="utf-8") as f:
            cached_text = f.read()
        if len(cached_text) >= 1000 and "抱歉，我无法" not in cached_text:
            print(f"[{chunk_desc}] Using valid cached reconstruction ({len(cached_text)} chars).")
            return chunk_idx, cached_text

    print(f"[{chunk_desc}] Sending to {MODEL}...")
    t0 = time.time()
    translated_md = call_gemini_translation(pdf_bytes, chunk_desc)
    elapsed = time.time() - t0
    print(f"[{chunk_desc}] Reconstructed successfully in {elapsed:.1f}s ({len(translated_md)} chars).")

    with open(cache_file, "w", encoding="utf-8") as f:
        f.write(translated_md)
    return chunk_idx, translated_md


def process_chapter(vol, ch_info, doc, chunk_size=8, workers=4, overwrite=False):
    """Process a single chapter: divide into chunks of chunk_size pages, translate with concurrency, and assemble."""
    ch_num = ch_info["num"]
    start_p = ch_info["start"]
    end_p = ch_info["end"]
    total_pages = end_p - start_p + 1

    chunks = []
    curr = start_p
    idx = 1
    while curr <= end_p:
        chunk_end = min(curr + chunk_size - 1, end_p)
        chunks.append((idx, curr, chunk_end))
        curr = chunk_end + 1
        idx += 1

    print(f"\n=======================================================")
    print(f"Processing Vol {vol} - Ch {ch_num:02d}: {ch_info['title']}")
    print(f"Pages: {start_p} - {end_p} (Total {total_pages} pages, {len(chunks)} chunks, concurrency={workers})")
    print(f"=======================================================")

    tasks = []
    for chunk_idx, c_start, c_end in chunks:
        pdf_bytes = extract_pdf_chunk(doc, c_start, c_end)
        tasks.append((vol, ch_num, chunk_idx, len(chunks), c_start, c_end, pdf_bytes, overwrite))

    chunk_results = [None] * len(chunks)
    with ThreadPoolExecutor(max_workers=workers) as executor:
        future_map = {executor.submit(process_single_chunk, task): task[2] for task in tasks}
        for future in as_completed(future_map):
            chunk_idx, translated_md = future.result()
            chunk_results[chunk_idx - 1] = translated_md

    # Assemble into full chapter document
    output_dir = OUTPUT_DIRS[vol]
    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / ch_info["name"]

    tags_formatted = "\n".join([f"  - {tag}" for tag in ch_info["tags"]])
    vol_title = "卷1：基础与核心引擎系统" if vol == 1 else "卷2：图形动作与声音"
    
    header = f"""---
type: Reference
title: "{ch_info['title']}"
description: "{ch_info['desc']}"
tags:
{tags_formatted}
status: stable
verified: []
maturity: L2
updated: 2026-09-07
---

# {ch_info['title']}

> 来源：*Game Engine Architecture, 4th Edition* (Jason Gregory) Volume {"I" if vol == 1 else "II"}, Chapter {ch_num}.  
> 原书页码：p.{start_p} ~ p.{end_p}（第四版，最新两卷本官方权威英文原版端到端多模态视觉重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于原版英文 PDF 完整视觉多模态精准重构，完整还原工业级系统架构、算法推导、数学公式、图表结构与代码实现。  
> 专栏导航：[{vol_title}](README.md) ｜ [专栏首页](../README.md)

---

"""

    body_parts = []
    for c_text in chunk_results:
        # Strip any leading `# ` title from chunk if it repeated the main chapter title
        lines = c_text.splitlines()
        filtered_lines = []
        skip_first = True
        for line in lines:
            if skip_first and line.startswith("# "):
                continue
            skip_first = False
            filtered_lines.append(line)
        body_parts.append("\n".join(filtered_lines).strip())

    full_content = header + "\n\n---\n\n".join(body_parts) + "\n"

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(full_content)

    print(f"\n[SUCCESS] Chapter {ch_num:02d} successfully written to {output_path} ({len(full_content)} chars, {output_path.stat().st_size} bytes).")


def main():
    parser = argparse.ArgumentParser(description="Translate Game Engine Architecture PDF to Markdown via Gemini 3.8 Flash.")
    parser.add_argument("--vol", type=int, choices=[1, 2], help="Volume number (1 or 2).")
    parser.add_argument("--chapter", type=int, help="Chapter number (1..18).")
    parser.add_argument("--chunk-size", type=int, default=8, help="Number of pages per chunk (default: 8).")
    parser.add_argument("--workers", type=int, default=4, help="Concurrency workers (default: 4).")
    parser.add_argument("--overwrite", action="store_true", help="Overwrite existing cache.")
    args = parser.parse_args()

    vols_to_process = [args.vol] if args.vol else [1, 2]

    for vol in vols_to_process:
        pdf_path = PDF_FILES[vol]
        if not os.path.exists(pdf_path):
            print(f"Error: PDF file not found: {pdf_path}")
            continue

        doc = pymupdf.open(pdf_path)
        chapters = CHAPTERS_CONFIG[vol]

        for ch in chapters:
            if args.chapter and ch["num"] != args.chapter:
                continue
            process_chapter(vol, ch, doc, chunk_size=args.chunk_size, workers=args.workers, overwrite=args.overwrite)

        doc.close()


if __name__ == "__main__":
    main()
