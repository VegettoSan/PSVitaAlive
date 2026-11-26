#pragma once
// Bounded, read-only metadata probes. No Vita APIs, extraction or executable code.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include <zlib.h>

namespace psvitaalive { namespace storage { namespace media {
enum class Platform { Unknown, PSP, PS1 };
struct Metadata {
    Platform platform=Platform::Unknown;
    std::string title,id,contentId;
};
using Read=std::function<bool(uint64_t,void*,size_t)>;
inline uint16_t le16(const unsigned char* p) {return uint16_t(p[0]) | uint16_t(p[1])<<8;}
inline uint32_t le32(const unsigned char* p) {return uint32_t(le16(p)) | uint32_t(le16(p+2))<<16;}
inline uint64_t le64(const unsigned char* p) {return uint64_t(le32(p)) | uint64_t(le32(p+4))<<32;}
inline bool within(uint64_t offset,uint64_t count,uint64_t size) {
    return offset<=size && count<=size-offset;
}
inline std::string idOf(std::string s) {
    if(s.size()==10 && s[4]=='-')s.erase(4,1);
    if(s.size()!=9)return {};
    for(char& c:s) {
        if(c>='a'&&c<='z')c=char(c-'a'+'A');
        if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9')))return {};
    }
    return s;
}
inline std::string cleanString(const unsigned char* p,size_t n,size_t cap=256) {
    const auto* z=static_cast<const unsigned char*>(std::memchr(p,0,n));
    if(!z || size_t(z-p)>cap)return {};
    std::string out;
    for(;p<z;++p)if(*p>=32 && *p!=127)out.push_back(char(*p));
    return out;
}
struct Sfo {std::string title,id,category;};
inline Sfo sfo(const std::vector<unsigned char>& a) {
    Sfo out;
    if(a.size()<20 || std::memcmp(a.data(),"\0PSF",4)!=0)return out;
    const uint32_t keys=le32(a.data()+8),data=le32(a.data()+12),count=le32(a.data()+16);
    if(count>256 || keys<20+16*count || keys>data || data>a.size())return {};
    for(uint32_t i=0;i<count;++i) {
        const auto* e=a.data()+20+i*16;
        const uint64_t key=uint64_t(keys)+le16(e), val=uint64_t(data)+le32(e+12);
        const uint32_t len=le32(e+4),max=le32(e+8);
        if(key>=data || !within(val,len,a.size()) || len>max || !len)continue;
        if(le16(e+2)!=0x0204)continue;
        const auto name=cleanString(a.data()+key,size_t(data-key),32);
        auto value=cleanString(a.data()+val,len);
        if(name=="TITLE")out.title=value;
        else if(name=="DISC_ID" || (name=="TITLE_ID" && out.id.empty()))out.id=idOf(value);
        else if(name=="CATEGORY")out.category=value;
    }
    return out;
}
struct Reader {
    const Read& read; uint64_t size; uint64_t budget=512*1024;
    bool get(uint64_t offset,void* dst,size_t n) {
        if(!within(offset,n,size) || n>budget)return false;
        budget-=n;return read(offset,dst,n);
    }
};
inline Metadata pbp(Reader& r) {
    Metadata out;std::array<unsigned char,40> h{};
    if(!r.get(0,h.data(),h.size()) || std::memcmp(h.data(),"\0PBP",4))return out;
    uint64_t offsets[9]={};
    for(unsigned i=0;i<8;++i)offsets[i]=le32(h.data()+8+i*4);
    offsets[8]=r.size;
    if(offsets[0]<40)return out;
    for(unsigned i=0;i<8;++i)if(offsets[i]>offsets[i+1])return out;
    const auto len=offsets[1]-offsets[0];
    if(len && len<=65536) {
        std::vector<unsigned char> buf(static_cast<size_t>(len));
        if(r.get(offsets[0],buf.data(),buf.size())) {
            auto meta=sfo(buf);out.title=meta.title;out.id=meta.id;
        }
    }
    std::array<unsigned char,8> sig{};
    if(r.get(offsets[7],sig.data(),sig.size())) {
        if(!std::memcmp(sig.data(),"PSISOIMG",8)||!std::memcmp(sig.data(),"PSTITLEI",8))out.platform=Platform::PS1;
        else if(!std::memcmp(sig.data(),"NPUMDIMG",8))out.platform=Platform::PSP;
    }
    if(out.platform==Platform::PS1) {
        const size_t n=static_cast<size_t>(std::min<uint64_t>(2048,offsets[7]-offsets[6]));
        std::vector<unsigned char> buf(n);
        if(n && r.get(offsets[6],buf.data(),n))for(size_t i=0;i+36<=n;++i) {
            if(buf[i+6]!='-' || buf[i+16]!='_' || buf[i+19]!='-')continue;
            bool valid=true;
            for(size_t j=0;j<36;++j) {
                const unsigned char c=buf[i+j];
                if(j==6||j==16||j==19)continue;
                if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9'))){valid=false;break;}
            }
            if(!valid || buf[i+17]<'0'||buf[i+17]>'9'||buf[i+18]<'0'||buf[i+18]>'9')continue;
            out.contentId=idOf(std::string(reinterpret_cast<const char*>(buf.data()+i+7),9));
            if(!out.contentId.empty())break;
        }
    }
    return out;
}
constexpr uint64_t sectorSize=2048;
struct Extent {uint32_t sector=0,bytes=0;bool dir=false;};
inline bool extent(const unsigned char* p,size_t n,Extent& out) {
    if(n<34 || p[0]<34 || p[0]>n || p[32]>p[0]-33 || p[26] || p[27] || (p[25]&0x80))return false;
    out={le32(p+2),le32(p+10),(p[25]&2)!=0};
    return out.bytes!=0;
}
// ISO9660 primary volume only; no Joliet/UDF or arbitrary filesystem traversal.
inline Metadata iso(Reader& r) {
    Metadata out;
    std::array<unsigned char,2048> block{};
    Extent root{};
    for(unsigned s=16;s<32;++s) {
        if(!r.get(uint64_t(s)*sectorSize,block.data(),block.size()) ||
           std::memcmp(block.data()+1,"CD001",5) || block[6]!=1)return out;
        if(block[0]==255)return out;
        if(block[0]==1) {
            if(le16(block.data()+128)!=sectorSize || !extent(block.data()+156,block.size()-156,root) || !root.dir)return out;
            break;
        }
    }
    auto find=[&](const Extent& dir,const char* wanted,Extent& result)->bool {
        const uint64_t base=uint64_t(dir.sector)*sectorSize;
        if(!dir.dir || !dir.bytes || dir.bytes>65536 || !within(base,dir.bytes,r.size))return false;
        for(uint32_t pos=0;pos<dir.bytes;pos+=2048) {
            const size_t n=std::min<size_t>(2048,dir.bytes-pos);
            if(!r.get(base+pos,block.data(),n))return false;
            for(size_t i=0;i<n && block[i];) {
                Extent item;
                const size_t len=block[i];
                if(!extent(block.data()+i,n-i,item))return false;
                const auto* p=block.data()+i;
                std::string name(reinterpret_cast<const char*>(p+33),p[32]);
                const auto semi=name.find(';');if(semi!=std::string::npos)name.resize(semi);
                if(name==wanted){result=item;return true;}
                i+=len;
            }
        }
        return false;
    };
    Extent game{},param{};
    if(!find(root,"PSP_GAME",game)||!find(game,"PARAM.SFO",param)||param.dir||param.bytes>65536)return out;
    std::vector<unsigned char> buf(param.bytes);
    if(!r.get(uint64_t(param.sector)*sectorSize,buf.data(),buf.size()))return out;
    auto m=sfo(buf);
    if(!m.title.empty() || !m.id.empty()){out.platform=Platform::PSP;out.title=m.title;out.id=m.id;}
    return out;
}
// CISO v1, 2048-byte blocks. Other variants deliberately fall back to unknown.
inline Metadata cso(Reader& physical) {
    std::array<unsigned char,24> h{};
    if(!physical.get(0,h.data(),h.size())||std::memcmp(h.data(),"CISO",4))return {};
    const uint32_t header=le32(h.data()+4),blockSize=le32(h.data()+16);
    const uint64_t size=le64(h.data()+8);
    const unsigned shift=h[21];
    if(header<24||blockSize!=2048||h[20]!=1||shift>16||size<sectorSize*17||size%sectorSize)return {};
    const uint64_t count=size/sectorSize;
    if(count>UINT32_MAX || !within(header,(count+1)*4,physical.size))return {};
    const uint64_t minData=uint64_t(header)+(count+1)*4;
    std::array<unsigned char,2048> plain{};
    std::vector<unsigned char> compressed;
    uint64_t cached=UINT64_MAX;
    Read logical=[&](uint64_t off,void* dst,size_t n) {
        auto* out=static_cast<unsigned char*>(dst);
        if(!within(off,n,size))return false;
        while(n) {
            const uint64_t idx=off/2048;
            if(cached!=idx) {
                unsigned char pair[8]{};
                if(!physical.get(uint64_t(header)+idx*4,pair,8))return false;
                const uint32_t a=le32(pair),b=le32(pair+4);
                const uint64_t start=uint64_t(a&0x7fffffff)<<shift,end=uint64_t(b&0x7fffffff)<<shift;
                if(start<minData||end<=start||!within(start,end-start,physical.size))return false;
                if(a&0x80000000u) {
                    if(end-start<2048 || !physical.get(start,plain.data(),plain.size()))return false;
                } else {
                    if(end-start>65536)return false;
                    compressed.resize(static_cast<size_t>(end-start));
                    if(!physical.get(start,compressed.data(),compressed.size()))return false;
                    z_stream zs{};zs.next_in=compressed.data();zs.avail_in=static_cast<uInt>(compressed.size());
                    zs.next_out=plain.data();zs.avail_out=static_cast<uInt>(plain.size());
                    if(inflateInit2(&zs,-15)!=Z_OK)return false;
                    const int rc=inflate(&zs,Z_FINISH);
                    const bool ok=rc==Z_STREAM_END && zs.total_out==plain.size();
                    inflateEnd(&zs);if(!ok)return false;
                }
                cached=idx;
            }
            const size_t at=static_cast<size_t>(off%2048),take=std::min(n,2048-at);
            std::memcpy(out,plain.data()+at,take);out+=take;off+=take;n-=take;
        }
        return true;
    };
    Reader image{logical,size,256*1024};return iso(image);
}
inline Metadata probe(const Read& read,uint64_t size) {
    Reader r{read,size};unsigned char h[4]{};
    if(!r.get(0,h,4))return {};
    if(!std::memcmp(h,"\0PBP",4))return pbp(r);
    if(!std::memcmp(h,"CISO",4))return cso(r);
    return iso(r);
}
}}} // namespace psvitaalive::storage::media
