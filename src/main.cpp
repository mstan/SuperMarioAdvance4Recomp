#include <cstdio>
#include <cstring>

#include "runtime.h"
#include "runtime_arm.h"

namespace {

bool ram_matches_rom(uint32_t ram, uint32_t rom, uint32_t size) {
    for (uint32_t offset = 0; offset < size; ++offset) {
        if (bus_read_u8(ram + offset) != bus_read_u8(rom + offset)) {
            return false;
        }
    }
    return true;
}

bool dispatch_position_independent_copy(uint32_t pc, uint32_t source,
                                        uint32_t size) {
    constexpr uint32_t kIwramStackStart = 0x03007000u;
    constexpr uint32_t kIwramStackEnd = 0x03008000u;
    const uint32_t earliest =
        pc >= size - 2u ? pc - (size - 2u) : kIwramStackStart;
    uint32_t start = earliest < kIwramStackStart
        ? kIwramStackStart
        : earliest;
    start = (start + 1u) & ~1u;
    for (; start <= pc && start + size <= kIwramStackEnd; start += 2u) {
        if (!ram_matches_rom(start, source, size)) continue;
        runtime_dispatch(source + (pc - start));
        return true;
    }
    return false;
}

// Nintendo's flash library builds three position-independent helpers at
// several active-stack depths, and deliberately reuses overlapping slots.
// Address-only dispatch cannot distinguish those live variants. Validate the
// complete bytes against the hash-gated ROM template, translate the current
// instruction offset back to that immutable template, and run its AOT CFG.
// These reviewed routines contain no PC-relative data accesses.
int dispatch_flash_stack_variant(uint32_t pc, int thumb) {
    constexpr uint32_t kIwramStackStart = 0x03007000u;
    constexpr uint32_t kIwramStackEnd = 0x03008000u;

    if (!thumb || pc < kIwramStackStart || pc >= kIwramStackEnd) {
        return 0;
    }
    if (dispatch_position_independent_copy(pc, 0x080E7918u, 0x30u) ||
        dispatch_position_independent_copy(pc, 0x080E7858u, 0x24u) ||
        dispatch_position_independent_copy(pc, 0x080E7814u, 0x04u)) {
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 ||
            std::strcmp(argv[i], "-h") == 0) {
            std::printf(
                "SuperMarioAdvance4Recomp [--bios <path>] [--rom <path>] [game.toml]\n");
            return 0;
        }
    }

    g_runtime_ram_dispatch_hook = &dispatch_flash_stack_variant;
    gbarecomp::RunOptions opts;
    opts.builtin_game_name = "Super Mario Advance 4: Super Mario Bros. 3 (USA, Rev 1)";
    opts.builtin_rom_sha1 = "532f3307021637474b6dd37da059ca360f612337";
    return gbarecomp::run_game(argc, argv, opts);
}
