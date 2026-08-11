#!/usr/bin/env bash
set -euo pipefail

f='Client PSVitaAlive/source/main.cpp'

sed -i 's/const unsigned SURFACE=RGBA8(0x37,0x37,0x37,255),TEXT=RGBA8(0xAA,0xAA,0xAA,255),ACCENT=RGBA8(0x3B,0xFF,0,255),WHITE=RGBA8(255,255,255,255),BLACK=RGBA8(0,0,0,255),PANEL=RGBA8(0x20,0x20,0x20,255);/const unsigned SURFACE=RGBA8(0x37,0x37,0x37,255),BORDER=RGBA8(0x6E,0x6E,0x6E,255),TEXT=RGBA8(0xAA,0xAA,0xAA,255),DIM=RGBA8(0x6E,0x6E,0x6E,255),ACCENT=RGBA8(0x3B,0xFF,0,255),WHITE=RGBA8(255,255,255,255),BLACK=RGBA8(0,0,0,255),PANEL=RGBA8(0x20,0x20,0x20,255);/' "$f"
sed -i 's/const int w=620,h=250,x=(960-w)\/2,y=(544-h)\/2;/const int w=620,h=320,x=(960-w)\/2,y=(544-h)\/2;/' "$f"
sed -i 's/vita2d_draw_rectangle(x,y,w,h,PANEL);vita2d_draw_rectangle(x,y,w,2,ACCENT);/vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,185));vita2d_draw_rectangle(x,y,w,h,PANEL);vita2d_draw_rectangle(x,y,w,2,ACCENT);vita2d_draw_rectangle(x,y+2,2,h-2,ACCENT);/' "$f"
sed -i 's/vita2d_pgf_draw_text(font,x+28,y+96,TEXT,.62f,"This can take a very long time and use network data.");/vita2d_pgf_draw_text(font,x+28,y+94,TEXT,.58f,"Images are downloaded once and kept in the local cache.");vita2d_pgf_draw_text(font,x+28,y+114,DIM,.54f,"This can take a very long time and use network data.");/' "$f"
sed -i 's/vita2d_pgf_draw_text(font,x+28,y+146,TEXT,.56f,"Already cached images are excluded.");/vita2d_pgf_draw_text(font,x+28,y+166,TEXT,.54f,"Already cached images are excluded from this count.");vita2d_pgf_draw_text(font,x+28,y+188,TEXT,.54f,"Total size: calculated while downloading");vita2d_pgf_draw_text(font,x+28,y+208,TEXT,.54f,"Speed and exact progress will appear in the download panel.");/' "$f"
sed -i 's/const int by=y+166,bw=220,bh=42;/const int by=y+236,bw=220,bh=38;/' "$f"
sed -i 's/vita2d_draw_rectangle(x+58,by,bw,bh,selected==0?ACCENT:SURFACE);vita2d_draw_rectangle(x+342,by,bw,bh,selected==1?ACCENT:SURFACE);/vita2d_draw_rectangle(x+58,by,bw,bh,selected==0?ACCENT:SURFACE);vita2d_draw_rectangle(x+58,by,bw,1,selected==0?ACCENT:BORDER);vita2d_draw_rectangle(x+342,by,bw,bh,selected==1?ACCENT:SURFACE);vita2d_draw_rectangle(x+342,by,bw,1,selected==1?ACCENT:BORDER);/' "$f"
sed -i 's/vita2d_pgf_draw_text(font,x+146,by+27,selected==0?BLACK:WHITE,.70f,"YES");vita2d_pgf_draw_text(font,x+430,by+27,selected==1?BLACK:WHITE,.70f,"NO");/vita2d_pgf_draw_text(font,x+125,by+25,selected==0?BLACK:WHITE,.62f,"DOWNLOAD ALL");vita2d_pgf_draw_text(font,x+425,by+25,selected==1?BLACK:WHITE,.62f,"LATER");/' "$f"
sed -i 's/vita2d_pgf_draw_text(font,x+28,y+234,TEXT,.52f,"Left\/Right: Select    Cross: Confirm    Circle: No");/vita2d_pgf_draw_text(font,x+28,y+h-14,DIM,.52f,"Left\/Right: Select    Cross: Confirm    Circle: Later");/' "$f"
sed -i 's/vita2d_draw_rectangle(x+w-170,y+h-50,130,34,cancelled?ACCENT:SURFACE);vita2d_pgf_draw_text(font,x+w-148,y+h-27,cancelled?BLACK:WHITE,.60f,"CIRCLE: CANCEL");/vita2d_draw_rectangle(x+w-190,y+h-52,162,38,cancelled?ACCENT:SURFACE);vita2d_draw_rectangle(x+w-190,y+h-52,162,1,ACCENT);vita2d_pgf_draw_text(font,x+w-176,y+h-27,cancelled?BLACK:WHITE,.54f,"CIRCLE  CANCEL DOWNLOAD");/' "$f"

echo 'PATCH_OK'
