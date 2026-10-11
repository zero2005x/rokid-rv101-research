#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Re-generate watermarked videos:
- watermark.png (static, bottom-right)
- "198964天安門" text: digits use a Latin font, CJK uses a CJK font (fix tofu),
  and the text JUMPS to a new random position every T seconds.
"""
import os, random, re, subprocess, sys

FFBIN = "/home/kali/rokid_root/.venv/lib/python3.13/site-packages/imageio_ffmpeg/binaries/ffmpeg-linux-x86_64-v7.0.2"
LATIN_FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"   # has ASCII digits
CJK_FONT   = "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"  # has 天安門
TEXT  = "198964天安門"
WORK  = "/home/kali/rokid_root/wm_work"
T_SEC = 3.0   # text jumps to a new random position every 3 seconds

from PIL import Image, ImageDraw, ImageFont

def even(n):
    n = int(round(n))
    return n if n % 2 == 0 else n + 1

def is_cjk(ch):
    return ord(ch) > 0x2E80

def split_runs(text):
    runs = []
    for ch in text:
        t = "cjk" if is_cjk(ch) else "latin"
        if runs and runs[-1][1] == t:
            runs[-1][0] += ch
        else:
            runs.append([ch, t])
    return runs

def render_text(tag, fontsize, opacity=0.7):
    sw = max(2, fontsize // 16)
    latin = ImageFont.truetype(LATIN_FONT, fontsize)
    cjk   = ImageFont.truetype(CJK_FONT, fontsize)
    runs = split_runs(TEXT)

    # measure total width and ink extents, baseline-align by bottom of ink
    pad = sw + 4
    pieces = []
    total_w = 0
    max_ascent = 0
    max_descent = 0
    for run, typ in runs:
        font = cjk if typ == "cjk" else latin
        asc, desc = font.getmetrics()
        bb = font.getbbox(run)
        if bb is None:
            bb = (0, 0, font.getlength(run), asc+desc)
        w = bb[2] - bb[0]
        pieces.append((run, font, bb))
        total_w += int(round(font.getlength(run)))
        max_ascent = max(max_ascent, asc)
        max_descent = max(max_descent, desc)

    W = even(total_w + 2*pad)
    H = even(max_ascent + max_descent + 2*pad)
    img = Image.new("RGBA", (W, H), (0,0,0,0))
    d = ImageDraw.Draw(img)
    a = int(255*opacity)
    x = pad
    # align each run so its baseline sits at (pad + max_ascent)
    baseline = pad + max_ascent
    for run, font, bb in pieces:
        asc, desc = font.getmetrics()
        # draw with baseline at 'baseline': PIL draws top-left; top = baseline - asc
        y = baseline - asc
        d.text((x, y), run, font=font, fill=(255,255,255,a),
               stroke_width=sw, stroke_fill=(0,0,0,a))
        x += int(round(font.getlength(run)))
    p = os.path.join(WORK, f"text_{tag}.png")
    img.save(p)
    return p, W, H

def prep_watermark(tag, W, opacity=0.55):
    src = os.path.join(WORK, "watermark.png")
    wm = Image.open(src).convert("RGBA")
    w = even(0.28 * W)
    h = even(w * wm.height / wm.width)
    wm = wm.resize((w, h), Image.LANCZOS)
    wm.putalpha(wm.getchannel("A").point(lambda v: int(v*opacity)))
    p = os.path.join(WORK, f"wm_{tag}.png")
    wm.save(p)
    return p, w, h

def probe_duration(video):
    r = subprocess.run([FFBIN, "-hide_banner", "-i", os.path.join(WORK, video)],
                       capture_output=True, text=True)
    m = re.search(r"Duration: (\d+):(\d+):(\d+\.\d+)", r.stderr)
    if not m:
        raise RuntimeError("cannot parse duration:\n" + r.stderr)
    h, mi, s = m.groups()
    return int(h)*3600 + int(mi)*60 + float(s)

def run(video, out, W, H):
    tag = video.split(".")[0][:4]
    mx, my = int(0.03*W), int(0.03*H)
    wp, ww, wh = prep_watermark(tag, W)
    wm_x = W - ww - mx
    wm_y = H - wh - my
    fs = int(0.06 * W)
    tp, tw, th = render_text(tag, fs)

    dur = probe_duration(video)
    N = max(1, int(dur // T_SEC) + (1 if dur % T_SEC > 0.01 else 0))

    # pre-compute N random positions (avoid watermark rect; ensure visible jump)
    rng = random.Random()
    positions = []
    prev = None
    min_jump = 0.20 * min(W, H)
    for i in range(N):
        best = None
        for _ in range(300):
            tx = rng.randint(mx, max(mx, W - tw - mx))
            ty = rng.randint(my, max(my, H - th - my))
            if tx < wm_x + ww and tx + tw > wm_x and ty < wm_y + wh and ty + th > wm_y:
                continue  # overlaps watermark
            if prev is not None:
                dcx = (tx + tw/2) - (prev[0] + tw/2)
                dcy = (ty + th/2) - (prev[1] + th/2)
                if (dcx**2 + dcy**2) ** 0.5 < min_jump:
                    continue  # too close to previous position
            best = (tx, ty)
            break
        if best is None:
            best = (rng.randint(mx, max(mx, W - tw - mx)),
                    rng.randint(my, max(my, H - th - my)))
        positions.append(best)
        prev = best

    print(f"[{tag}] {video}: {W}x{H} dur={dur:.2f}s windows={N} text={tw}x{th} "
          f"wm={ww}x{wh}@({wm_x},{wm_y})", flush=True)
    for i, (x, y) in enumerate(positions):
        print(f"    win{i}: t={i*T_SEC:g}s pos=({x},{y})", flush=True)

    # build filter graph
    parts = ["[1:v]format=rgba[wm]",
             f"[2:v]format=rgba,split={N}" + "".join(f"[t{i}]" for i in range(N)),
             f"[0:v][wm]overlay=x={wm_x}:y={wm_y}[b0]"]
    for i in range(N):
        if i < N - 1:
            en = f"between(t,{i*T_SEC:g},{(i+1)*T_SEC:g})"
        else:
            en = f"gte(t,{i*T_SEC:g})"
        x, y = positions[i]
        outlbl = f"[b{i+1}]" if i < N - 1 else "[out]"
        parts.append(f"[b{i}][t{i}]overlay=x={x}:y={y}:enable='{en}'{outlbl}")
    fc = ";".join(parts)

    cmd = [FFBIN, "-y", "-hide_banner", "-loglevel", "error", "-stats",
           "-i", os.path.join(WORK, video), "-i", wp, "-i", tp,
           "-filter_complex", fc,
           "-map", "[out]", "-map", "0:a",
           "-c:v", "libx264", "-preset", "medium", "-crf", "18",
           "-pix_fmt", "yuv420p", "-c:a", "copy", "-movflags", "+faststart",
           os.path.join(WORK, out)]
    print("  ffmpeg filter_complex:", fc, flush=True)
    r = subprocess.run(cmd)
    if r.returncode != 0:
        print("  FAILED", flush=True)
        sys.exit(r.returncode)
    print(f"  done -> {out}", flush=True)

if __name__ == "__main__":
    run("拍照閃爍一次錄影閃爍兩次.mp4", "拍照閃爍一次錄影閃爍兩次_wm.mp4", 864, 1920)
    run("展示閃爍白紅藍綠四色光.mp4", "展示閃爍白紅藍綠四色光_wm.mp4", 1080, 2400)
    print("ALL DONE", flush=True)
