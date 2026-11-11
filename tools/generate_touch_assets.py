#!/usr/bin/env python3
"""Build the touch-control PNGs from the generated plate and original glyphs.

Run through scripts/worksafe.py; requires Pillow. Does not modify the game data.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'android/app/src/main/assets/touch_hud'
DESIGN = ROOT / 'design/touch_hud'
S = 256
INK = (238, 242, 240, 255)


def glyph(name):
    im = Image.new('RGBA', (S, S))
    d = ImageDraw.Draw(im)
    def line(points, width=5, color=INK):
        pts = [(round(x*2), round(y*2)) for x,y in points]
        d.line(pts, fill=color, width=width*2, joint='curve')
        r = width
        for x,y in (pts[0], pts[-1]): d.ellipse((x-r,y-r,x+r,y+r),fill=color)
    def ellipse(box, width=5, fill=None):
        d.ellipse(tuple(int(v*2) for v in box), outline=INK, width=width*2, fill=fill)
    def rect(box, width=5, fill=None):
        d.rounded_rectangle(tuple(int(v*2) for v in box),radius=5*2,outline=INK,width=width*2,fill=fill)
    def arrow(direction):
        if direction=='up': line([(45,69),(64,50),(83,69)])
        if direction=='down': line([(45,59),(64,78),(83,59)])
        if direction=='left': line([(69,45),(50,64),(69,83)])
        if direction=='right': line([(59,45),(78,64),(59,83)])
    def person():
        ellipse((57,34,70,47),4); line([(63,49),(58,67),(70,74)],5)
    if name in ('nav_up','nav_down','nav_left','nav_right','action_previous','action_next'):
        arrow({'action_previous':'up','action_next':'down'}.get(name,name[4:]))
    elif name=='fire':
        ellipse((43,43,85,85),4); ellipse((59,59,69,69),3)
        for a,b in [((64,32),(64,45)),((64,83),(64,96)),((32,64),(45,64)),((83,64),(96,64))]: line([a,b],4)
    elif name=='action':
        line([(45,77),(45,59),(49,55),(53,58),(53,39),(57,36),(61,39),(61,57),(65,45),(69,46),(69,58),(73,50),(77,52),(77,64),(81,60),(85,63),(82,78),(75,87),(56,87),(45,77)],4)
    elif name in ('run','run_toggle'):
        ellipse((64,31,76,43),4); line([(64,48),(55,58),(69,65),(78,54)],5); line([(63,49),(75,49),(84,57)],5); line([(61,64),(51,77),(42,80)],5); line([(69,65),(77,78),(88,82)],5)
        if name=='run_toggle': line([(38,48),(48,48)],3); line([(35,56),(45,56)],3)
    elif name in ('crouch','sneak'):
        person(); line([(58,67),(48,74),(60,80),(73,80)],5); line([(70,74),(82,77),(88,86)],5)
        if name=='sneak': line([(37,89),(52,89)],3); line([(76,91),(91,91)],3)
    elif name in ('lean_left','lean_right'):
        # Draw once and mirror for the opposite direction.
        ellipse((52,33,64,45),4); line([(59,48),(68,64),(65,82)],5); line([(63,55),(49,64)],5); line([(65,82),(55,91)],5); line([(65,82),(77,91)],5); line([(43,45),(34,54),(43,63)],4)
        if name=='lean_right': im=ImageOps.mirror(im)
    elif name in ('inventory','quick_inventory'):
        rect((40,45,88,87),4); line([(53,45),(53,39),(75,39),(75,45)],4); line([(40,61),(88,61)],4); line([(60,58),(68,58),(68,66),(60,66),(60,58)],3)
        if name=='quick_inventory': line([(36,34),(48,34)],3); line([(32,40),(43,40)],3)
    elif name in ('holster','pistol','smg','rifle'):
        if name in ('holster','pistol'):
            line([(41,49),(82,49),(82,60),(66,60),(61,79),(49,79),(53,60),(41,60),(41,49)],4); line([(69,61),(68,67),(61,67)],3)
            if name=='holster': line([(39,88),(89,88)],4)
        else:
            line([(34,51),(87,51),(87,57),(70,57),(69,65),(61,65),(58,57),(45,57),(39,66),(33,65),(38,55)],4)
            line([(87,54),(96,54)],4); line([(62,66),(59,80),(70,80),(73,66)],4)
            if name=='rifle': line([(34,51),(25,44),(26,61),(35,61)],4); line([(74,46),(83,46)],3)
    elif name=='reload':
        d.arc((80,80,176,176),40,300,fill=INK,width=10); line([(79,40),(88,40),(88,51)],4); line([(49,75),(49,53),(62,45),(75,53),(75,75),(62,83),(49,75)],4)
    elif name=='drop':
        rect((49,36,79,61),4); line([(64,67),(64,86)],4); line([(55,77),(64,86),(73,77)],4); line([(44,93),(84,93)],4)
    elif name=='map':
        line([(37,46),(54,39),(74,46),(91,39),(91,82),(74,89),(54,82),(37,89),(37,46)],4); line([(54,39),(54,82)],3); line([(74,46),(74,89)],3)
    elif name=='briefing':
        rect((43,36,85,92),4); line([(53,51),(75,51)],4); line([(53,62),(75,62)],4); line([(53,73),(69,73)],4)
    elif name=='camera':
        rect((37,49,91,83),4); line([(47,49),(51,42),(68,42),(72,49)],4); ellipse((54,55,75,76),4)
    elif name in ('zoom_in','zoom_out'):
        ellipse((39,38,75,74),4); line([(70,70),(89,89)],5); line([(48,56),(66,56)],4)
        if name=='zoom_in': line([(57,47),(57,65)],4)
    elif name=='pause':
        rect((44,41,54,87),3,INK); rect((74,41,84,87),3,INK)
    elif name=='accept': line([(41,64),(57,79),(88,47)],6)
    elif name=='melee':
        line([(45,86),(75,56)],6); line([(69,50),(79,60)],4); line([(75,52),(88,35),(85,50),(79,57)],4); line([(41,81),(50,90)],4)
    elif name in ('binoculars','night_vision'):
        ellipse((36,61,59,84),4); ellipse((69,61,92,84),4); line([(39,64),(45,43),(57,43),(59,69)],4); line([(69,69),(71,43),(83,43),(89,64)],4); line([(59,64),(69,64)],4)
        if name=='night_vision': line([(62,34),(66,34)],4); line([(64,28),(64,40)],3)
    elif name=='lockpick':
        line([(43,88),(43,48),(47,40),(53,40),(57,47)],4); line([(57,47),(64,56),(76,56),(85,45)],4); line([(58,87),(58,70),(81,70)],4)
    elif name=='camera_drag':
        ellipse((51,51,77,77),4); line([(64,33),(64,48)],4); line([(64,80),(64,95)],4); line([(33,64),(48,64)],4); line([(80,64),(95,64)],4)
        line([(58,39),(64,33),(70,39)],3); line([(58,89),(64,95),(70,89)],3); line([(39,58),(33,64),(39,70)],3); line([(89,58),(95,64),(89,70)],3)
    return im


NAMES = ('fire action action_previous action_next run run_toggle crouch sneak lean_left lean_right '
         'inventory quick_inventory holster reload drop map briefing camera zoom_in zoom_out pause accept '
         'nav_up nav_down nav_left nav_right melee pistol smg rifle binoculars night_vision lockpick camera_drag').split()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    plate = Image.open(DESIGN / 'button_plate.png').convert('RGBA').resize((S,S), Image.Resampling.LANCZOS)
    for name in NAMES:
        for state in ('normal','pressed'):
            frame = plate.copy()
            if state=='pressed':
                tint = Image.new('RGBA', (S,S), (190,18,35,0))
                tint.putalpha(frame.getchannel('A').point(lambda v: int(v * .36)))
                frame = Image.alpha_composite(frame, tint)
            frame = Image.alpha_composite(frame, glyph(name))
            frame.save(OUT / (name + '_' + state + '.png'), optimize=True)
    # Separate parts allow the stick to be displaced by the touch logic.
    plate.save(OUT / 'joystick_base.png', optimize=True)
    knob = Image.new('RGBA',(S,S)); dr=ImageDraw.Draw(knob)
    dr.ellipse((58,58,198,198),fill=(35,40,44,205),outline=(206,213,211,225),width=5)
    dr.ellipse((74,74,182,182),outline=(127,139,139,150),width=2)
    knob.save(OUT / 'joystick_knob.png',optimize=True)
    font = ImageFont.load_default(size=15)
    w, h, cols = 180, 166, 6
    preview=Image.new('RGB',(w*cols, h*((len(NAMES)+cols-1)//cols)),(22,26,30))
    draw=ImageDraw.Draw(preview)
    for i,name in enumerate(NAMES):
        x=(i%cols)*w; y=(i//cols)*h
        icon=Image.open(OUT / (name+'_normal.png')).resize((115,115),Image.Resampling.LANCZOS)
        preview.paste(icon,(x+32,y+8),icon)
        box=draw.textbbox((0,0),name,font=font)
        draw.text((x+(w-(box[2]-box[0]))/2,y+128),name,font=font,fill=(225,228,226))
    DESIGN.mkdir(parents=True, exist_ok=True)
    preview.save(DESIGN / 'preview.jpg',quality=90)
    print(f'{len(NAMES)} buttons x 2 states + joystick components: {OUT}')

if __name__=='__main__': main()
