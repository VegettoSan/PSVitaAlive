#include "storage/game_media_metadata.hpp"
#include <cassert>
#include <iostream>
using namespace psvitaalive::storage::media;
using Buffer=std::vector<unsigned char>;
static void put(Buffer& a,size_t at,uint32_t v,unsigned n=4) {
    for(unsigned i=0;i<n;++i)a.at(at+i)=static_cast<unsigned char>(v>>(8*i));
}
static Buffer makeSfo() {
    Buffer a(128);std::memcpy(a.data(),"\0PSF",4);
    put(a,8,52);put(a,12,72);put(a,16,2);
    std::memcpy(a.data()+52,"TITLE\0DISC_ID\0",14);
    put(a,22,0x0204,2);put(a,24,10);put(a,28,16);
    put(a,36,6,2);put(a,38,0x0204,2);put(a,40,10);put(a,44,12);put(a,48,16);
    std::memcpy(a.data()+72,"Test Game",10);std::memcpy(a.data()+88,"ULUS12345",10);
    return a;
}
static Metadata readProbe(const Buffer& a) {
    uint64_t bytes=0;
    Read read=[&](uint64_t off,void* dst,size_t n) {
        assert(within(off,n,a.size()));bytes+=n;std::memcpy(dst,a.data()+off,n);return true;
    };
    auto out=probe(read,a.size());assert(bytes<=512*1024);return out;
}
static void record(Buffer& a,size_t at,uint32_t sector,uint32_t size,bool dir,const char* name) {
    const size_t n=std::strlen(name);a.at(at)=static_cast<unsigned char>(33+n+(n%2==0));
    put(a,at+2,sector);put(a,at+10,size);a.at(at+25)=dir?2:0;
    a.at(at+32)=static_cast<unsigned char>(n);std::memcpy(a.data()+at+33,name,n);
}
int main() {
    auto param=makeSfo();auto sm=sfo(param);
    assert(sm.title=="Test Game" && sm.id=="ULUS12345");
    Buffer p(400);std::memcpy(p.data(),"\0PBP",4);
    put(p,8,40);for(unsigned i=1;i<7;++i)put(p,8+i*4,168);put(p,36,300);
    std::copy(param.begin(),param.end(),p.begin()+40);
    std::memcpy(p.data()+300,"PSISOIMG",8);
    std::memcpy(p.data()+168,"UP0000-NPUJ12345_00-ABCDEFGHIJKLMNOP",36);
    auto m=readProbe(p);assert(m.platform==Platform::PS1 && m.id=="ULUS12345" && m.contentId=="NPUJ12345");
    std::memcpy(p.data()+300,"NPUMDIMG",8);assert(readProbe(p).platform==Platform::PSP);
    put(p,36,401);assert(readProbe(p).platform==Platform::Unknown);
    Buffer isoImage(24*2048);
    const size_t v=16*2048;isoImage[v]=1;std::memcpy(isoImage.data()+v+1,"CD001",5);isoImage[v+6]=1;
    put(isoImage,v+128,2048,2);record(isoImage,v+156,20,2048,true,".");
    record(isoImage,20*2048,21,2048,true,"PSP_GAME");
    record(isoImage,21*2048,22,static_cast<uint32_t>(param.size()),false,"PARAM.SFO;1");
    std::copy(param.begin(),param.end(),isoImage.begin()+22*2048);
    m=readProbe(isoImage);assert(m.platform==Platform::PSP && m.id=="ULUS12345");
    // CISO v1: compressed and uncompressed blocks in a single fixture.
    Buffer c(24+25*4);std::memcpy(c.data(),"CISO",4);put(c,4,24);put(c,8,static_cast<uint32_t>(isoImage.size()));put(c,16,2048);c[20]=1;
    for(unsigned i=0;i<24;++i) {
        const uint32_t off=static_cast<uint32_t>(c.size());
        if(i%2==0) {
            unsigned char out[4096];z_stream zs{};
            assert(deflateInit2(&zs,6,Z_DEFLATED,-15,8,Z_DEFAULT_STRATEGY)==Z_OK);
            zs.next_in=isoImage.data()+i*2048;zs.avail_in=2048;zs.next_out=out;zs.avail_out=sizeof(out);
            assert(deflate(&zs,Z_FINISH)==Z_STREAM_END);
            const size_t len=zs.total_out;deflateEnd(&zs);
            put(c,24+i*4,off);c.insert(c.end(),out,out+len);
        } else {
            put(c,24+i*4,off|0x80000000u);
            c.insert(c.end(),isoImage.begin()+i*2048,isoImage.begin()+(i+1)*2048);
        }
    }
    put(c,24+24*4,static_cast<uint32_t>(c.size()));
    m=readProbe(c);assert(m.platform==Platform::PSP && m.title=="Test Game");
    put(c,24+16*4,0);assert(readProbe(c).platform==Platform::Unknown);
    isoImage.resize(21*2048);assert(readProbe(isoImage).platform==Platform::Unknown);
    std::cout<<"bounded media metadata tests OK\n";
}
