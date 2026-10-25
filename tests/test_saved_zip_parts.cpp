#include "installer/saved_zip_parts.hpp"
#include <cassert>
using namespace psvitaalive;
int main() {
    const std::string first = "ux0:data/manual/Test/Game Files.zip.001";
    assert(savedZipIsFirstPart(first));
    assert(savedZipPartPath(first,0)==first);
    assert(savedZipPartPath(first,1)=="ux0:data/manual/Test/Game Files.zip.002");
    assert(savedZipPartPath(first,3)=="ux0:data/manual/Test/Game Files.zip.004");
    assert(savedZipPartPath(first,4).empty());
    assert(!savedZipIsFirstPart("file.zip"));
    assert(savedZipPartPath("file.zip",1).empty());
}
