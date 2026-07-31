#include <cstdio>
#include <cstring>

#include "runtime.h"

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 ||
            std::strcmp(argv[i], "-h") == 0) {
            std::printf(
                "SuperMarioAdvance4Recomp [--bios <path>] [--rom <path>] [game.toml]\n");
            return 0;
        }
    }

    gbarecomp::RunOptions opts;
    opts.builtin_game_name = "Super Mario Advance 4: Super Mario Bros. 3 (USA, Rev 1)";
    opts.builtin_rom_sha1 = "532f3307021637474b6dd37da059ca360f612337";
    return gbarecomp::run_game(argc, argv, opts);
}
