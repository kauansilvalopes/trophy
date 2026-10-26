from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import struct,hashlib,math
root=Path(__file__).resolve().parent.parent
source=root/'third_party/inter/Inter-Medium.ttf'
entries=[];data=bytearray()
for size in [24,28,32,44,64]:
 font=ImageFont.truetype(str(source),size)
 for code in list(range(32,127))+list(range(160,592))+[0x2013,0x2014,0x2018,0x2019,0x201c,0x201d,0x2122,0x2605,0x2026,0x2022,0x2190,0x2192,0x2713,0x25b3,0x25cb,0x25a1]:
  char=chr(code);l,t,rr,b=font.getbbox(char,anchor='ls');w,h=rr-l,b-t
  mask=Image.new('L',(max(w,1),max(h,1)));ImageDraw.Draw(mask).text((-l,-t),char,font=font,fill=255,anchor='ls')
  if code in [0x25b3,0x25cb,0x25a1,0xd7]:
   w=h=size;l=0;t=-size;mask=Image.new('L',(w,h));d=ImageDraw.Draw(mask);a=max(2,size//8);z=size-a-1;stroke=max(2,size//14)
   if code==0x25b3:d.line([(size//2,a),(z,z),(a,z),(size//2,a)],fill=255,width=stroke)
   if code==0x25cb:d.ellipse((a,a,z,z),outline=255,width=stroke)
   if code==0x25a1:d.rectangle((a,a,z,z),outline=255,width=stroke)
   if code==0xd7:d.line((a,a,z,z),fill=255,width=stroke);d.line((z,a,a,z),fill=255,width=stroke)
  if code==0x2605:
   w=h=size;l=0;t=-size;mask=Image.new('L',(w,h));d=ImageDraw.Draw(mask)
   points=[(size/2+math.cos(-math.pi/2+i*math.pi/5)*(size*.44 if i%2==0 else size*.2),size/2+math.sin(-math.pi/2+i*math.pi/5)*(size*.44 if i%2==0 else size*.2)) for i in range(10)]
   d.polygon(points,fill=255)
  raw=mask.tobytes() if w*h else b'' 
  entries.append((code,size,len(data),l,t,w,h,size if code in [0x25b3,0x25cb,0x25a1,0xd7] else round(font.getlength(char))));data.extend(raw)
result=struct.pack('<II',0x46545345,len(entries))+b''.join(struct.pack('<IIIiiIII',*e) for e in entries)+data
out=root/'assets/fonts';out.mkdir(exist_ok=True);(out/'inter.atlas').write_bytes(result)
print('Font atlas:',len(result),'bytes; source SHA256:',hashlib.sha256(source.read_bytes()).hexdigest())
