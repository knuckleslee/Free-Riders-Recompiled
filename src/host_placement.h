#pragma once
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace sfr {
// SFR_MAIN_CORE=reserve keeps the core the main guest thread is pinned to for
// it alone (Windows): the other guest threads and the render thread are placed
// on the rest. On a four-core PC without SMT the main thread otherwise shares
// its core with a worker whose guest processor maps there, and with whatever
// the scheduler puts beside it. An experiment for the benchmark (main-core,
// all-main-core); by default the core is shared as before.
inline bool main_core_reserved() {
    static const bool reserved = [] {
        const char* text = std::getenv("SFR_MAIN_CORE");
        return text && std::string_view(text) == "reserve";
    }();
    return reserved;
}
// The logical processors of the main guest thread's core, once it is pinned
// to one; 0 until then, or when it was not pinned.
inline std::atomic<uint64_t> main_core_mask{0};

// The fewest processors the others are left with: below this nothing is reserved.
inline constexpr int reserve_leaves_at_least = 2;

// A processor order without the reserved processors, when enough remain.
inline std::vector<uint32_t> without_reserved(const std::vector<uint32_t>& order, uint64_t reserved) {
    std::vector<uint32_t> rest;
    for (const uint32_t processor : order)
        if (processor >= 64 || !(reserved >> processor & 1)) rest.push_back(processor);
    return int(rest.size()) >= reserve_leaves_at_least ? rest : order;
}
// An affinity mask without the reserved processors, when enough remain.
inline uint64_t mask_without_reserved(uint64_t mask, uint64_t reserved) {
    const uint64_t rest = mask & ~reserved;
    return std::popcount(rest) >= reserve_leaves_at_least ? rest : mask;
}
}  // namespace sfr
