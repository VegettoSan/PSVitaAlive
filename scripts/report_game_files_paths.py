#!/usr/bin/env python3
"""Generate complete Game Files route report from canonical apps/*.json only."""
import json
from collections import defaultdict
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
AUDIT=ROOT/"docs/audits/game_files_zip_roots_20261105.json"
OUT=ROOT/"docs/GAME_FILES_ROUTES_20261105.md"
BT=chr(96)
def code(s):return BT+str(s).replace("|",r"\|").replace(BT,"\\"+BT)+BT
def clean(s):return str(s).replace("|",r"\|").replace("\n"," ")

def main():
    audit={x["id"]:x for x in json.loads(AUDIT.read_text(encoding="utf-8"))["results"]}
    rows=[];groups=defaultdict(set);multi=[];external=inside=ext_links=app_links=0
    for p in sorted((ROOT/"apps").glob("*.json")):
        a=json.loads(p.read_text(encoding="utf-8"))
        links=[l for l in a.get("links",[]) if l.get("type")=="Game Files"]
        if not links:continue
        one=[]
        for l in links:
            base=l.get("extract_path","")
            paths=l.get("installed_paths")
            if base.startswith("ux0:data/"):
                ext_links+=1;kind="data"
                assert isinstance(paths,list) and paths, (p,base)
                targets=["ux0:data/"+root.rstrip("/")+"/" for root in paths]
                for root in paths:groups[root].add(a["id"])
            else:
                assert base in ("ux0:app/",f'ux0:app/{a["title_id"]}/'),(p,base)
                assert paths is None,(p,paths)
                app_links+=1;kind="app"
                targets=[f'ux0:app/{a["title_id"]}/']
            one.append({"kind":kind,"base":base,"paths":paths or [],"targets":targets,"name":l.get("name","Game Files")})
        assert len({q["kind"] for q in one})==1, p
        if one[0]["kind"]=="data":external+=1
        else:inside+=1
        rows.append((a,p,one))
        if len(one)>1:multi.append((a,one))
    rows.sort(key=lambda v:(v[0].get("name","").casefold(),v[0]["title_id"]))
    assert (len(rows),external,inside)==(158,150,8), (len(rows),external,inside)
    lines=[
        "# PSVitaAlive — Inventario completo de rutas Game Files",
        "",
        "**Corte:** 5 de noviembre de 2026. **Fuente:** archivos canónicos de §apps/§, no el catálogo generado.",
        "",
        "> Regla: para Game Files externos, §installed_paths§ se resuelve desde la raíz §ux0:data/§, aunque §extract_path§ apunte a una subcarpeta más profunda. Los Game Files ubicados en §ux0:app/§ pertenecen a la aplicación y no requieren §installed_paths§.",
        "",
        "## Resumen",
        "",
        f"- **{len(rows)} aplicaciones** con Game Files: **{external}** en §ux0:data/§ y **{inside}** en §ux0:app/§.",
        f"- **{ext_links+app_links} enlaces Game Files**: {ext_links} hacia data y {app_links} hacia app.",
        "- Las rutas documentan la carpeta principal identificada; no otorgan permiso automático para borrar partidas, ajustes, cachés compartidas u otros archivos.",
        "",
        "## Tabla por aplicación (configuración efectiva)",
        "",
        "| # | Aplicación | Title ID | JSON | Destino §extract_path§ | §installed_paths§ | Ruta que se identifica | Verificación |",
        "|---:|---|---|---|---|---|---|---|",
    ]
    for index,(app,p,rec) in enumerate(rows,1):
        dest=", ".join(dict.fromkeys(code(x["base"]) for x in rec))
        names=", ".join(dict.fromkeys(code(v) for x in rec for v in x["paths"])) or "—"
        resolved=", ".join(dict.fromkeys(code(v) for x in rec for v in x["targets"]))
        ev=audit.get(app["id"])
        if rec[0]["kind"]=="app":status="Destino app"
        elif ev and ev["status"]=="zip_verified":status="ZIP"
        elif ev and ev["status"]=="from_extract_path":status="Ruta de extracción"
        else:status="Documentación / revisión"
        lines.append(f"| {index} | [{clean(app['name'])}](../apps/{p.name}) | {code(app['title_id'])} | {code(p.name)} | {dest} | {names} | {resolved} | {status} |")
    lines.extend([
        "",
        "## Variantes y enlaces Game Files múltiples",
        "",
        "Si una aplicación ofrece variantes de idioma o varios enlaces, se relacionan aquí. La auditoría de un índice ZIP no prueba por sí sola que todos sus enlaces alternativos contengan la misma estructura.",
        "",
        "| Aplicación | Enlace | §extract_path§ | §installed_paths§ | Ruta principal |",
        "|---|---|---|---|---|",
    ])
    for app,rec in multi:
        for v in rec:
            lines.append(f"| {clean(app['name'])} | {clean(v['name'])} | {code(v['base'])} | {', '.join(code(x) for x in v['paths']) or '—'} | {', '.join(code(x) for x in v['targets'])} |")
    lines.extend([
        "",
        "## Carpetas externas compartidas",
        "",
        "Una misma carpeta puede pertenecer funcionalmente a varios ports. **No eliminarla como propiedad exclusiva de una aplicación**.",
        "",
        "| Carpeta | Apps que la referencian (ID interno) |",
        "|---|---|",
    ])
    shared={k:v for k,v in groups.items() if len(v)>1}
    for root,ids in sorted(shared.items(),key=lambda v:v[0].casefold()):
        lines.append(f"| {code('ux0:data/'+root+'/')} | {', '.join(code(x) for x in sorted(ids))} |")
    if not shared:lines.append("| — | No se encontraron carpetas repetidas |")
    lines.extend([
        "",
        "## Excepciones y notas",
        "",
        "- **Animal Crossing Vita:** su ZIP extrae en §ux0:data/AnimalCrossing/rom/§, pero se identifica la carpeta principal §ux0:data/AnimalCrossing/§.",
        "- **The Drifter:** el ZIP ya incluye §DRFT00001/§. Se extrae en §ux0:app/§, y no declara §installed_paths§.",
        "- **Pocket Mortys Vita:** el ZIP contiene archivos sueltos para la aplicación; se extraen directamente en §ux0:app/POMO00137/§.",
        "- **Batman: The Dark Knight Rises y NBA 2K16:** sus ZIP incluyen una carpeta §shader_cache/§ independiente. La tabla registra solo las carpetas principales §kras/§ y §nba2k16/§; su propiedad requiere revisión antes de borrar.",
        "- **Wonder Boy: The Dragon's Trap:** §wonderboy/§ proviene de instrucciones del port; el índice ZIP no fue accesible por error HTTP.",
        "- **Rutas que coinciden con Title ID:** pueden estar en §ux0:data/§; únicamente el §extract_path§ del enlace indica el destino real.",
        "- **Data Files y otras clases de enlaces:** no forman parte de esta migración.",
        "",
        "## Procedencia",
        "",
        "- [Archivos fuente §apps/§](../apps/).",
        "- [Auditoría de índices ZIP](audits/game_files_zip_roots_20261105.json).",
        "- [Plan técnico de Game/Data Files](INSTALLED_GAME_DATA_MANAGEMENT_PLAN_20261105.md).",
        "- [Script que genera este documento](../scripts/report_game_files_paths.py).",
        "",
        "**Estado funcional:** los metadatos están listos para el catálogo; la lectura de §installed_paths§ en el cliente PS Vita y su edición en el generador web siguen siendo fases de implementación separadas.",
        "",
    ])
    OUT.write_text("\n".join(lines).replace("§",BT),encoding="utf-8")
    print(f"{OUT}: {len(rows)} apps; {ext_links+app_links} Game Files links; {len(multi)} multi-link apps; {len(shared)} shared roots")
if __name__=="__main__":main()
