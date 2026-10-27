#pragma once
#include <string>
namespace psvitaalive {
struct ZipRecoveryTransaction {
    std::string source, base, info;
    unsigned parts = 1;
};
inline std::string serializeZipRecoveryTransaction(const ZipRecoveryTransaction& t) {
    return "PSVA_ZIP_TXN_V1\n" + t.source + "\n" + t.base + "\n" +
        std::to_string(t.parts) + "\n" + t.info;
}
inline bool parseZipRecoveryTransaction(const std::string& str, const std::string& job,
                                        ZipRecoveryTransaction& output) {
    const std::string magic = "PSVA_ZIP_TXN_V1\n";
    if (str.size() > 10000 || str.compare(0, magic.size(), magic) != 0) return false;
    const size_t a = str.find('\n', magic.size());
    const size_t b = a == std::string::npos ? a : str.find('\n', a+1);
    const size_t c = b == std::string::npos ? b : str.find('\n', b+1);
    if (a == std::string::npos || b == std::string::npos || c == std::string::npos) return false;
    ZipRecoveryTransaction t;
    t.source = str.substr(magic.size(),a-magic.size());
    t.base = str.substr(a+1,b-a-1);
    const std::string count = str.substr(b+1,c-b-1);
    if (count == "1") t.parts = 1;
    else if (count == "2") t.parts = 2;
    else if (count == "4") t.parts = 4;
    else return false;
    t.info = str.substr(c+1);
    const std::string from = job + "/";
    const std::string to = "ux0:data/psvitaalive/manual/";
    if (t.source.compare(0,from.size(),from) != 0 ||
        t.source.size() == from.size() ||
        t.source.find('/',from.size()) != std::string::npos ||
        t.source.find("..") != std::string::npos ||
        t.base.compare(0,to.size(),to) != 0 ||
        t.base.size() < to.size()+6 ||
        t.base.find("..") != std::string::npos ||
        t.base.compare(t.base.size()-4,4,".zip") != 0 ||
        t.base.find('\n') != std::string::npos ||
        t.info.find("PS Vita Alive Store - ZIP manual recovery\n\n") != 0 ||
        t.info.size() > 8192) return false;
    output = t;
    return true;
}
} // namespace psvitaalive
