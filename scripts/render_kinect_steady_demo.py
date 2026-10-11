#!/usr/bin/env python3
"""Draws tests/kinect_steady_demo's output side by side: the Kinect skeleton
as the sensor gave it (left, steadying off) and as the game is given it
(right, on), with the menu cursor's trail in the menu scenes. Writes a GIF.

usage: kinect_steady_demo | python scripts/render_kinect_steady_demo.py OUT.gif
"""
import json
import sys
from PIL import Image, ImageDraw, ImageFont

BONES = [(0, 1), (1, 2), (2, 3), (2, 4), (4, 5), (5, 6), (6, 7), (2, 8), (8, 9), (9, 10), (10, 11),
         (0, 12), (12, 13), (13, 14), (14, 15), (0, 16), (16, 17), (17, 18), (18, 19)]
RIGHT_ARM = {9, 10, 11}
W, H, PANEL = 1100, 640, 520
BG, FG, DIM = (19, 24, 33), (221, 228, 238), (150, 164, 182)
OFF, ON, GUESS, HELD = (255, 150, 130), (128, 225, 167), (255, 205, 119), (70, 207, 236)


def font(size):
    for name in ('/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc', 'C:/Windows/Fonts/msjh.ttc', 'DejaVuSans.ttf'):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


SCENES = {
    'arms crossed in a race': '比賽中雙手交叉（右前臂被擋住）',
    'right hand behind the back': '右手放到背後',
    'fast punch (must not lag)': '快速出拳（不能有延遲）',
    'menu: hand held on a button': '選單：手停在按鈕上',
    'menu: slow move, then hold': '選單：慢慢移到下一個按鈕再停住',
}


def main():
    frames = [json.loads(line) for line in sys.stdin if line.strip()]
    big, mid, small = font(22), font(17), font(14)
    trails = {}
    images = []
    last_scene = None
    for f in frames:
        if f['scene'] != last_scene:
            trails = {'given': [], 'out': [], 'given_origin': None, 'out_origin': None}
            last_scene = f['scene']
        im = Image.new('RGB', (W, H), BG)
        d = ImageDraw.Draw(im)
        d.text((20, 14), SCENES.get(f['scene'], f['scene']), font=big, fill=FG)
        d.text((W - 230, 18), '示意模擬（同一段資料）', font=small, fill=DIM)
        for side, (key, title, colour) in enumerate([('given', '關：Kinect 原樣', OFF), ('out', '開：遮擋保持＋游標穩定', ON)]):
            left = 20 + side * (PANEL + 20)
            top = 56
            d.rectangle([left, top, left + PANEL, top + H - 76], outline=(45, 54, 70), width=2)
            d.text((left + 12, top + 8), title, font=mid, fill=colour)
            scale = 230
            cx = left + (150 if f['menu'] else PANEL / 2)

            def place(p):
                return (cx + p[0] * scale, top + 50 + (0.8 - p[1]) * scale)

            joints = f[key]
            given = f['given']
            for a, b in BONES:
                guessed = given[a][3] < 2 or given[b][3] < 2
                moved = key == 'out' and any(abs(joints[j][k] - given[j][k]) > 1e-3 for j in (a, b) for k in range(2))
                col = HELD if moved else (GUESS if guessed else FG)
                d.line([place(joints[a]), place(joints[b])], fill=col, width=4)
            for j, p in enumerate(joints):
                guessed = given[j][3] < 2
                moved = key == 'out' and any(abs(p[k] - given[j][k]) > 1e-3 for k in range(2))
                r = 5
                x, y = place(p)
                d.ellipse([x - r, y - r, x + r, y + r], fill=HELD if moved else (GUESS if guessed else FG))
            if f['menu']:
                # The cursor follows the hand on the shoulders; drawn 7 pixels
                # a centimetre (the title moves it about 26).
                hand, shoulder = joints[11], joints[2]
                cursor = (hand[0] - shoulder[0], hand[1] - shoulder[1])
                if not trails[key]:
                    trails[key + '_origin'] = cursor
                trails[key].append(cursor)
                trails[key] = trails[key][-45:]
                box = (left + 270, top + 90, left + PANEL - 14, top + 330)
                d.rectangle(box, outline=(60, 70, 90))
                d.text((box[0] + 6, box[1] + 4), '游標軌跡（1 公分＝7 像素）', font=small, fill=DIM)
                origin = trails[key + '_origin']
                ox, oy = box[0] + (box[2] - box[0]) * 0.7, (box[1] + box[3]) / 2 + 10
                pts = [(ox + (c[0] - origin[0]) * 700, oy - (c[1] - origin[1]) * 700) for c in trails[key]]
                pts = [(min(max(x, box[0] + 2), box[2] - 2), min(max(y, box[1] + 22), box[3] - 2)) for x, y in pts]
                if len(pts) > 1:
                    d.line(pts, fill=colour, width=2)
                x, y = pts[-1]
                d.ellipse([x - 7, y - 7, x + 7, y + 7], outline=colour, width=3)
        legend = [('白：追蹤中', FG), ('黃：Kinect 推測（被擋住）', GUESS), ('藍：被固定／穩定的點', HELD)]
        x = 20
        for text, col in legend:
            d.text((x, H - 18), text, font=small, fill=col)
            x += 230
        images.append(im)
    images[0].save(sys.argv[1], save_all=True, append_images=images[1:], duration=33, loop=0, optimize=True)


if __name__ == '__main__':
    main()
