"""Generate a local text/edge fixture and inspection panels (Pillow required)."""
from pathlib import Path
import sys
from PIL import Image, ImageDraw, ImageFont

folder = Path(sys.argv[1])
folder.mkdir(parents=True, exist_ok=True)
font_path = 'C:/Windows/Fonts/simsun.ttc'
if sys.argv[2] == 'prepare':
    im = Image.new('RGB', (1280, 720), '#303848')
    d = ImageDraw.Draw(im)
    for top, bg, fg in [(0, '#f4f0dc', '#101010'), (150, '#18243c', '#ffffff')]:
        d.rectangle((0, top, 1279, top+149), fill=bg)
        for i, size in enumerate([12, 14, 16, 20]):
            d.text((16, top+8+i*33), '冒险岛 北斗 任务说明：攻击力 +128  经验值 123456789  MapleStory',
                   font=ImageFont.truetype(font_path, size), fill=fg)
    for x in range(16, 400, 8):
        d.line((x, 340, x+220, 650), fill='white', width=1)
    for x in range(600, 950, 4):
        for y in range(350, 650, 4):
            d.rectangle((x, y, x+3, y+3), fill='white' if (x+y)//4 % 2 else 'black')
    im.save(folder/'input.png')
    (folder/'input.bgra').write_bytes(im.convert('RGBA').tobytes('raw', 'BGRA'))
elif sys.argv[2] == 'compare':
    source = Image.open(folder/'input.png')
    outputs = {'Nearest': source.resize((2560,1440), Image.Resampling.NEAREST)}
    for name in ['linear', 'fast', 'balanced']:
        outputs[name] = Image.frombytes('RGBA', (2560,1440), (folder/(name+'.bgra')).read_bytes(), 'raw', 'BGRA').convert('RGB')
        outputs[name].save(folder/(name+'.png'))
    panel = Image.new('RGB', (1500, 4*320), 'white')
    d = ImageDraw.Draw(panel)
    for i, (name, im) in enumerate(outputs.items()):
        d.text((8, i*320+8), name, fill='black', font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 22))
        panel.paste(im.crop((20,0,1520,286)), (0,i*320+34))
    panel.save(folder/'comparison.png')
    # The game's current window uses a fractional ratio. Inspect that separately
    # from 2x, where nearest-neighbor pixels have uniform widths by construction.
    fractional = {
        'Nearest 1.5x': source.resize((1920,1080), Image.Resampling.NEAREST),
        'Single-pass linear 1.5x': Image.frombytes('RGBA', (1920,1080),
            (folder/'linear-1920x1080.bgra').read_bytes(), 'raw', 'BGRA').convert('RGB'),
        'CuNNy balanced 1.5x': Image.frombytes('RGBA', (1920,1080),
            (folder/'1920x1080.bgra').read_bytes(), 'raw', 'BGRA').convert('RGB')}
    panel = Image.new('RGB', (1200, 3*260), 'white')
    d = ImageDraw.Draw(panel)
    for i, (name, im) in enumerate(fractional.items()):
        d.text((8,i*260+5), name, fill='black', font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',20))
        panel.paste(im.crop((15,0,1215,224)), (0,i*260+32))
    panel.save(folder/'comparison-fractional.png')
