#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
从 RemixIcon SVG 生成 Deskflow 托盘图标（官方样例同款 SVG 结构）。
与 DesktopExtensionKit-samplecode 的 testWhite.svg 一致：
  - <defs><path id="path-1" d="..."/></defs>
  - <g fill="none" fill-rule="evenodd"> + <mask fill="white"> + <use href>
  - 主体 <use fill="#颜色" xlink:href>
ImageKit 的 createImageSource 原生支持该标准 SVG。
用法: python gen_tray_svg.py <remixicon_icons_dir> <out_dir>
"""
import os
import re
import sys

SHAPES = {
    'connected': 'link-m.svg',
    'disconnected': 'link-unlink-m.svg',
    'error': 'error-warning-fill.svg',
}
COLORS = {
    'white': '#FFFFFF',  # 深色壁纸
    'black': '#000000',  # 浅色壁纸
}

def find_svg(root, name):
    for dirpath, _, files in os.walk(root):
        if name in files:
            return os.path.join(dirpath, name)
    raise FileNotFoundError(name)

def remix_path_d(raw):
    """提取 RemixIcon 的 path d 属性。"""
    m = re.search(r'<path\b[^>]*\bd="([^"]*)"', raw)
    if m:
        return m.group(1)
    raise ValueError('no path d in svg')

def build_svg(path_d, color):
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<svg width="30px" height="30px" viewBox="0 0 24 24" version="1.1" '
        'xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">\n'
        '    <title>deskflow_status_icon</title>\n'
        '    <defs>\n'
        f'        <path d="{path_d}" id="path-1"></path>\n'
        '    </defs>\n'
        '    <g id="deskflow_icon" stroke="none" stroke-width="1" fill="none" fill-rule="evenodd">\n'
        '        <mask id="mask-2" fill="white">\n'
        '            <use xlink:href="#path-1"></use>\n'
        '        </mask>\n'
        f'        <use id="shape" fill="{color}" xlink:href="#path-1"></use>\n'
        '    </g>\n'
        '</svg>\n'
    )

def gen(remix_dir, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    for shape, svg_name in SHAPES.items():
        src = find_svg(remix_dir, svg_name)
        d = remix_path_d(open(src, encoding='utf-8').read())
        for theme, color in COLORS.items():
            svg = build_svg(d, color)
            out = os.path.join(out_dir, f'{shape}_{theme}.svg')
            with open(out, 'w', encoding='utf-8') as f:
                f.write(svg)
            print(f'OK {os.path.basename(out)} ({color}) from {svg_name}')

if __name__ == '__main__':
    gen(sys.argv[1], sys.argv[2])
