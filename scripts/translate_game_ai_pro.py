#!/usr/bin/env python3
"""
Game AI Pro Pipeline: Automated Download & Gemini 3.8 Flash Multimodal Reconstruction.
Fetches book chapters from https://www.gameaipro.com/, downloads PDF chapters,
processes via Gemini 3.8 Flash multimodal API, and outputs structured OKF Markdown files.
"""

import os
import sys
import re
import time
import json
import base64
import argparse
import traceback
from pathlib import Path
from urllib.parse import urljoin, quote, urlparse, urlunparse
from concurrent.futures import ThreadPoolExecutor, as_completed

# Add local libraries path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".py_libs")))
import pymupdf
import requests

ENDPOINT = "http://127.0.0.1:10100/v1/chat/completions"
MODEL = "google-antigravity/gemini-3.8-flash-high"

PDF_DIR = Path(".kb_work/gameaipro_pdf")
CACHE_DIR = Path(".kb_work/gameaipro_cache")
PDF_DIR.mkdir(parents=True, exist_ok=True)
CACHE_DIR.mkdir(parents=True, exist_ok=True)

BASE_OUT_DIR = Path("读书笔记/GameAIPro")

HTTP_HEADERS = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"
}

VOLUMES_CONFIG = {
    4: {
        "key": "vol4",
        "folder": "卷4-OnlineEdition2021",
        "title": "Game AI Pro 4 (Online Edition 2021)",
        "desc": "Game AI Pro 第4卷（2021在线专著版），汇集现代战术AI、自动化AI测试、并行事件模拟与生产级前沿技术。",
        "pattern": "OnlineEdition2021",
        "tags": ["game-ai", "game-ai-pro", "automated-testing", "tactics-ai", "simulation"]
    },
    3: {
        "key": "vol3",
        "folder": "卷3-GameAIPro3",
        "title": "Game AI Pro 3: Balanced Cuts of Game AI Professionals",
        "desc": "Game AI Pro 第3卷，涵盖复杂行为树、效用系统优化、空间推理、移动规划与3D寻路加速。",
        "pattern": "GameAIPro3",
        "tags": ["game-ai", "game-ai-pro", "behavior-trees", "utility-ai", "pathfinding", "spatial-reasoning"]
    },
    2: {
        "key": "vol2",
        "folder": "卷2-GameAIPro2",
        "title": "Game AI Pro 2: More Collected Wisdom of Game AI Professionals",
        "desc": "Game AI Pro 第2卷，聚焦战术移动、物理感知、动态遮蔽、射击战斗决策与自适应战术协调。",
        "pattern": "GameAIPro2",
        "tags": ["game-ai", "game-ai-pro", "tactical-movement", "combat-ai", "steering", "navigation"]
    },
    1: {
        "key": "vol1",
        "folder": "卷1-GameAIPro1",
        "title": "Game AI Pro 1: Collected Wisdom of Game AI Professionals",
        "desc": "Game AI Pro 系列奠基之作，系统涵盖行为树核心原理、随机性算法、导航网格构建与经典AI决策架构。",
        "pattern": "GameAIPro",
        "tags": ["game-ai", "game-ai-pro", "decision-making", "navmesh", "architecture", "state-machines"]
    }
}


def fetch_site_inventory():
    """Fetch https://www.gameaipro.com/ and return structured chapters mapping by volume."""
    r = requests.get("https://www.gameaipro.com/", headers=HTTP_HEADERS, timeout=30)
    r.encoding = "utf-8"
    html = r.text

    pattern = re.compile(r"<a\s+[^>]*href=[\"\x27]([^\"\x27]+\.pdf)[\"\x27][^>]*>(.*?)</a>", re.I)
    matches = pattern.findall(html)

    inventory = {1: [], 2: [], 3: [], 4: []}

    for href, title in matches:
        clean_title = re.sub(r"<[^>]+>", "", title).strip()
        clean_title = re.sub(r"^\d+\.\s*", "", clean_title) # strip leading number
        full_url = urljoin("https://www.gameaipro.com/", href)
        
        m = re.search(r"Chapter(\d+)", href, re.I)
        if not m:
            continue
        ch_num = int(m.group(1))
        
        if "OnlineEdition2021" in href:
            vol_idx = 4
        elif "GameAIPro3" in href:
            vol_idx = 3
        elif "GameAIPro2" in href:
            vol_idx = 2
        else:
            vol_idx = 1

        filename = href.split("/")[-1]
        inventory[vol_idx].append({
            "ch_num": ch_num,
            "title": clean_title,
            "url": full_url,
            "filename": filename
        })

    for v in inventory:
        inventory[v].sort(key=lambda x: x["ch_num"])

    return inventory


def download_pdf(url, dest_path):
    """Download PDF file if not exists."""
    if dest_path.exists() and dest_path.stat().st_size > 1024:
        return dest_path
    
    p = urlparse(url)
    encoded_url = urlunparse((p.scheme, p.netloc, quote(p.path), p.params, p.query, p.fragment))
    print(f"Downloading: {encoded_url} -> {dest_path.name}")
    r = requests.get(encoded_url, headers=HTTP_HEADERS, timeout=60)
    if r.status_code != 200:
        raise RuntimeError(f"Failed to download {encoded_url}: HTTP {r.status_code}")
    
    dest_path.write_bytes(r.content)
    return dest_path


def extract_pdf_slice(doc, from_page, to_page):
    """Extract page range [from_page, to_page] (1-based) as PDF bytes."""
    sub_doc = pymupdf.open()
    sub_doc.insert_pdf(doc, from_page=from_page - 1, to_page=to_page - 1)
    pdf_bytes = sub_doc.tobytes()
    sub_doc.close()
    return pdf_bytes


def call_gemini_reconstruction(pdf_bytes, prompt_desc, retries=3):
    """Send base64 PDF chunk to Gemini 3.8 Flash for professional AI engineering reconstruction."""
    b64_pdf = base64.b64encode(pdf_bytes).decode("utf-8")
    
    prompt = f"""你是一位享誉全球的顶级游戏 AI 架构师与权威技术专家。
当前研读对象：游戏工业界经典 AI 专著技术文献附件页面（{prompt_desc}）。
请对附件 PDF 页面涵盖的 AI 决策模型、空间搜索、移动规划、系统拓扑、代码实现与工程设计模式进行深入研读，并将其重构为一份详尽、深入、严谨的高级工程技术文档（Markdown 格式）。

【重构与技术规范要求】：
1. 全面深度覆盖：完整解构页面中所有的 AI 算法推导、数据结构、决策树拓扑、底层机理与工业界实战经验，保持工业级深度，不要概括缩写或省略细节；
2. 权威术语中英双解：行为树（Behavior Trees）、效用系统（Utility Systems）、分层任务网络（HTN）、导向行为（Steering Behaviors）、导航网格（NavMesh）、空间推理（Spatial Reasoning）、黑板（Blackboard）等所有专业名词均需标注规范英文；
3. 代码与数学公式：所有代码段、伪代码、数据结构定义原样保留，并保持规范的语法高亮与缩进；所有数学符号与方程必须使用严格规范的 LaTeX 语法（行内公式 $...$，独立公式 $$...$$）；
4. 架构图表全景重构：系统架构框图、状态机流转图、对比表格，请使用清晰的 Markdown 表格、ASCII 文本结构图或结构化分层列表进行完整还原；
5. 层级排版：使用清晰的 Markdown 标题层级（##, ###, ####），直接进入各技术小节与正文论述。

请直接输出重构后的 Markdown 格式技术正文，不要输出任何外部客套话或包裹说明。"""

    payload = {
        "model": MODEL,
        "messages": [
            {
                "role": "user",
                "content": [
                    {"type": "text", "text": prompt},
                    {"type": "image_url", "image_url": {"url": f"data:application/pdf;base64,{b64_pdf}"}}
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
                print(f"[{prompt_desc}] HTTP {res.status_code}: {res.text[:200]}", flush=True)
                time.sleep(3 * (attempt + 1))
                continue
            data = res.json()
            content = data["choices"][0]["message"]["content"].strip()
            
            if content.startswith("```markdown"):
                content = content[len("```markdown"):].strip()
            if content.startswith("```md"):
                content = content[len("```md"):].strip()
            if content.endswith("```"):
                content = content[:-3].strip()

            refusal_keywords = ["抱歉，我无法", "版权保护", "无法对该", "我无法对", "版权要求"]
            is_refused = any(kw in content for kw in refusal_keywords)
            if is_refused or len(content) < 2000:
                print(f"[{prompt_desc}] Warning: content too short ({len(content)} chars) or refused, retrying...", flush=True)
                time.sleep(2 * (attempt + 1))
                continue

            return content
        except Exception as e:
            print(f"[{prompt_desc}] Attempt {attempt + 1} failed: {e}")
            time.sleep(3 * (attempt + 1))

    raise RuntimeError(f"Failed to process {prompt_desc} after {retries} attempts.")


def process_single_chapter(vol_idx, ch, overwrite=False):
    """Process one chapter: download PDF, chunk if needed, translate with Gemini 3.8 Flash, assemble OKF file."""
    vol_info = VOLUMES_CONFIG[vol_idx]
    ch_num = ch["ch_num"]
    title = ch["title"]
    pdf_url = ch["url"]
    filename = ch["filename"]

    pdf_path = PDF_DIR / filename
    download_pdf(pdf_url, pdf_path)

    doc = pymupdf.open(pdf_path)
    total_pages = len(doc)
    
    # If pages <= 10, 1 chunk is sufficient; otherwise split into chunks of 8 pages
    chunk_size = 8
    chunks = []
    curr = 1
    chunk_idx = 1
    while curr <= total_pages:
        chunk_end = min(curr + chunk_size - 1, total_pages)
        chunks.append((chunk_idx, curr, chunk_end))
        curr = chunk_end + 1
        chunk_idx += 1

    chunk_texts = [None] * len(chunks)
    for i, (c_idx, c_start, c_end) in enumerate(chunks):
        cache_key = f"gameaipro_v{vol_idx}_c{ch_num:02d}_chunk{c_idx:02d}_{c_start}_{c_end}.md"
        cache_file = CACHE_DIR / cache_key
        desc = f"{vol_info['title']} Ch {ch_num:02d} ({c_start}-{c_end}/{total_pages}pp)"

        if cache_file.exists() and not overwrite:
            with open(cache_file, "r", encoding="utf-8") as f:
                cached_text = f.read()
            if len(cached_text) >= 800 and "抱歉，我无法" not in cached_text:
                chunk_texts[i] = cached_text
                continue

        pdf_bytes = extract_pdf_slice(doc, c_start, c_end)
        t0 = time.time()
        print(f"[{desc}] Reconstructing via {MODEL}...", flush=True)
        reconstructed_md = call_gemini_reconstruction(pdf_bytes, desc)
        print(f"[{desc}] Success in {time.time() - t0:.1f}s ({len(reconstructed_md)} chars)", flush=True)

        with open(cache_file, "w", encoding="utf-8") as f:
            f.write(reconstructed_md)
        chunk_texts[i] = reconstructed_md

    doc.close()

    # Build OKF file
    out_dir = BASE_OUT_DIR / vol_info["folder"]
    out_dir.mkdir(parents=True, exist_ok=True)
    out_file = out_dir / f"{ch_num:02d}-{slugify(title)}.md"

    tags_formatted = "\n".join([f"  - {tag}" for tag in vol_info["tags"]])

    header = f"""---
type: Reference
title: "第{ch_num}章 {title}"
description: "Game AI Pro 工业级精读：{title}。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
tags:
{tags_formatted}
status: stable
verified: []
maturity: L2
updated: 2026-09-07
---

# 第{ch_num}章 {title}

> 来源：*{vol_info['title']}*, Chapter {ch_num}.  
> 原文作者 / 资源：[{title}]({pdf_url})（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[{vol_info['folder']}](README.md) ｜ [专栏首页](../README.md)

---

"""

    body_parts = []
    for c_text in chunk_texts:
        lines = c_text.splitlines()
        filtered = []
        skip_first = True
        for line in lines:
            if skip_first and line.startswith("# "):
                continue
            skip_first = False
            filtered.append(line)
        body_parts.append("\n".join(filtered).strip())

    full_content = header + "\n\n---\n\n".join(body_parts) + "\n"
    out_file.write_text(full_content, encoding="utf-8")
    print(f"[Done] Chapter {ch_num:02d} written: {out_file} ({len(full_content)} chars)", flush=True)
    return out_file


def slugify(s):
    """Convert title to filename-safe slug."""
    s = re.sub(r"[^\w\s-]", "", s).strip().lower()
    s = re.sub(r"[-\s]+", "-", s)
    return s[:40]


def main():
    parser = argparse.ArgumentParser(description="Game AI Pro Multimodal Translation Pipeline.")
    parser.add_argument("--vol", type=int, choices=[1, 2, 3, 4], help="Volume number (1..4).")
    parser.add_argument("--chapter", type=int, help="Chapter number.")
    parser.add_argument("--start-ch", type=int, help="Start chapter number inclusive.")
    parser.add_argument("--end-ch", type=int, help="End chapter number inclusive.")
    parser.add_argument("--workers", type=int, default=4, help="Concurrency workers (default: 4).")
    parser.add_argument("--overwrite", action="store_true", help="Overwrite cache.")
    args = parser.parse_args()

    inventory = fetch_site_inventory()
    vols_to_run = [args.vol] if args.vol else [4, 3, 2, 1]

    for v in vols_to_run:
        chapters = inventory[v]
        if args.chapter:
            chapters = [c for c in chapters if c["ch_num"] == args.chapter]
        if args.start_ch:
            chapters = [c for c in chapters if c["ch_num"] >= args.start_ch]
        if args.end_ch:
            chapters = [c for c in chapters if c["ch_num"] <= args.end_ch]

        print(f"\n=======================================================", flush=True)
        print(f"Processing Volume {v}: {VOLUMES_CONFIG[v]['title']} ({len(chapters)} chapters)", flush=True)
        print(f"=======================================================", flush=True)

        with ThreadPoolExecutor(max_workers=args.workers) as executor:
            futures = {executor.submit(process_single_chapter, v, c, args.overwrite): c for c in chapters}
            for fut in as_completed(futures):
                try:
                    fut.result()
                except Exception as e:
                    c = futures[fut]
                    print(f"Error processing Vol {v} Ch {c['ch_num']}: {e}", flush=True)
                    traceback.print_exc()


if __name__ == "__main__":
    main()
