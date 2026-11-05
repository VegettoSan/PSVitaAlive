#!/usr/bin/env python3
"""Audit Game Files ZIP central directories using HTTP Range, without downloading payloads."""
import concurrent.futures, io, json, os, re, urllib.request, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/"docs/audits/game_files_zip_roots_20261105.json"
PENDING=set("""barony-vita batman-the-dark-knight-rises biohazard4-vita call-of-duty-4-modern-warfare-multiplayer castle-crashers cod-waw cod-boz-vita cut-the-rope-classic cut-the-rope-magic devil-may-cry-4-refrain-vita ducktales-remastered duskligth ge007vita gravity-guy half-life-2 jazz-jackrabbit-2-resurrection manhunt modern-combat-2-black-pegasus modern-combat-3-fallen-nation modern-combat-sandstorm paper-mario-vita red-ball-4 resident-evil-4-gc shotgunk2vita the-drifter turbo-fast-league ultimate-spiderman-total-mayhem""".split())
HEADERS={"Accept-Encoding":"identity","User-Agent":"PSVitaAlive/1.0 (ZIP central directory range-only audit)"}
class RemoteZip(io.RawIOBase):
    def __init__(self,url):
        self.url,self.pos=url,0
        with urllib.request.urlopen(urllib.request.Request(url,headers={**HEADERS,"Range":"bytes=0-0"}),timeout=18) as resp:
            cr=resp.headers.get("Content-Range","")
            if resp.status!=206 or "/" not in cr: raise RuntimeError("HTTP Range unavailable; refused ZIP download")
            self.length=int(cr.rsplit("/",1)[-1])
            resp.read(1)
        self.at=-1;self.cache=b""
    def readable(self):return True
    def seekable(self):return True
    def tell(self):return self.pos
    def seek(self,off,whence=0):
        self.pos=off if whence==0 else self.pos+off if whence==1 else self.length+off
        if self.pos<0:raise ValueError("negative offset")
        return self.pos
    def read(self,size=-1):
        if size<0:size=self.length-self.pos
        size=min(size,max(0,self.length-self.pos))
        if size==0:return b""
        if not(self.at<=self.pos and self.pos+size<=self.at+len(self.cache)):
            start=self.pos;length=min(max(size,131072),8388608,self.length-start)
            rq=urllib.request.Request(self.url,headers={**HEADERS,"Range":f"bytes={start}-{start+length-1}"})
            with urllib.request.urlopen(rq,timeout=40) as resp:
                if resp.status!=206:raise RuntimeError("Range ignored; refused archive download")
                self.cache=resp.read(length+1)
            if len(self.cache)>length:raise RuntimeError("unexpected oversized Range response")
            self.at=start
        out=self.cache[self.pos-self.at:self.pos-self.at+size]
        self.pos+=len(out)
        return out
def zip_roots(url):
    with zipfile.ZipFile(RemoteZip(url)) as z:
        roots=set();count=0
        for info in z.infolist():
            p=info.filename.replace("\\","/")
            if p.startswith("/") or any(k in ("..",".","") for k in p.strip("/").split("/")):
                raise RuntimeError("unsafe ZIP path")
            q=p.strip("/").split("/")
            if q[0] in ("__MACOSX",".DS_Store"):continue
            if len(q)==1 and not info.is_dir():
                if re.fullmatch(r"(?i)(readme|license|changelog|instructions)(?:[._-][\w-]+)?\.(?:txt|md|html)",q[0]):continue
                raise RuntimeError("root-level loose file: "+q[0][:80])
            roots.add(q[0]);count+=1
        if len(roots)!=1:raise RuntimeError("root candidates: "+", ".join(sorted(roots)[:8]))
        root=next(iter(roots))
        if not re.fullmatch(r"[\w .()\-]+",root):raise RuntimeError("unsafe root")
        return root,count
def inspect(path):
    j=json.loads(path.read_text(encoding="utf-8"))
    links=[l for l in j.get("links",[]) if l.get("type")=="Game Files"]
    if not links or all(str(l.get("extract_path","")).startswith("ux0:app/") for l in links):return None
    base=[str(l.get("extract_path","")).replace("\\","/") for l in links]
    o={"id":j["id"],"name":j.get("name"),"title_id":j.get("title_id"),"status":"pending","proposed_paths":[],"urls":[l.get("url") for l in links if l.get("url")]}
    if len(set(base))==1 and base[0].startswith("ux0:data/") and base[0]!="ux0:data/":
        o.update(status="from_extract_path",proposed_paths=[base[0][len("ux0:data/"):].split("/")[0]],note="ZIP contents not verified")
        return o
    urls=[u for u in o["urls"] if u and u.startswith("https://archive.org/download/") and u.lower().split("?")[0].endswith(".zip")]
    if not urls:o.update(status="unverified",note="No Archive.org ZIP");return o
    errs=[]
    for u in urls[:2]:
        try:
            root,n=zip_roots(u)
            o.update(status="zip_verified",proposed_paths=[root],zip_used=u,entries=n)
            return o
        except Exception as e:errs.append(f"{type(e).__name__}: {str(e)[:160]}")
    o.update(status="unverified",note=" | ".join(errs))
    return o
def main():
    files=sorted((ROOT/"apps").glob("*.json"))
    if os.getenv("AUDIT_PENDING_ONLY")=="1":files=[p for p in files if p.stem in PENDING]
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as ex:
        res=[x for x in ex.map(inspect,files) if x is not None]
    for row in res:print(f'{row["id"]}: {row["status"]} {row["proposed_paths"]} {row.get("note","")[:80]}',flush=True)
    OUT.parent.mkdir(parents=True,exist_ok=True)
    OUT.write_text(json.dumps({"method":"HTTP Range ZIP central directory only","results":res},ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print("VERIFIED",sum(x["status"]=="zip_verified" for x in res),"OF",len(res))
if __name__=="__main__":main()
