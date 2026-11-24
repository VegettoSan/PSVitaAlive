from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageChops
import cairosvg, json, io, zipfile
P=Path(__file__).resolve().parent
for d in ['atlases','source','sprites','preview']: (P/d).mkdir(exist_ok=True)
WHITE='#ffffff'
PALETTE={'background':'#000000','surface':'#373737','surface_hover':'#6e6e6e','border':'#6e6e6e','text':'#aaaaaa','text_secondary':'#a0a0a0','text_muted':'#6e6e6e','accent':'#3bff00','on_accent':'#000000','disabled':'#6e6e6e'}
AT={}
def svg(w,h,body): return f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">{body}</svg>'
def rect(x,y,w,h,r=0,fill='white',stroke=None,sw=1,opacity=1):
 return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}"'+(f' stroke="{stroke}" stroke-width="{sw}"' if stroke else '')+f' opacity="{opacity}"/>'
def circle(x,y,r,fill='white',stroke=None,sw=1): return f'<circle cx="{x}" cy="{y}" r="{r}" fill="{fill}"'+(f' stroke="{stroke}" stroke-width="{sw}"' if stroke else '')+'/>'
def path(d,fill='none',stroke='white',sw=1.8): return f'<path d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}" stroke-linecap="round" stroke-linejoin="round"/>'
def line(x1,y1,x2,y2,sw=1.8): return path(f'M{x1} {y1}L{x2} {y2}',sw=sw)
def make_atlas(name,size,cell,cols,entries):
 a=Image.new('RGBA',(size,size)); m={}; group=[]
 for i,e in enumerate(entries):
  id,w,h,body,mode,tint,nine=e; x=(i%cols)*cell+(cell-w)//2; y=(i//cols)*cell+(cell-h)//2
  assert x>=2 and y>=2 and x+w+2<=size and y+h+2<=size
  source=svg(w,h,body); (P/'source'/f'{id}.svg').write_text(source)
  png=cairosvg.svg2png(bytestring=source.encode()); im=Image.open(io.BytesIO(png)).convert('RGBA'); im.save(P/'sprites'/f'{id}.png'); a.paste(im,(x,y))
  # extrude 2px gutters (nearest edge); not part of sprite rectangle
  for n in (1,2):
   a.paste(im.crop((0,0,1,h)),(x-n,y)); a.paste(im.crop((w-1,0,w,h)),(x+w+n-1,y)); a.paste(im.crop((0,0,w,1)),(x,y-n)); a.paste(im.crop((0,h-1,w,h)),(x,y+h+n-1))
  for dx,sx in [(-2,0),(w,w-1)]:
   for dy,sy in [(-2,0),(h,h-1)]: a.paste(Image.new('RGBA',(2,2),im.getpixel((sx,sy))),(x+dx,y+dy))
  m[id]={'x':x,'y':y,'w':w,'h':h,'mode':mode,'default_tint':tint,'nine_slice':nine}
  group.append(f'<svg x="{x}" y="{y}" width="{w}" height="{h}" viewBox="0 0 {w} {h}">{body.replace(chr(34)+"surface"+chr(34), chr(34)+id+"_gradient"+chr(34)).replace("url(#surface)", "url(#"+id+"_gradient)")}</svg>')
 a.save(P/'atlases'/f'{name}.png',optimize=True); (P/'source'/f'{name}.svg').write_text(svg(size,size,''.join(group)))
 AT[name]={'file':f'{name}.png','width':size,'height':size,'sprites':m}
 return a
# Neutral grayscale surfaces are multiplied by theme color; alpha controls edge/opacity.
def surface(radius=10,top='#ffffff',bottom='#e4e4e4',border=None,opacity=1):
 return f'<defs><linearGradient id="surface" x1="0" y1="0" x2="0" y2="1"><stop stop-color="{top}"/><stop offset="1" stop-color="{bottom}"/></linearGradient></defs>'+rect(2,2,52,52,radius,'url(#surface)',border,1,opacity)
def U(id,body,tint='surface',nine=True,w=56,h=56): return (id,w,h,body,'multiply',tint,([14,14,14,14] if nine is True else nine if nine else None))
ui=[U('ui_card',surface()),U('ui_card_hover',surface(top='#ffffff',bottom='#f1f1f1'),'surface_hover'),U('ui_card_focus',surface(border='#ffffff'),'surface'),U('ui_focus_ring',rect(1.5,1.5,53,53,12,'none','white',2.5),'accent'),U('ui_panel',surface(12, bottom='#dedede')),U('ui_panel_inset',rect(2,2,52,52,10,'#dddddd'),'surface'),U('ui_button_primary',surface(9,bottom='#eeeeee'),'accent'),U('ui_button_secondary',surface(9,border='#b0b0b0')),U('ui_button_pressed',surface(9,top='#d4d4d4',bottom='#f4f4f4'),'accent'),U('ui_button_disabled',rect(2,2,52,52,9,'white',opacity=.4),'disabled'),U('ui_button_danger',rect(2,2,52,52,9,'white',opacity=.85),'surface'),U('ui_tab_active',rect(2,2,52,52,9),'accent'),U('ui_tab_idle',rect(2,2,52,52,9,'white',opacity=.65)),U('ui_input',surface(9,border='#b5b5b5')),U('ui_input_focus',rect(2,2,52,52,9,'none','white',2),'accent'),U('ui_badge',rect(2,2,52,52,10)),U('ui_badge_outline',rect(2,2,52,52,10,'none','white',1.8),'border'),U('ui_dialog',surface(12,bottom='#ececec')),U('ui_toast',surface(10,bottom='#ededed')),U('ui_keyboard_key',surface(8,bottom='#dfdfdf')),U('ui_keyboard_active',surface(8,bottom='#eeeeee'),'accent'),U('ui_placeholder',rect(2,2,52,52,10,'white',opacity=.55)),U('ui_overlay',rect(0,0,56,56,0,'#000000',opacity=.66),'literal_white',False),U('ui_separator',rect(0,7,56,2,1,'white',opacity=.48),'border',False,56,16),U('ui_scroll_track',rect(4,2,8,52,4,'white',opacity=.4),'surface_hover',[0,7,0,7],16,56),U('ui_scroll_thumb',rect(4,2,8,52,4),'accent',[0,7,0,7],16,56),U('ui_progress_track',rect(2,3,52,10,5,'white',opacity=.45),'surface_hover',[8,0,8,0],56,16),U('ui_progress_fill',rect(2,3,52,10,5),'accent',[8,0,8,0],56,16),U('ui_toggle_off',rect(1,2,54,28,14,'#a0a0a0')+circle(15,16,10),'surface_hover',False,56,32),U('ui_toggle_on',rect(1,2,54,28,14)+circle(41,16,10,'#000000'),'accent',False,56,32),U('ui_checkbox_off',rect(4,4,24,24,6,'none','white',2),'text',False,32,32),U('ui_checkbox_on',rect(4,4,24,24,6)+path('M10 16l4 4 8-9',stroke='#000000',sw=2.4),'accent',False,32,32),U('ui_radio_off',circle(16,16,11,'none','white',2),'text',False,32,32),U('ui_radio_on',circle(16,16,11,'none','white',2)+circle(16,16,6),'accent',False,32,32),U('ui_slider_knob',circle(16,16,12),'accent',False,32,32),U('ui_image_frame',rect(1.5,1.5,53,53,12,'none','white',1.6),'border')]
# v0.2: use vacant cells in UI atlas; existing sprite positions unchanged.
extra_icons={
'key_shift':path('M12 3L3 12H8V21H16V12H21Z'),
'key_backspace':path('M9 5H21V19H9L2 12ZM12 9L18 15M18 9L12 15'),
'key_space':path('M3 10V17H21V10'),
'key_enter':path('M21 4V15H5M10 10L5 15 10 20'),
'key_more':circle(5,12,1.8)+circle(12,12,1.8)+circle(19,12,1.8),
'customization':path('M12 3A9 9 0 1 0 12 21H15C18 21 18 17 15 17H14C12 17 12 14 14 14H18C24 14 22 3 12 3Z')+circle(7,10,1.2)+circle(11,6.5,1.2)+circle(16,7.5,1.2),
'emulator':rect(5,5,14,14,2,'none','white',1.8)+rect(9,9,6,6,1,'none','white',1.5)+path('M8 2V5M12 2V5M16 2V5M8 19V22M12 19V22M16 19V22M2 8H5M2 12H5M2 16H5M19 8H22M19 12H22M19 16H22'),
'media':rect(3,4,18,16,2,'none','white',1.8)+path('M9 8L16 12 9 16Z','white','none'),
'plugin':path('M8 3V8M16 3V8M5 8H19V12A7 7 0 0 1 5 12ZM12 19V22'),
'port':path('M3 7H21M16 2L21 7 16 12M21 17H3M8 12L3 17 8 22'),
'cache':path('M3 7L12 3 21 7 12 11ZM3 12L12 16 21 12M3 17L12 21 21 17'),
'files':path('M4 3H14L20 9V21H4ZM14 3V9H20M8 13H16M8 17H16'),
'zip':path('M5 2H15L20 7V22H5ZM15 2V7H20M9 3H12M12 6H15M9 9H12M12 12H15')+rect(9,15,5,4,1,'none','white',1.5),
'lock':rect(4,10,16,12,2,'none','white',1.8)+path('M7 10V7A5 5 0 0 1 17 7V10M12 15V18')+circle(12,15,1),
'clean':path('M15 3L21 6 13 16M8 12L15 16 11 22H2L5 18Z M5 18L11 22'),
'data':path('M3 6H10L13 9H21V21H3ZM8 14H16M8 17H16'),
'dlc':path('M3 7L12 2 21 7V17L12 22 3 17ZM3 7L12 12 21 7M12 12V22')+path('M15 15H19M17 13V17',sw=1.4),
'unknown':circle(12,12,9,'none','white',1.8)+path('M9 8C9 5 16 5 16 9 16 11 12 11 12 14')+circle(12,18,1.1),
'chevron_up':path('M5 15L12 8 19 15',sw=2.3),
'chevron_down':path('M5 9L12 16 19 9',sw=2.3),
'genre_action':path('M3 19L15 7M12 4L19 11M15 7L20 2 22 4 19 11M2 17L7 22M5 19L3 21'),
'genre_adventure':circle(12,12,10,'none','white',1.8)+path('M16 7L14 14 7 17 10 10Z','white','none'),
'genre_racing':path('M4 22V3M4 4H21L18 9 21 14H4')+rect(5,5,4,4)+rect(13,5,4,4)+rect(9,9,4,4)+rect(17,9,3,4),
'genre_puzzle':path('M3 3H9V6A3 3 0 1 0 15 6V3H21V9H18A3 3 0 1 0 18 15H21V21H15V18A3 3 0 1 0 9 18V21H3V15H6A3 3 0 1 0 6 9H3Z'),
'genre_music':path('M9 18V5L21 2V15M9 8L21 5')+circle(6,18,3)+circle(18,15,3),
'genre_sport':circle(12,12,10,'none','white',1.8)+path('M2 12H22M12 2V22M5 5Q12 12 5 19M19 5Q12 12 19 19'),
'genre_strategy':path('M4 21H20M6 18H18L17 12H7ZM7 12L5 4H9V8H15V4H19L17 12'),
'genre_horror':path('M12 2C5 2 3 7 3 12V16L7 21 9 18 12 22 15 18 17 21 21 16V12C21 7 19 2 12 2Z')+circle(8,11,2)+circle(16,11,2)+circle(12,16,1.2),
}
ui += [('icon_'+k,32,32,f'<g transform="translate(3 3) scale(1.0833333333)">{v}</g>','multiply','text',None) for k,v in extra_icons.items()]
make_atlas('ui_atlas',512,64,8,ui)
# Original glyph drawings in a shared 24-unit grid, deliberately monochrome.
icons={
'search':circle(10,10,6,'none','white',1.8)+path('M14.5 14.5L21 21'),
'filter':path('M3 5H21L14 13V20L10 18V13Z'),
'download':path('M12 3V15M7 10L12 15 17 10M4 16V21H20V16'),
'upload':path('M12 15V3M7 8L12 3 17 8M4 16V21H20V16'),
'update':path('M20 10A8 8 0 0 0 6 5L3 8M3 3V8H8M4 14A8 8 0 0 0 18 19L21 16M21 21V16H16'),
'favorite':path('M12 21L4 13C-1 7 7 1 12 7 17 1 25 7 20 13Z'),
'favorite_filled':path('M12 21L4 13C-1 7 7 1 12 7 17 1 25 7 20 13Z','white'),
'settings':path('M9 3H15L16 6 19 7 21 10 19 13 20 16 17 19 14 18 12 21 9 19 6 20 3 17 4 14 2 11 4 8 7 7Z')+circle(12,12,3,'none','white',1.8),
'trash':path('M4 6H20M9 6V3H15V6M6 6L7 21H17L18 6M10 10V17M14 10V17'),
'folder':path('M3 6H10L13 9H21V20H3Z'),
'gamepad':path('M7 7H17C20 7 21 10 22 15 23 20 19 21 16 17H8C5 21 1 20 2 15 3 10 4 7 7 7ZM5 12H11M8 9V15')+circle(16,11,1.2)+circle(19,14,1.2),
'package':path('M3 7L12 2 21 7V17L12 22 3 17ZM3 7L12 12 21 7M12 12V22M7.5 4.5L16.5 9.5'),
'verified':path('M12 2L21 6V12C21 17 16 20 12 22 8 20 3 17 3 12V6ZM7 12L10.5 15.5 17 9'),
'installed':circle(12,12,9,'none','white',1.8)+path('M7.5 12L10.5 15 17 8.5'),
'wifi':path('M2 8Q12 0 22 8M5 12Q12 6 19 12M8 16Q12 13 16 16')+circle(12,20,1.5),
'battery':rect(2,6,18,12,2,'none','white',1.8)+rect(21,9,2,6,1),
'charging':rect(2,6,18,12,2,'none','white',1.8)+rect(21,9,2,6,1)+path('M12 7L8 13H12L10 17 16 11H12Z','white','none'),
'storage':rect(3,4,18,16,3,'none','white',1.8)+path('M3 14H21')+circle(7,17,1)+circle(11,17,1),
'report':path('M3 4H21V16H10L4 21V16H3ZM12 7V10')+circle(12,13,1),
'user':circle(12,7,4,'none','white',1.8)+path('M3 21V19C3 12 21 12 21 19V21'),
'image':rect(3,3,18,18,3,'none','white',1.8)+circle(8,8,2,'none','white',1.5)+path('M4 18L10 12 14 16 17 13 21 17'),
'pause':rect(6,4,4,16,1)+rect(14,4,4,16,1),
'play':path('M7 4L20 12 7 20Z','white','none'),
'info':circle(12,12,9,'none','white',1.8)+circle(12,8,1.2)+path('M12 11V17'),
'warning':path('M12 3L22 21H2Z M12 9V14')+circle(12,17.5,1),
'history':path('M3 11A9 9 0 1 1 5 18M3 5V11H9M12 7V12L16 15'),
'qr':rect(2,2,7,7,0,'none','white',1.6)+rect(15,2,7,7,0,'none','white',1.6)+rect(2,15,7,7,0,'none','white',1.6)+rect(4,4,3,3)+rect(17,4,3,3)+rect(4,17,3,3)+path('M13 12H16V15H21V20H17V17H13V22M22 12V13M12 2V8M2 12H8',sw=2),
'close':path('M5 5L19 19M19 5L5 19',sw=2.2),
'check':path('M4 12L9 17 20 6',sw=2.3),
'back':path('M14 5L7 12 14 19M7 12H21'),
'forward':path('M10 5L17 12 10 19M17 12H3'),
'link':path('M10 7L13 4C17 0 24 7 20 11L17 14M14 17L11 20C7 24 0 17 4 13L7 10M8 16L16 8'),
'list':path('M8 5H21M8 12H21M8 19H21')+circle(3,5,1.2)+circle(3,12,1.2)+circle(3,19,1.2),
'grid':''.join(rect(x,y,7,7,1.5,'none','white',1.6) for x,y in [(3,3),(14,3),(3,14),(14,14)]),
'sort':path('M7 3V21M3 7L7 3 11 7M17 21V3M13 17L17 21 21 17'),
'archive':rect(3,3,18,5,1,'none','white',1.7)+path('M5 8V21H19V8M9 12H15')}
# 36 glyphs, 40px cells; 32px sprites with 3px margin in art.
I=lambda id,b:(id,32,32,f'<g transform="translate(3 3) scale(1.0833333333)">{b}</g>','multiply','text',None)
make_atlas('icons_atlas',256,40,6,[I('icon_'+k,v) for k,v in icons.items()])
# Controller glyphs: shell neutral, classic color symbols baked, mono symbols white.
shapes={'cross':path('M10 10L22 22M22 10L10 22',sw=2.6),'circle':circle(16,16,7.3,'none','white',2.5),'triangle':path('M16 8L24 22H8Z',sw=2.2),'square':rect(9.5,9.5,13,13,1,'none','white',2.4)}
colors={'cross':'#6399F8','circle':'#F26B76','triangle':'#56D49B','square':'#DF8CCB'}
controls=[]
def C(id,b,mode='literal',tint='literal_white',w=32,h=32): return (id,w,h,b,mode,tint,None)
for key,shape in shapes.items():
 shell=circle(16,16,14,'#242424','#777777',1.2)
 controls += [C('ctrl_'+key+'_classic',shell+shape.replace('white',colors[key])),C('ctrl_'+key+'_mono',shell+shape),C('ctrl_'+key+'_glyph',shape,'multiply','text')]
# hand drawn letters, no embedded fonts; shoulder buttons are L/R as on Vita.
L=path('M13 10V22H21',sw=2.2); R=path('M11 22V10H17C23 10 23 16 17 16H11M17 16L22 22',sw=2.0)
for key,g in [('l',L),('r',R)]: controls.append(C('ctrl_'+key,rect(2,5,28,22,7,'#242424','#777777',1.2)+g))
for key,ang in [('up',0),('right',90),('down',180),('left',270),('all',None)]:
 b=path('M11 2H21V11H30V21H21V30H11V21H2V11H11Z','#242424','#777777',1.2)
 for a in [0,90,180,270]: b+=f'<g transform="rotate({a} 16 16)">'+path('M16 5L12.5 10H19.5Z',('white' if ang in [a,None] else '#666666'),'none')+'</g>'
 controls.append(C('ctrl_dpad_'+key,b))
for key,g in [('left',L),('right',R)]: controls.append(C('ctrl_stick_'+key,circle(16,16,14,'#242424','#777777',1.1)+circle(16,16,10,'#373737','#6e6e6e',1)+f'<g transform="translate(4.8 4.8) scale(.7)">{g}</g>'))
# Small Start/Select physical shapes, differentiated by adjacent translated action at runtime; literal hardware labels are allowed.
# stroke paths make labels font independent, at 32px.
letters={'S':'M5 1H2Q0 1 0 3T2 5H3Q5 5 5 7T3 9H0','T':'M0 1H5M2.5 1V9','A':'M0 9L2.5 1 5 9M1 6H4','R':'M0 9V1H3Q5 1 5 3T3 5H0M2.5 5L5 9','E':'M5 1H0V9H5M0 5H4','L':'M0 1V9H5','C':'M5 1H2Q0 1 0 3V7Q0 9 2 9H5'}
def hardware_label(word):
 sc=.75; total=(len(word)*6-1)*sc; return '<g transform="translate('+str((32-total)/2)+' 12) scale(.75)">'+''.join(f'<g transform="translate({i*6} 0)">{path(letters[ch],sw=1.3)}</g>' for i,ch in enumerate(word))+'</g>'
for key in ['start','select']: controls.append(C('ctrl_'+key,rect(1,8,30,16,7,'#242424','#777777',1)+hardware_label(key.upper())))
controls.extend([C('ctrl_touch_front',rect(5,3,22,26,4,'#242424','#777777',1.2)+rect(8,7,16,16,2,'none','white',1.2)+circle(16,15,3,'none','white',1.4)),C('ctrl_touch_rear',rect(5,3,22,26,4,'#242424','#777777',1.2)+''.join(circle(x,y,1.1) for x in [11,16,21] for y in [10,15,20]))])
make_atlas('controls_atlas',256,40,6,controls)
manifest={'format_version':1,'project':'PSVitaAlive','palette':PALETTE,'coordinates':'top-left pixels; x/y/w/h exclude 2 px extruded gutter','nine_slice_order':['left','top','right','bottom'],'alpha':'straight RGBA8888; verify blend configuration in client','atlases':AT}
(P/'atlases'/'atlas.json').write_text(json.dumps(manifest,indent=2)+'\n')
# C++ header optional, compile-time coordinates, no JSON parser required.
lines=['#pragma once','// Generated from build_assets.py. Coordinates are top-left pixels.','namespace psvitaalive { namespace skin {','struct Sprite { int x, y, w, h, left, top, right, bottom; };']
for name,a in AT.items():
 lines += [f'namespace {name} {{',f'constexpr int width = {a["width"]}, height = {a["height"]};']
 for k,v in a['sprites'].items(): lines.append('constexpr Sprite '+k+' = {'+', '.join(map(str,[v['x'],v['y'],v['w'],v['h']]+(v['nine_slice'] or [0]*4)))+'};')
 lines.append('}')
lines+=['}}']; (P/'atlases'/'atlas_coords.hpp').write_text('\n'.join(lines)+'\n')
# Validate unique IDs, dimensions, non-overlap of sprite+gutter, alpha, metadata.
ids=set()
for name,a in AT.items():
 im=Image.open(P/'atlases'/a['file']); assert im.mode=='RGBA' and im.size==(a['width'],a['height']); bounds=[]
 for id,v in a['sprites'].items():
  assert id not in ids; ids.add(id); x,y,w,h=(v[k] for k in ['x','y','w','h']); box=(x-2,y-2,x+w+2,y+h+2); assert box[0]>=0 and box[1]>=0 and box[2]<=im.width and box[3]<=im.height
  assert not any(box[0]<b[2] and box[2]>b[0] and box[1]<b[3] and box[3]>b[1] for b in bounds); bounds.append(box)
  crop=im.crop((x,y,x+w,y+h)); assert crop.getchannel('A').getbbox() and crop.tobytes()==Image.open(P/'sprites'/f'{id}.png').tobytes()
  if v['nine_slice']: l,t,r,b=v['nine_slice']; assert l+r<w and t+b<h
print({k:len(v['sprites']) for k,v in AT.items()})
