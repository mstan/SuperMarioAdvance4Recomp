#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "runtime.h"
#include "runtime_arm.h"
#include "smb3_extended_view.h"
#include "smb3_entity_view.h"

#if defined(GBAGAME_RECOMP_UI)
#include "game_launcher_boot.h"
#endif

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
                "SuperMarioAdvance4Recomp [--bios <path>] [--rom <path>] [game.toml]\n"
                "\n"
                "Windowed play opens the recomp-ui launcher first. "
                "--no-launcher skips it\n"
                "for one run; --launcher forces it past the persisted "
                "skip-launcher setting.\n"
                "Press Escape during play to open the in-game menu.\n");
            return 0;
        }
    }

    g_runtime_ram_dispatch_hook = &dispatch_flash_stack_variant;
    if (!smb3::register_entity_view_state()) {
        std::fprintf(stderr, "Unable to register the entity-view state provider.\n");
        return 1;
    }
    gbarecomp::RunOptions opts;
    opts.launcher_game_version = GBARECOMP_GAME_VERSION;
    opts.builtin_game_name = "Super Mario Advance 4: Super Mario Bros. 3 (USA, Rev 1)";
    opts.builtin_rom_sha1 = "532f3307021637474b6dd37da059ca360f612337";
    opts.mod_game_id = "super-mario-advance-4-us-rev1";
    opts.mod_owns_adaptive_view = true;
    opts.max_view_width = 480;
    opts.max_resize_view_width = 480;
    opts.resize_driven_view = true;
    opts.extended_view_init = &smb3::install_extended_view;
    opts.extended_view_frame = &smb3::update_extended_view;
    opts.launcher_region = "USA";
    opts.launcher_game_config = "game.toml";
    opts.launcher_save_path = "saves/super_mario_advance_4_usa_rev1.sav";
    opts.launcher_expose_widescreen = false;
    opts.launcher_expose_adaptive_view = false;

#if defined(GBAGAME_RECOMP_UI)
    std::vector<std::string> args(argv, argv + argc);
    if (game_launcher_preboot(args, opts)) return 0;
    std::vector<char*> av;
    av.reserve(args.size());
    for (auto& arg : args) av.push_back(arg.data());
    return gbarecomp::run_game(static_cast<int>(av.size()), av.data(), opts);
#else
    return gbarecomp::run_game(argc, argv, opts);
#endif
}
