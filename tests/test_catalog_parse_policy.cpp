#include "catalog/catalog_parse_policy.hpp"
#include <cassert>
#include <map>

using namespace psvitaalive::catalog_parse;

struct FakeValue {
    bool present = false;
    std::string text;
    std::vector<FakeValue> entries;
    explicit operator bool() const { return present; }
    const std::string& getString() const { return text; }
    const auto& getArray() const { return entries; }
    const FakeValue& operator[](std::size_t i) const { return entries[i]; }
};
struct FakeObject {
    std::map<std::string, FakeValue> fields;
    mutable unsigned reads = 0;
    const FakeValue& operator[](const char* key) const {
        ++reads;
        static const FakeValue missing{};
        const auto found = fields.find(key);
        return found == fields.end() ? missing : found->second;
    }
};

int main() {
    FakeObject v1{{{"name", {true, "old", {}}}}};
    FakeObject v2{{{"n", {true, "new", {}}}}};
    assert(field(v1, "name", "n").text == "old" && v1.reads == 1);
    assert(field(v2, "name", "n").text == "new" && v2.reads == 2);
    FakeObject mixed{{{"name", {true, "", {}}}, {"n", {true, "new", {}}}}};
    assert(field(mixed, "name", "n").text.empty() && mixed.reads == 1);
    FakeObject missing;
    assert(!field(missing, "name", "n") && missing.reads == 2);
    assert(!field(missing, "id") && missing.reads == 3);
    FakeValue array{true, "", {{false, "", {}}, {true, "", {}},
        {true, "a", {}}, {true, "b", {}}, {true, "", {}},
        {true, "c", {}}, {true, "d", {}}, {true, "e", {}}}};
    std::vector<std::string> limited, unlimited;
    appendStrings(array, limited, 4);
    appendStrings(array, unlimited);
    assert((limited == std::vector<std::string>{"a", "b", "c", "d"}));
    assert((unlimited == std::vector<std::string>{"a", "b", "c", "d", "e"}));
    assert(initialItemReserve(0) == 0);
    assert(initialItemReserve(23) == 23);
    assert(initialItemReserve(512) == 512);
    assert(initialItemReserve(1000000) == 512);
    for (const auto* prefix : {"H", "PV", "PSP", "PS1", "U"}) {
        for (const auto* owner : {"", "PSVAS1178", "app_123", "\xC3\xA9"}) {
            for (const auto* role : {"cover", "icon", "shot0", "shot1", "shot2", "shot3"}) {
                std::uint64_t old = 1469598103934665603ULL;
                for (unsigned char c : std::string(prefix) + "|" + owner + "|" + role) {
                    old ^= c;
                    old *= 1099511628211ULL;
                }
                assert(imageResourceHash(prefix, owner, role) == old);
            }
        }
    }
}
