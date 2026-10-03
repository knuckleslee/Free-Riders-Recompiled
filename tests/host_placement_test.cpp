#include "host_placement.h"
#include <iostream>
#include <stdexcept>

static void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}

int main() {
    try {
        // i5-3470: four cores, no SMT; the main thread is on processor 0.
        require(sfr::without_reserved({0, 1, 2, 3}, 0x1) == std::vector<uint32_t>({1, 2, 3}),
                "the main core leaves the order");
        // SMT: both hardware threads of the main core are reserved.
        require(sfr::without_reserved({0, 2, 4, 6, 1, 3, 5, 7}, 0x3) == std::vector<uint32_t>({2, 4, 6, 3, 5, 7}),
                "both threads of the main core leave the order");
        require(sfr::without_reserved({0, 1}, 0x1) == std::vector<uint32_t>({0, 1}),
                "a two-processor PC keeps sharing");
        require(sfr::without_reserved({0, 1, 2}, 0) == std::vector<uint32_t>({0, 1, 2}), "nothing reserved");
        require(sfr::mask_without_reserved(0xF, 0x1) == 0xE, "mask without the main core");
        require(sfr::mask_without_reserved(0x3, 0x1) == 0x3, "too few left: unchanged mask");
        require(sfr::mask_without_reserved(0xFF, 0x3) == 0xFC, "SMT mask without the main core");
        require(!sfr::main_core_reserved() && sfr::main_core_mask.load() == 0, "off unless SFR_MAIN_CORE=reserve");
        std::cout << "host_placement ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
