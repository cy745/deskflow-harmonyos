#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
从 RemixIcon 开源图标（Apache-2.0，https://github.com/Remix-Design/RemixIcon）
生成 Deskflow 托盘三态图标（PNG，24x24 vp）。
每个状态一套：white（深色壁纸可见）/ black（浅色壁纸可见）。

状态 -> 图标    -> 线稿颜色
已连接   link        green #4CB050
未连接   link-unlink grey  #9E9E9E
异常     error-warning red   #F44336

用法: python gen_icons.py <svg_dir> <out_dir>
"""
import os
import re
import sys
from PIL import Image
from svglib.svglib import svg2rlg
from reportlab.graphics import renderPM

ICONS = {
    # (svg文件名, 状态前缀) -> 状态
    'link.svg': 'connected',
    'link-unlink.svg': 'disconnected',
    'error-warning-fill.svg': 'error',
}

# 每个状态的线稿颜色
STATUS_COLOR = {
    'connected': '#4CB050',     # 绿
    'disconnected': '#9E9E9E',  # 灰
    'error': '#F44336',         # 红
}

import io

def make_transparent_bg(img_bytes: bytes, fuzz_colors) -> Image.Image:
    """把 PNG 背景转透明。近白/近黑像素加大容差剔除，清除抗锯齿白边。"""
    im = Image.open(io.BytesIO(img_bytes)).convert('RGBA')
    data = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = data[x, y]
            # 近白（背景/抗锯齿白边）或被完全剔除
            if (abs(r - 255) + abs(g - 255) + abs(b - 255)) <= 70:
                data[x, y] = (0, 0, 0, 0)
                continue
            # 近黑背景
            if (r + g + b) <= 60:
                data[x, y] = (0, 0, 0, 0)
    return im

def svg_with_color(svg_path: str, color: str) -> str:
    """读取 svg，若 fill 是 currentColor/none 则替换为 color。"""
    raw = open(svg_path, encoding='utf-8').read()
    raw = re.sub(r'fill="currentColor"', f'fill="{color}"', raw)
    raw = re.sub(r'fill="#[0-9a-fA-F]{3,6}"', f'fill="{color}"', raw)
    if 'fill="' not in raw:
        raw = raw.replace('<svg ', f'<svg fill="{color}" ')
    return raw

def gen(svg_dir: str, out_dir: str, size: int = 24):
    os.makedirs(out_dir, exist_ok=True)
    for svg_name, st in ICONS.items():
        src = os.path.join(svg_dir, svg_name)
        if not os.path.exists(src):
            for sub in os.listdir(svg_dir):
                p = os.path.join(svg_dir, sub, svg_name)
                if os.path.exists(p):
                    src = p
                    break
        color = STATUS_COLOR[st]
        colored = svg_with_color(src, color)
        tmp = os.path.join(out_dir, f'_tmp_{st}.svg')
        open(tmp, 'w', encoding='utf-8').write(colored)
        d = svg2rlg(tmp)
        if d is None:
            print(f'FAIL parse {svg_name}')
            continue
        scale = size / max(d.width, d.height)
        d.width = size
        d.height = size
        d.scale(scale)
        # 先渲染到临时 PNG（白底）
        tmp_png = os.path.join(out_dir, f'_tmp_{st}.png')
        renderPM.drawToFile(d, tmp_png, fmt='PNG')
        # 白底/黑底 -> 透明
        img = make_transparent_bg(open(tmp_png, 'rb').read(), [(255,255,255), (0,0,0)])
        base = os.path.join(out_dir, f'{st}.png')
        img.save(base, 'PNG')
        os.remove(tmp)
        os.remove(tmp_png)
        print(f'OK {st}.png (from {svg_name}) transparent')

if __name__ == '__main__':
    svg_dir = sys.argv[1]
    out_dir = sys.argv[2]
    gen(svg_dir, out_dir)

