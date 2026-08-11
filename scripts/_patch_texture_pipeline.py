from pathlib import Path
import subprocess

path = Path('Client PSVitaAlive/source/ui/full_catalog_screen.cpp')
text = path.read_text(encoding='utf-8')
start_marker = 'void FullCatalogScreen::prepareVisibleTextures(){'
end_marker = '\nvoid FullCatalogScreen::drawImage'
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit('prepareVisibleTextures boundaries not found')
new = '''void FullCatalogScreen::prepareVisibleTextures(){
    if(!imageCache_||catalogLoading_||installProgressActive_)return;
    // Upload at most one new texture per frame. This avoids several Vita2D
    // GPU buffer mappings being created in the same frame while navigating.
    bool loadedThisFrame=false;
    auto prepareItem=[&](const CatalogItem&it,bool detail){
        if(loadedThisFrame)return;
        const std::string& primary=!it.icon.empty()?it.icon:it.cover;
        if(!primary.empty()){
            const size_t before=textures_.size();
            prepareImageTexture(primary,"app");
            loadedThisFrame=textures_.size()>before;
            if(loadedThisFrame)return;
        }
        if(detail){
            const size_t sc=std::min<size_t>(5,it.screenshots.size());
            for(size_t i=0;i<sc&&!loadedThisFrame;++i){
                const size_t before=textures_.size();
                prepareImageTexture(it.screenshots[i],"shot");
                loadedThisFrame=textures_.size()>before;
            }
        }
    };
    if(state_.mode==UiMode::FULL_CATALOG||state_.mode==UiMode::OPENING_DETAIL){
        const int rows=visibleRowsFull();
        for(int r=0;r<rows&&!loadedThisFrame;++r)
            for(int c=0;c<3&&!loadedThisFrame;++c){
                int i=(state_.catalogScrollRow+r)*3+c;
                if(i>=0&&i<(int)items_.size())prepareItem(items_[i],false);
            }
    }else{
        const int rows=visibleRowsSplit();
        for(int r=0;r<rows&&!loadedThisFrame;++r){
            int i=state_.catalogScrollRow+r;
            if(i>=0&&i<(int)items_.size())prepareItem(items_[i],false);
        }
        const int i=selectedIndex();
        if(i>=0&&!loadedThisFrame)prepareItem(items_[i],true);
    }
}'''
path.write_text(text[:start] + new + text[end:], encoding='utf-8')
subprocess.run(['git', 'config', 'user.name', 'github-actions[bot]'], check=True)
subprocess.run(['git', 'config', 'user.email', '41898282+github-actions[bot]@users.noreply.github.com'], check=True)
subprocess.run(['git', 'add', 'Client PSVitaAlive/source/ui/full_catalog_screen.cpp', 'scripts/_patch_texture_pipeline.py'], check=True)
subprocess.run(['git', 'rm', 'scripts/_patch_texture_pipeline.py'], check=True)
subprocess.run(['git', 'commit', '-m', 'Throttle texture uploads and pause during catalog loading'], check=True)
subprocess.run(['git', 'push'], check=True)
