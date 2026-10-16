#pragma once
#include <string>

namespace iiCameraIntergration::detail {
inline bool validDeviceId(const std::string& id) {
    if (id.empty()) return false;
    for (std::size_t i = 0; i < id.size();) {
        const auto first = static_cast<unsigned char>(id[i++]);
        if (!first) return false;
        if (first < 0x80) continue;
        unsigned count{}, code{}, minimum{};
        if (first >= 0xc2 && first <= 0xdf) { count=1; code=first&0x1f; minimum=0x80; }
        else if (first >= 0xe0 && first <= 0xef) { count=2; code=first&0xf; minimum=0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { count=3; code=first&7; minimum=0x10000; }
        else return false;
        if (count > id.size()-i) return false;
        while (count--) {
            const auto next=static_cast<unsigned char>(id[i++]);
            if ((next&0xc0)!=0x80) return false;
            code=(code<<6)|(next&0x3f);
        }
        if (code<minimum || code>0x10ffff || (code>=0xd800 && code<=0xdfff)) return false;
    }
    return true;
}
} // namespace iiCameraIntergration::detail
