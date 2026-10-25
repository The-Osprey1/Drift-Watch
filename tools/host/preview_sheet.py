"""Native renderer output to a compact gallery and animation review."""
from pathlib import Path
import sys
from PIL import Image, ImageDraw, ImageFont

out = Path(sys.argv[1])
names = ["DIGITAL","STEALTH","CORPO","HOLOGRAM","RADAR","Phantom",
         "Orbit","Lunar","Yozakura","Hanami","Wasteland","Mojave"]
try:
    font = ImageFont.truetype("DejaVuSans.ttf", 16)
except OSError:
    font = ImageFont.load_default()
mask = Image.new("L", (240,240))
ImageDraw.Draw(mask).ellipse((0,0,239,239), fill=255)
def tile(name, index):
    src = Image.open(out / f"preview_{name}_{index:03}.ppm").convert("RGB")
    im = Image.new("RGB", (240,240), "#111318")
    im.paste(src,(0,0),mask)
    return im

sheet = Image.new("RGB",(1060,840),"#111318")
draw=ImageDraw.Draw(sheet)
for i,name in enumerate(names):
    x=20+(i%4)*260; y=16+(i//4)*278
    sheet.paste(tile(name,0),(x,y))
    draw.text((x,y+244),name,font=font,fill="#ccd1da")
sheet.save(out/"precision-faces.png")
animated=["RADAR","Phantom","Orbit","Lunar","Yozakura","Hanami","Wasteland","Mojave"]
frames=[]
for i in range(1,241):
    im=Image.new("RGB",(1040,536),"#111318")
    d=ImageDraw.Draw(im)
    for k,name in enumerate(animated):
        x=10+(k%4)*260; y=8+(k//4)*268
        im.paste(tile(name,i),(x,y))
        d.text((x,y+242),name,font=font,fill="#ccd1da")
    frames.append(im)
frames[0].save(out/"precision-motion.gif",save_all=True,append_images=frames[1:],
               duration=50,loop=0,optimize=False)

# Representative states expose the eclipse and finite entrance choreography.
states=Image.new("RGB",(1060,1120),"#111318")
d=ImageDraw.Draw(states)
for row,(name,indices,labels) in enumerate([
    ("RADAR",[1,19,37,55],["0 ms","900 ms","1800 ms","2700 ms"]),
    ("Phantom",[1,81,181,228],["54 s / visible","58 s / hidden","3 s / returning","5.35 s / settled"]),
    ("Orbit",[1,4,7,11],["0 ms","150 ms","300 ms","500 ms"]),
    ("Yozakura",[1,7,13,19],["0 ms","300 ms","600 ms","900 ms"])]):
    for col,(i,label) in enumerate(zip(indices,labels)):
        x=20+col*260; y=16+row*278
        states.paste(tile(name,i),(x,y))
        d.text((x,y+244),f"{name} / {label}",font=font,fill="#ccd1da")
states.save(out/"precision-motion-states.png")

# Opt-in transport for inspecting native previews when artifact downloads are unavailable.
if "--log-preview" in sys.argv:
    import base64
    for name in ["precision-faces.png","precision-motion-states.png"]:
        print("PREVIEW_PNG_BEGIN "+name)
        data=base64.b64encode((out/name).read_bytes()).decode()
        for i in range(0,len(data),1000):
            print(data[i:i+1000])
        print("PREVIEW_PNG_END")
