#include "smb3_extended_view.h"

#include <algorithm>
#include <cstddef>
#include "gba_bus.h"
#include "gba_ppu.h"
#include "runtime.h"
#include "runtime_bus_bridge.h"

namespace smb3 {
namespace {
unsigned read16(const std::uint8_t* p) {
    return unsigned(p[0]) | (unsigned(p[1]) << 8);
}
unsigned read32(const std::uint8_t* p) {
    return read16(p) | (read16(p + 2) << 16);
}

bool horizontal_level(const gba::GbaBus* bus) {
    if (!bus) return false;
    const auto* ram = bus->iwram_ptr();
    // The adapter targets the hash-gated USA revision 1. Menus, transitions,
    // the separate Mario Bros. game and vertical levels retain native framing.
    return ram[0x77d8] == 1 && ram[0x3774] == 0x1a &&
           ram[0x3cb3] == 0 && ram[0x3849] < 19;
}

int tilemap(int bg, int x, int y, std::uint16_t* entry) {
    const auto* bus = gbarecomp::active_bus();
    if (!entry || !horizontal_level(bus) || y < 0 || y >= 160)
        return gba::kWsTilemapUnavailable;
    // BG0 contains screen-space status text; never repeat it in the margins.
    if (bg == 0) return gba::kWsTilemapUnavailable;
    // These backdrop maps are complete ROM-authored, repeating images, unlike
    // the streamed BG1 geometry which must be reconstructed from the level.
    if (bg == 2 || bg == 3) return gba::kWsTilemapKeepWrapped;
    if (bg != 1) return gba::kWsTilemapUnavailable;

    const auto* ram = bus->iwram_ptr();
    const int world_x = x + static_cast<int>(read32(ram + 0x3750));
    const int world_y = y + static_cast<int>(read32(ram + 0x3754));
    if (world_x < 0 || world_x >= 4096 || world_y < 0 || world_y >= 1024)
        return gba::kWsTilemapUnavailable;

    // Horizontal maps hold sixteen 256px screens. Each screen is sixteen
    // columns by sixty-four metatile rows; the high byte is a boolean bank.
    const auto* map = bus->ewram_ptr();
    const unsigned index = unsigned(world_x / 256) * 0x400u +
        unsigned(world_y / 16) * 16u + (unsigned(world_x / 16) & 15u);
    const unsigned tile = map[0x2e000u + index] +
        (map[0x32000u + index] != 0 ? 256u : 0u);
    const unsigned quadrant = ((world_x & 8) ? 2u : 0u) +
                              ((world_y & 8) ? 1u : 0u);
    const unsigned halfword = tile * 4u + quadrant;
    const auto* rom = bus->rom_ptr();
    const unsigned tileset = ram[0x3849];
    if (!rom || bus->rom_size() < 0x124cb0u)
        return gba::kWsTilemapUnavailable;
    unsigned address = read32(rom + 0x124c64u + tileset * 4u);
    // The game's column publisher selects the common extended bank above
    // halfword 0x497 for nonzero tilesets. Entries are raw GBA tilemap words,
    // ordered top-left, bottom-left, top-right, bottom-right.
    if (halfword > 0x497u && tileset != 0) address = 0x0813a038u;
    if (address < 0x08000000u) return gba::kWsTilemapUnavailable;
    const std::size_t offset = address - 0x08000000u + halfword * 2u;
    if (offset + 2u > bus->rom_size()) return gba::kWsTilemapUnavailable;
    *entry = static_cast<std::uint16_t>(read16(rom + offset));
    return gba::kWsTilemapReplace;
}
}  // namespace

void update_extended_view(const gbarecomp::ExtendedViewFrameInfo* frame) {
    const auto* bus = gbarecomp::active_bus();
    const bool field = horizontal_level(bus);
    gba::g_ws_pillarbox = field ? 0 : 1;
    gba::g_ws_native_view_left = -1;
    if (field && frame) {
        const int camera_x = static_cast<int>(read32(bus->iwram_ptr() + 0x3750));
        // At the level's left boundary spend the available width on the real
        // level to the right, rather than exposing negative world coordinates.
        gba::g_ws_native_view_left = std::clamp(camera_x, 0,
                                               static_cast<int>(frame->extra_left));
    }
}

void install_extended_view(std::uint32_t, std::uint32_t) {
    gba::g_ws_tilemap_provider = tilemap;
    gba::g_ws_authored_margin_layers = 1;
    gba::g_ws_pillarbox = 1;
    gba::g_ws_pillarbox_left = 0;
    gba::g_ws_pillarbox_right = 0;
    gba::g_ws_native_view_left = -1;
    gba::publish_read_only_wide_bg_providers(nullptr, true);
}
}  // namespace smb3
