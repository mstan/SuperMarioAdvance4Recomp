#include <cstdio>
#include <cstring>
#include <filesystem>
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

// Build identity. The stock executable runs the unmodified cartridge. The
// VC Edition executable (SMA4_VC_EDITION, see game_vc.toml) is generated from
// the stock ROM plus the VC Edition xdelta, which restores the e-Reader-only
// content. Because the patch changes guest code, it runs only the exact
// patched image its corpus was built from; players select that ROM directly
// (tools/vc_edition.py apply builds it from a stock ROM + the patch).
constexpr char kStockRomSha1[] = "532f3307021637474b6dd37da059ca360f612337";
#if defined(SMA4_VC_EDITION)
#if !defined(SMA4_VC_ROM_SHA1)
#error "SMA4_VC_ROM_SHA1 must be provided by CMake from game_vc.toml"
#endif
constexpr char kRomSha1[] = SMA4_VC_ROM_SHA1;
constexpr char kGameName[] =
    "Super Mario Advance 4: Super Mario Bros. 3 (USA, Rev 1, VC Edition)";
constexpr char kGameConfig[] = "game_vc.toml";
constexpr char kSavePath[] = "saves/super_mario_advance_4_usa_vc.sav";
constexpr char kExeName[] = "SuperMarioAdvance4VCRecomp";
#else
constexpr const char* kRomSha1 = kStockRomSha1;
constexpr char kGameName[] = "Super Mario Advance 4: Super Mario Bros. 3 (USA, Rev 1)";
constexpr char kGameConfig[] = "game.toml";
constexpr char kSavePath[] = "saves/super_mario_advance_4_usa_rev1.sav";
constexpr char kExeName[] = "SuperMarioAdvance4Recomp";
#endif

// gbarecomp's runtime library falls back to "game.toml" in the working
// directory when no positional config is given. The VC Edition must never pick
// up the stock config (its [rom].sha1 would reject the patched image), so name
// game_vc.toml explicitly whenever it is present and the player supplied none.
void select_default_config(std::vector<std::string>& args) {
#if defined(SMA4_VC_EDITION)
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a.size() > 5 && a[0] != '-' &&
            (a.compare(a.size() - 5, 5, ".toml") == 0 ||
             a.compare(a.size() - 5, 5, ".TOML") == 0)) return;
    }
    std::error_code ec;
    if (std::filesystem::exists(kGameConfig, ec)) args.emplace_back(kGameConfig);
#else
    (void)args;
#endif
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 ||
            std::strcmp(argv[i], "-h") == 0) {
            std::printf(
                "%s [--bios <path>] [--rom <path>] [%s]\n"
                "\n"
                "Windowed play opens the recomp-ui launcher first. "
                "--no-launcher skips it\n"
                "for one run; --launcher forces it past the persisted "
                "skip-launcher setting.\n"
                "Press Escape during play to open the in-game menu.\n"
#if defined(SMA4_VC_EDITION)
                "\n"
                "VC Edition: select the ROM already patched with "
                "SMA4_USA_Australia_Rev_1_to_\nUSA_VC_Edition.xdelta "
                "(8 MiB, SHA-1 " SMA4_VC_ROM_SHA1 ").\n"
#endif
                , kExeName, kGameConfig);
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
    opts.builtin_game_name = kGameName;
    opts.builtin_rom_sha1 = kRomSha1;
    opts.enable_rom_archives = true;
    opts.mod_game_id = "super-mario-advance-4-us-rev1";
    opts.mod_owns_adaptive_view = true;
    opts.max_view_width = 480;
    opts.max_resize_view_width = 480;
    opts.resize_driven_view = true;
    opts.extended_view_init = &smb3::install_extended_view;
    opts.extended_view_frame = &smb3::update_extended_view;
    opts.launcher_region = "USA";
    opts.launcher_game_config = kGameConfig;
    opts.launcher_save_path = kSavePath;
#if defined(SMA4_VC_EDITION)
    // Keep this build's launcher state apart from the stock executable's when
    // both sit in one folder: each remembers a different ROM. The BIOS pick
    // and keybinds are shared.
    opts.launcher_config_filename = "config_vc.ini";
    opts.launcher_rom_cache_filename = "rom_vc.cfg";
#endif
    opts.launcher_expose_widescreen = false;
    opts.launcher_expose_adaptive_view = false;

    std::vector<std::string> args(argv, argv + argc);
    select_default_config(args);
#if defined(GBAGAME_RECOMP_UI)
    if (game_launcher_preboot(args, opts)) return 0;
#endif
    std::vector<char*> av;
    av.reserve(args.size());
    for (auto& arg : args) av.push_back(arg.data());
    return gbarecomp::run_game(static_cast<int>(av.size()), av.data(), opts);
}
