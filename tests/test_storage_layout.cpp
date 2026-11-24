#include "ui/storage_layout.hpp"
#include "storage/saved_zip_metadata.hpp"
#include <cassert>
#include <string>
#include <vector>
#include <algorithm>
using psvitaalive::ui::StorageLayout;
using psvitaalive::storage::savedZipField;
using psvitaalive::storage::savedZipMatchesApp;

int main() {
    const std::string metadata = "PS Vita Alive Store - ZIP manual recovery\n"
        "App: Café\r\nApp ID: arcaea\nTitle ID: ARCA69420\n"
        "Archive: example.zip\nArchive parts: 4\n";
    assert(savedZipField(metadata,"App: ") == "Café");
    assert(savedZipField(metadata,"App ID: ") == "arcaea");
    assert(savedZipField(metadata,"Title ID: ") == "ARCA69420");
    assert(savedZipField(metadata,"Archive parts: ") == "4");
    assert(savedZipField("Other: App: bogus\nApp: Real","App: ") == "Real");
    assert(savedZipField("App ID: -\n","App ID: ").empty());
    assert(savedZipField("App: \n","App: ").empty());
    assert(savedZipField(metadata,"Missing: ").empty());
    assert(savedZipMatchesApp("arcaea","ARCA69420","arcaea","ARCA69420"));
    assert(savedZipMatchesApp("arcaea","","arcaea","ARCA69420"));
    assert(savedZipMatchesApp("","ARCA69420","arcaea","ARCA69420"));
    assert(!savedZipMatchesApp("","","arcaea","ARCA69420"));
    assert(!savedZipMatchesApp("arcaea","WRONG","arcaea","ARCA69420"));
    assert(!savedZipMatchesApp("other","ARCA69420","arcaea","ARCA69420"));

    using L = StorageLayout;
    static_assert(L::loadingSegmentOffset(400,0)==0);
    static_assert(L::loadingSegmentOffset(400,900)==300);
    static_assert(L::loadingSegmentOffset(400,1800)==0);
    for(int width : {1,3,4,100,400,712}) {
        for(unsigned ms=0;ms<3600;++ms) {
            const int offset=L::loadingSegmentOffset(width,ms);
            assert(offset>=0 && offset+L::loadingSegmentWidth(width)<=width);
        }
    }
    static_assert(L::rowHeight(4,true) > L::rowHeight(4,false));
    static_assert(L::rowHeight(4,true)-L::gap >= 86);
    static_assert(L::listOffset+L::viewportHeight < L::buttonOffset);
    static_assert(L::buttonOffset+L::buttonHeight < L::height);
    for(int zips : {0,1,2,20,512}) {
        std::vector<int> heights{L::rowHeight(5,false),L::rowHeight(0,false),
            L::rowHeight(4,false),L::rowHeight(1,false)};
        heights.insert(heights.end(),zips,L::rowHeight(4,true));
        heights.push_back(L::rowHeight(2,false));
        heights.push_back(L::rowHeight(3,false));
        int total=0;
        for(int h:heights) total+=h;
        int offset=0;
        int scroll=0;
        for(int h:heights) {
            if(offset<scroll)scroll=offset;
            else if(offset+h>scroll+L::viewportHeight)
                scroll=offset+h-L::viewportHeight;
            scroll=std::max(0,std::min(scroll,std::max(0,total-L::viewportHeight)));
            assert(L::fullyVisible(offset,h,scroll));
            assert(L::rowHit(offset-scroll,offset,h,scroll));
            assert(L::rowHit(offset-scroll+h-L::gap-1,offset,h,scroll));
            assert(!L::rowHit(offset-scroll+h-L::gap,offset,h,scroll));
            assert(!L::rowHit(offset-scroll-1,offset,h,scroll));
            assert(!L::fullyVisible(offset,h,offset+1));
            offset+=h;
        }
        assert(scroll==total-L::viewportHeight);
    }
}
