from pathlib import Path
import re

path = Path('Client PSVitaAlive/source/main.cpp')
debug = Path('.github/patch_debug.txt')
text = path.read_text(encoding='utf-8')

lines = []
lines.append(f'length={len(text)}')
lines.append(f'prompt_count={text.count("bool promptDownloadAllImages(size_t totalImages)")}')
lines.append(f'format_count={text.count("std::string formatBytes")}')
lines.append(f'cancel_count={text.count("CIRCLE: CANCEL")}')

try:
    new_prompt = '''bool promptDownloadAllImages(size_t totalImages){
    vita2d_wait_rendering_done();
    vita2d_pgf* font=vita2d_load_default_pgf();
    if(!font){psvitaalive::diagnostics::log("[Startup] custom image prompt font load failed; defaulting to on-demand mode");return false;}
    bool yes=false,done=false;int selected=0;uint32_t prev=0;
    while(!done){
        SceCtrlData pad={};sceCtrlPeekBufferPositive(0,&pad,1);
        const uint32_t pressed=pad.buttons&~prev;prev=pad.buttons;
        if(pressed&SCE_CTRL_LEFT)selected=0;
        if(pressed&SCE_CTRL_RIGHT)selected=1;
        if(pressed&SCE_CTRL_CROSS){yes=selected==0;done=true;}
        if(pressed&SCE_CTRL_CIRCLE){yes=false;done=true;}
        vita2d_start_drawing();vita2d_clear_screen();
        const unsigned SURFACE=RGBA8(0x37,0x37,0x37,255),BORDER=RGBA8(0x6E,0x6E,0x6E,255),TEXT=RGBA8(0xAA,0xAA,0xAA,255),DIM=RGBA8(0x6E,0x6E,0x6E,255),ACCENT=RGBA8(0x3B,0xFF,0,255),WHITE=RGBA8(255,255,255,255),BLACK=RGBA8(0,0,0,255),PANEL=RGBA8(0x20,0x20,0x20,255);
        const int w=620,h=320,x=(960-w)/2,y=(544-h)/2;
        vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,185));
        vita2d_draw_rectangle(x,y,w,h,PANEL);vita2d_draw_rectangle(x,y,w,2,ACCENT);vita2d_draw_rectangle(x,y+2,2,h-2,ACCENT);
        vita2d_pgf_draw_text(font,x+28,y+32,ACCENT,.68f,"PSVitaAlive");
        vita2d_pgf_draw_text(font,x+28,y+68,WHITE,1.02f,"Download catalog images?");
        vita2d_pgf_draw_text(font,x+28,y+94,TEXT,.58f,"Images are downloaded once and kept in the local cache.");
        vita2d_pgf_draw_text(font,x+28,y+114,DIM,.54f,"This can take a very long time and use network data.");
        char count[96]={};sceClibSnprintf(count,sizeof(count),"Pending images: %u",(unsigned)totalImages);
        vita2d_pgf_draw_text(font,x+28,y+144,ACCENT,.68f,count);
        vita2d_pgf_draw_text(font,x+28,y+166,TEXT,.54f,"Already cached images are excluded from this count.");
        vita2d_pgf_draw_text(font,x+28,y+188,TEXT,.54f,"Total size: calculated while downloading");
        vita2d_pgf_draw_text(font,x+28,y+208,TEXT,.54f,"Speed and exact progress will appear in the download panel.");
        const int by=y+236,bw=220,bh=38;
        vita2d_draw_rectangle(x+58,by,bw,bh,selected==0?ACCENT:SURFACE);vita2d_draw_rectangle(x+58,by,bw,1,selected==0?ACCENT:BORDER);
        vita2d_draw_rectangle(x+342,by,bw,bh,selected==1?ACCENT:SURFACE);vita2d_draw_rectangle(x+342,by,bw,1,selected==1?ACCENT:BORDER);
        vita2d_pgf_draw_text(font,x+125,by+25,selected==0?BLACK:WHITE,.62f,"DOWNLOAD ALL");
        vita2d_pgf_draw_text(font,x+425,by+25,selected==1?BLACK:WHITE,.62f,"LATER");
        vita2d_pgf_draw_text(font,x+28,y+h-14,DIM,.52f,"Left/Right: Select    Cross: Confirm    Circle: Later");
        vita2d_end_drawing();vita2d_swap_buffers();sceKernelDelayThread(16*1000);
    }
    vita2d_wait_rendering_done();vita2d_free_pgf(font);
    psvitaalive::diagnostics::log(std::string("[Startup] image warmup choice=")+(yes?"ALL":"ON_DEMAND"));return yes;
}
'''
    pattern = re.compile(r'bool promptDownloadAllImages\(size_t totalImages\)\{.*?\n\}std::string formatBytes', re.S)
    text, n = pattern.subn(new_prompt + 'std::string formatBytes', text, count=1)
    lines.append(f'prompt_replaced={n}')

    old = 'vita2d_draw_rectangle(x+w-170,y+h-50,130,34,cancelled?ACCENT:SURFACE);vita2d_pgf_draw_text(font,x+w-148,y+h-27,cancelled?BLACK:WHITE,.60f,"CIRCLE: CANCEL");'
    new = 'vita2d_draw_rectangle(x+w-190,y+h-52,162,38,cancelled?ACCENT:SURFACE);vita2d_draw_rectangle(x+w-190,y+h-52,162,1,ACCENT);vita2d_pgf_draw_text(font,x+w-176,y+h-27,cancelled?BLACK:WHITE,.54f,"CIRCLE  CANCEL DOWNLOAD");'
    lines.append(f'cancel_exact={old in text}')
    text = text.replace(old, new, 1)
    path.write_text(text, encoding='utf-8')
    lines.append('write=ok')
except Exception as e:
    lines.append('error=' + repr(e))

debug.write_text('\n'.join(lines) + '\n', encoding='utf-8')
print('\n'.join(lines))
