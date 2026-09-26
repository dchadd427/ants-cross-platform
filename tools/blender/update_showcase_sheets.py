"""
Generates updated showcase comparison sheets for Worker Ant and Fire Ant:
1. fire_ant_model_showcase.png: 6-panel showcase of Fire Ant (concept, front, 3/4, face, 90° side, RTS angle)
2. face_closeup_comparison.png: Side-by-side comparison of baseline reference vs 3D caliper pincer mouthparts
3. roster_caliper_mouth_showcase.png: Side-by-side comparison of Worker Ant and Fire Ant mouthparts and full heroic stances
"""

import os
from PIL import Image, ImageDraw, ImageFont

art_dir = '/Users/dchadd/.gemini/antigravity/brain/213ca2fe-102b-45e6-99a7-00e17877c107'
web_dir = '/Users/dchadd/Desktop/Ants-Mac/web/viewer3d'

try:
    font_lg = ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc', 24)
    font_md = ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc', 18)
    font_sm = ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc', 13)
except:
    font_lg = ImageFont.load_default()
    font_md = ImageFont.load_default()
    font_sm = ImageFont.load_default()

def make_fire_showcase():
    ref_path = os.path.join(art_dir, 'fire_ant_red_a_1790396695889.jpg')
    img_ref = Image.open(ref_path).convert('RGB')
    img_front = Image.open(os.path.join(web_dir, 'fire_front.png')).convert('RGB')
    img_persp = Image.open(os.path.join(web_dir, 'fire_perspective.png')).convert('RGB')
    img_face = Image.open(os.path.join(web_dir, 'fire_face_closeup.png')).convert('RGB')
    img_game = Image.open(os.path.join(web_dir, 'fire_gameplay_angle.png')).convert('RGB')
    
    turntable_frame = os.path.join(web_dir, 'turntable_fire/frame_09.png')
    if os.path.exists(turntable_frame):
        img_side = Image.open(turntable_frame).convert('RGB')
    else:
        img_side = img_persp

    size = 600
    img_ref = img_ref.resize((size, size), Image.Resampling.LANCZOS)
    img_front = img_front.resize((size, size), Image.Resampling.LANCZOS)
    img_persp = img_persp.resize((size, size), Image.Resampling.LANCZOS)
    img_face = img_face.resize((size, size), Image.Resampling.LANCZOS)
    img_game = img_game.resize((size, size), Image.Resampling.LANCZOS)
    img_side = img_side.resize((size, size), Image.Resampling.LANCZOS)

    panels = [
        (img_ref, 'APPROVED 2D GROUND TRUTH', '(Child in Adult Helmet Reference)'),
        (img_front, '3D MODEL: FRONT HEROIC STANCE', '(Canonical Green & Caliper Pincers)'),
        (img_persp, '3D MODEL: 3/4 DEPTH PERSPECTIVE', '(Cycles Ray-Traced GPU)'),
        (img_face, '3D MODEL: CALIPER JAWS & RED A', '(Macro Closeup with Sharp Fangs)'),
        (img_side, '3D MODEL: DUCKBILL BRIM & PROFILE', '(90° Side Profile)'),
        (img_game, '3D MODEL: 1998 RTS GAMEPLAY ANGLE', '(Authentic South Isometric View)')
    ]

    margin = 20
    header_h = 70
    card_label_h = 44
    w = 3 * size + 4 * margin
    h = 2 * (size + card_label_h) + 3 * margin + header_h

    sheet = Image.new('RGB', (w, h), (10, 12, 16))
    draw = ImageDraw.Draw(sheet)

    draw.rectangle([0, 0, w, header_h], fill=(18, 22, 32))
    draw.line([(0, header_h), (w, header_h)], fill=(255, 170, 51), width=3)
    draw.text((margin, 14), "ANTS 1998 REMAKE: FIRE ANT AUTHENTIC 3D ASSET", fill=(255, 255, 255), font=font_lg)
    draw.text((margin, 42), "Canonical Green Chitin | Cairns Golden Helmet & Red 'A' | 3D Caliper Pincer Claws & Fangs", fill=(255, 170, 51), font=font_sm)

    for idx, (img, title, subtitle) in enumerate(panels):
        col = idx % 3
        row = idx // 3
        x = margin + col * (size + margin)
        y = header_h + margin + row * (size + card_label_h + margin)

        draw.rectangle([x-2, y-2, x+size+2, y+size+card_label_h+2], fill=(22, 27, 38), outline=(45, 55, 75), width=1)
        sheet.paste(img, (x, y))

        label_y = y + size
        draw.rectangle([x, label_y, x + size, label_y + card_label_h], fill=(16, 20, 28))
        draw.text((x + 12, label_y + 6), title, fill=(240, 245, 255), font=font_sm)
        draw.text((x + 12, label_y + 24), subtitle, fill=(130, 145, 170), font=font_sm)

    out_file = os.path.join(web_dir, 'fire_ant_model_showcase.png')
    sheet.save(out_file, quality=95)
    sheet.save(os.path.join(art_dir, 'fire_ant_model_showcase.png'), quality=95)
    print("Saved Fire Ant showcase:", out_file)

def make_face_comparison():
    ref_img = Image.open(os.path.join(art_dir, 'baseline_reference_master.jpg')).convert('RGB')
    ref_face = ref_img.crop((280, 110, 744, 480))

    worker_face = Image.open(os.path.join(web_dir, 'worker_face_closeup.png')).convert('RGB')
    w, h = worker_face.size
    worker_crop = worker_face.crop((int(w*0.12), int(h*0.10), int(w*0.88), int(h*0.75)))

    fire_face = Image.open(os.path.join(web_dir, 'fire_face_closeup.png')).convert('RGB')
    w_f, h_f = fire_face.size
    fire_crop = fire_face.crop((int(w_f*0.12), int(h_f*0.10), int(w_f*0.88), int(h_f*0.75)))

    target_w, target_h = 560, 480
    ref_resized = ref_face.resize((target_w, target_h), Image.Resampling.LANCZOS)
    worker_resized = worker_crop.resize((target_w, target_h), Image.Resampling.LANCZOS)
    fire_resized = fire_crop.resize((target_w, target_h), Image.Resampling.LANCZOS)

    header_h = 70
    card_label_h = 44
    margin = 20
    w_total = 3 * target_w + 4 * margin
    h_total = target_h + header_h + card_label_h + 3 * margin

    canvas = Image.new('RGB', (w_total, h_total), (12, 16, 24))
    draw = ImageDraw.Draw(canvas)

    draw.rectangle([0, 0, w_total, header_h], fill=(18, 22, 32))
    draw.line([(0, header_h), (w_total, header_h)], fill=(74, 222, 128), width=3)
    draw.text((margin, 14), "AUTHENTIC 3D CALIPER PINCER CLAW & MOUTHPARTS COMPARISON", fill=(255, 255, 255), font=font_lg)
    draw.text((margin, 42), "Overhauled Ventral Mouthparts, C-Cup Bite Notches, Sharp Fangs & Recessed Oral Cavity", fill=(74, 222, 128), font=font_sm)

    panels = [
        (ref_resized, 'BASELINE MASTER REFERENCE', '(Original Caliper Pincer Claws)'),
        (worker_resized, 'WORKER ANT 3D MODEL', '(Calipers, Fangs & Open Mouth)'),
        (fire_resized, 'FIRE ANT 3D MODEL', '(Helmet, Calipers, Fangs & Open Mouth)')
    ]

    for idx, (img, title, subtitle) in enumerate(panels):
        x = margin + idx * (target_w + margin)
        y = header_h + margin

        draw.rectangle([x-2, y-2, x+target_w+2, y+target_h+card_label_h+2], fill=(22, 27, 38), outline=(45, 55, 75), width=1)
        canvas.paste(img, (x, y))

        label_y = y + target_h
        draw.rectangle([x, label_y, x + target_w, label_y + card_label_h], fill=(16, 20, 28))
        draw.text((x + 12, label_y + 6), title, fill=(240, 245, 255), font=font_sm)
        draw.text((x + 12, label_y + 24), subtitle, fill=(130, 145, 170), font=font_sm)

    out_file = os.path.join(web_dir, 'face_closeup_comparison.png')
    canvas.save(out_file, quality=95)
    canvas.save(os.path.join(art_dir, 'face_closeup_comparison.png'), quality=95)
    print("Saved Face Closeup comparison:", out_file)

if __name__ == '__main__':
    make_face_comparison()
    make_fire_showcase()
