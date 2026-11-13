#include "smb3_extended_view.h"
#include "gba_bus.h"
#include "gba_ppu.h"
#include "runtime.h"
#include "runtime_bus_bridge.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <vector>

namespace {
void require(bool result, const char* message) {
    if (result) return;
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
}
}
namespace smb3 {
int entity_view_object_x(int, std::uint16_t, std::uint16_t,
                         std::uint16_t, int*) { return 0; }
}

int main() {
    auto bus = std::make_unique<gba::GbaBus>();
    gbarecomp::set_active_bus(bus.get());
    bus->iwram_ptr()[0x77d8] = 1;
    bus->iwram_ptr()[0x3774] = 0x11;
    bus->write16(0x04000008, 0xd600);
    bus->write16(0x0400000a, 0xda03);
    bus->write16(0x04000048, 0x0018);
    bus->write16(0x0400004a, 0x0017);
    gbarecomp::ExtendedViewFrameInfo frame;
    frame.view_width = 288;
    frame.extra_left = frame.extra_right = 24;
    smb3::install_extended_view(24, 24);
    smb3::update_extended_view(&frame);
    require(gba::g_ws_window_x_provider != nullptr, "Map window provider missing");
    // The native panel's BG3/OBJ rectangle must agree with its original
    // horizontal window on every row, including the added screen margins.
    for (int y : {0, 40, 96, 159}) {
        for (int x = -24; x < 264; ++x) {
            int mapped = -999;
            require(gba::g_ws_window_x_provider(x, y, &mapped) == 1 && mapped == x,
                    "Modal panel window moved relative to its native pixels");
        }
    }
    // A real map wipe still expands its aperture over the complete wide
    // canvas instead of reverting to a centered 240-pixel transition.
    bus->write16(0x04000048, 0x003f);
    bus->write16(0x0400004a, 0x0000);
    smb3::update_extended_view(&frame);
    for (int x = -24; x < 264; ++x) {
        int mapped = -999;
        require(gba::g_ws_window_x_provider(x, 80, &mapped) == 1 &&
                mapped >= 0 && mapped < 240,
                "Full native wipe does not cover the wide canvas");
    }
    int mapped = -999;
    gba::g_ws_window_x_provider(-24, 80, &mapped);
    require(mapped != -24, "Map wipe expansion was disabled");

    // Vertical room data uses 240px screens down each 4KiB column bank.
    // Exercise the real provider across both native addressing boundaries.
    std::vector<std::uint8_t> rom(0x140000);
    const unsigned table = 0x124d00;
    for (unsigned i = 0; i < 4; ++i)
        rom[0x124c64 + 8 * 4 + i] = (0x08000000u + table) >> (i * 8);
    rom[table + 2 * 8] = 0x34;
    rom[table + 2 * 8 + 1] = 0x12;
    rom[table + 3 * 8] = 0x67;
    rom[table + 3 * 8 + 1] = 0x45;
    bus->set_rom(rom.data(), rom.size());
    auto* ram = bus->iwram_ptr();
    ram[0x3774] = 0x1a;
    ram[0x3849] = 8;
    ram[0x3cb3] = 0x10;
    ram[0x3ae6] = 1;
    bus->ewram_ptr()[0x2e000 + 0x10f0] = 2;
    bus->ewram_ptr()[0x2e000 + 0x04f0] = 3;
    bus->write16(0x04000008, 0x9400);
    bus->write16(0x0400000a, 0xd801);
    bus->write16(0x04000014, 0);
    bus->write16(0x04000016, 240);
    bus->write16(0x03003754, 240);
    smb3::update_extended_view(&frame);
    require(!smb3::horizontal_level_loaded(bus.get()),
            "Vertical geometry was not identified independently of horizontal rooms");
    require(gba::g_ws_pillarbox == 0, "Vertical room still pillarboxed");
    int source_x = -1, source_y = -1;
    require(gba::g_ws_bg_xy_provider(0, 24, 0, &source_x, &source_y) == 1 &&
            source_x == 0 && source_y == 0, "Vertical top HUD was not centered");
    int card_x = -1;
    require(gba::g_ws_obj_attr_x_provider(0, 143, 0x4000 | 189, 0x8000, &card_x) == 1 &&
            card_x == 237, "Vertical HUD card was not attached to the wide right edge");
    std::uint16_t entry = 0;
    require(gba::g_ws_tilemap_provider(1, 256, 0, &entry) == gba::kWsTilemapReplace &&
            entry == 0x1234, "Vertical tilemap used horizontal bank or 256px row stride");
    require(gba::g_ws_tilemap_provider(1, 1024, 0, &entry) == gba::kWsTilemapUnavailable,
            "Vertical tilemap exceeded its four column banks");
    bus->write16(0x04000016, 480);
    bus->write16(0x03003754, 480);
    require(gba::g_ws_tilemap_provider(1, 256, 0, &entry) == gba::kWsTilemapUnavailable,
            "Vertical tilemap exceeded the authored final screen");
    bus->write16(0x04000016, 240);
    bus->write16(0x03003754, 240);
    ram[0x3cb3] = 0;
    ram[0x3ae6] = 3;
    smb3::update_extended_view(&frame);
    require(gba::g_ws_tilemap_provider(1, 256, 0, &entry) == gba::kWsTilemapReplace &&
            entry == 0x4567, "Horizontal tilemap addressing changed");
    ram[0x3cb3] = 1;
    smb3::update_extended_view(&frame);
    require(gba::g_ws_pillarbox,
            "Unknown orientation was treated as loaded geometry");
    gbarecomp::set_active_bus(nullptr);
    return 0;
}
