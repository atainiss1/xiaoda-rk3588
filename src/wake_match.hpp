#pragma once
#include <cmath>
#include <cstddef>
namespace rkvoice {
// All supported forms are the four-syllable greeting, encoded as eight phones.
// Reject decoder paths that stitch "你好 ... 我是 ... 小哒" into a wake phrase.
inline bool compact_wake_phrase(const float* times, std::size_t count) {
    if (!times || count!=8) return false;
    for (std::size_t i=0;i<count;++i) {
        if (!std::isfinite(times[i])) return false;
        if (i && (times[i]<times[i-1] || times[i]-times[i-1]>0.5f)) return false;
    }
    const float duration=times[count-1]-times[0];
    return duration>=0.3f && duration<=2.0f;
}
}
