#include "host_profile_stack.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
// Whether `bytes` (an instruction, then whatever) end a call: the return
// address is just past them, with filler before so every look-back is in range.
static bool ends_call(std::vector<uint8_t> bytes) {
    std::vector<uint8_t> code(8, 0x90);
    code.insert(code.end(), bytes.begin(), bytes.end());
    return sfr::follows_call(code.data() + code.size());
}
#if defined(__x86_64__) || defined(_M_X64)
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
__declspec(noinline) static const void* where_called() { return _ReturnAddress(); }
#else
__attribute__((noinline)) static const void* where_called() { return __builtin_return_address(0); }
#endif
#endif

int main() {
    try {
        require(ends_call({0xE8, 1, 2, 3, 4}), "call rel32");
        require(ends_call({0xFF, 0x15, 1, 2, 3, 4}), "call [rip+disp32]");
        require(ends_call({0xFF, 0x90, 1, 2, 3, 4}), "call [rax+disp32]");
        require(ends_call({0xFF, 0x94, 0x24, 1, 2, 3, 4}), "call [rsp+disp32]");
        require(ends_call({0xFF, 0x50, 0x08}), "call [rax+8]");
        require(ends_call({0xFF, 0x54, 0x24, 0x08}), "call [rsp+8]");
        require(ends_call({0xFF, 0xD0}), "call rax");
        require(ends_call({0x41, 0xFF, 0xD3}), "call r11");
        require(ends_call({0xFF, 0x10}), "call [rax]");
        require(!ends_call({0xFF, 0xE0}), "jmp rax is not a call");
        require(!ends_call({0xFF, 0x20}), "jmp [rax] is not a call");
        require(!ends_call({0xE9, 1, 2, 3, 4}), "jmp rel32 is not a call");
        require(!ends_call({0x48, 0x89, 0x44, 0x24, 0x08}), "mov is not a call");
        require(!ends_call({0xFF, 0x25, 1, 2, 3, 4}), "jmp [rip+disp32] is not a call");
#if defined(__x86_64__) || defined(_M_X64)
        require(sfr::follows_call(static_cast<const uint8_t*>(where_called())), "this compiler's own call");
#endif

        const uint64_t words[] = {5, 100, 7, 200, 300};
        const auto is_return = [](uint64_t word) { return word >= 100; };
        const auto two = sfr::first_return_addresses<2>(words, 5, is_return);
        require(two[0] == 100 && two[1] == 200, "first two from the top");
        const auto none = sfr::first_return_addresses<2>(words, 1, is_return);
        require(none[0] == 0 && none[1] == 0, "none found in a short copy");
        const auto one = sfr::first_return_addresses<2>(words, 3, is_return);
        require(one[0] == 100 && one[1] == 0, "only what the copy holds");
        std::cout << "host_profile_stack ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
