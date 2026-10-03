#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace sfr {
// Whether the x64 bytes ending just before `after` are a call instruction,
// which makes `after` a plausible return address. The host profiler uses it
// to find, in a copy of a sampled thread's stack, which code of this
// executable called into the system DLL or driver the thread was in, without
// unwinding (whose function table lookups may take a lock the suspended
// thread holds). A data word that happens to point just past such bytes
// passes too; a profile only needs most samples right.
inline bool follows_call(const uint8_t* after) {
    if (after[-5] == 0xE8) return true;  // call rel32
    // FF /2: call through a register or memory, by the length of its operand.
    const auto indirect = [](uint8_t modrm) { return (modrm & 0x38) == 0x10; };
    if (after[-6] == 0xFF && indirect(after[-5]) &&
        ((after[-5] & 0xC7) == 0x05 || ((after[-5] & 0xC0) == 0x80 && (after[-5] & 7) != 4)))
        return true;  // [rip+disp32], [reg+disp32]
    if (after[-7] == 0xFF && indirect(after[-6]) && (after[-6] & 0xC7) == 0x84) return true;  // [sib+disp32]
    if (after[-3] == 0xFF && indirect(after[-2]) && (after[-2] & 0xC0) == 0x40 && (after[-2] & 7) != 4)
        return true;  // [reg+disp8]
    if (after[-4] == 0xFF && indirect(after[-3]) && (after[-3] & 0xC7) == 0x44) return true;  // [sib+disp8]
    const uint8_t last = after[-1];
    if (after[-2] == 0xFF && indirect(last) &&
        ((last & 0xC0) == 0xC0 || ((last & 0xC0) == 0 && (last & 7) != 4 && (last & 7) != 5)))
        return true;  // a register, [reg]
    return false;
}

// The first N words of a copied stack, from its top, that is_return accepts;
// 0 for each one not found.
template <size_t N, class IsReturn>
std::array<uint64_t, N> first_return_addresses(const uint64_t* words, size_t count, IsReturn&& is_return) {
    std::array<uint64_t, N> found{};
    size_t next = 0;
    for (size_t i = 0; i < count && next < N; ++i)
        if (is_return(words[i])) found[next++] = words[i];
    return found;
}
}  // namespace sfr
