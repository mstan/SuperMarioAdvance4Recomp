#include "smb3_extended_view.h"

#include <algorithm>
#include <cstddef>
#include "gba_bus.h"
#include "gba_ppu.h"
#include "runtime.h"
#include "runtime_bus_bridge.h"
#include "smb3_entity_view.h"

namespace smb3 {
bool level_loaded(const gba::GbaBus* bus) {
    if (!bus) return false;
    const auto* ram = bus->iwram_ptr();
    const unsigned state = ram[0x3774];
    if (ram[0x77d8] != 1 ||
        (ram[0x3cb3] != 0 && ram[0x3cb3] != 0x10) || ram[0x3849] >= 19)
        return false;
    if (state == 0x19 || state == 0x1a) return true;
    if (state < 0x1b || state > 0x1e) return false;
    const auto* io = bus->io().raw();
    const auto halfword = [io](unsigned offset) {
        return unsigned(io[offset]) | (unsigned(io[offset + 1]) << 8);
    };
    // The return-to-map state also runs after the level RAM is replaced.
    // Retain its last visible level frame, but stop sampling that geometry
    // once the all-layer fade is opaque or the map layout is installed.
    const bool opaque = (halfword(0x50) & 0xffu) == 0xffu &&
                        (halfword(0x54) & 31u) >= 16u;
    return !opaque && halfword(8) == 0x9400u && halfword(10) == 0xd801u;
}
bool horizontal_level_loaded(const gba::GbaBus* bus) {
    return level_loaded(bus) && bus->iwram_ptr()[0x3cb3] == 0;
}
namespace {
int g_output_width = 240;
bool g_goal_clear = false;

unsigned read16(const std::uint8_t* p) {
    return unsigned(p[0]) | (unsigned(p[1]) << 8);
}
unsigned read32(const std::uint8_t* p) {
    return read16(p) | (read16(p + 2) << 16);
}
int signed16(const std::uint8_t* p) {
    const unsigned value = read16(p);
    return value < 0x8000u ? static_cast<int>(value)
                           : static_cast<int>(value) - 0x10000;
}

bool level(const gba::GbaBus* bus) {
    return level_loaded(bus);
}

bool vertical_level(const gba::GbaBus* bus) {
    return bus->iwram_ptr()[0x3cb3] == 0x10;
}

bool periodic_vertical(const gba::GbaBus* bus) {
    if (!level(bus) || !vertical_level(bus)) return false;
    const auto* ram = bus->iwram_ptr();
    const unsigned player = read32(ram + 0x7818);
    // The native vertical OBJ publisher (08042F58) wraps by 240 unless the
    // player's C7 field disables that policy. Mirror its real eligibility;
    // header timer bits and the backing map's 256px banks are unrelated.
    return player >= 0x03000000u && player <= 0x03008000u - 0xc8u &&
           ram[player - 0x03000000u + 0xc7u] == 0;
}

int periodic_x(int x) {
    const int remainder = x % 240;
    return remainder < 0 ? remainder + 240 : remainder;
}

bool entering_level(const gba::GbaBus* bus) {
    if (!bus) return false;
    const auto* ram = bus->iwram_ptr();
    return ram[0x77d8] == 1 && ram[0x3774] >= 0x12 && ram[0x3774] <= 0x19;
}

bool overworld(const gba::GbaBus* bus) {
    if (!bus) return false;
    const auto* ram = bus->iwram_ptr();
    const auto* io = bus->io().raw();
    return ram[0x77d8] == 1 && ram[0x3774] >= 0x11 && ram[0x3774] <= 0x15 &&
           read16(io + 8) == 0xd600u && read16(io + 10) == 0xda03u;
}

int native_origin() {
    return gba::g_ws_native_view_left >= 0 ? gba::g_ws_native_view_left
                                          : (g_output_width - 240) / 2;
}

int display_scroll(const gba::GbaBus* bus, unsigned ram_offset,
                   unsigned io_offset) {
    // RAM supplies the complete world coordinate, while the PPU register is
    // the scroll actually displayed on this row. Match its low nine bits so
    // the authored map has the same texel phase as the native VRAM ring.
    const int full = signed16(bus->iwram_ptr() + ram_offset);
    const unsigned hardware = read16(bus->io().raw() + io_offset) & 511u;
    const unsigned delta = (hardware - static_cast<unsigned>(full) + 256u) & 511u;
    return full + static_cast<int>(delta) - 256;
}

int half_scroll(int x) {
    return x / 2 - (x < 0 && x % 2 != 0 ? 1 : 0);
}

int level_right(const gba::GbaBus* bus) {
    // Vertical rooms use four 256px columns, each with its own 4KiB bank.
    if (vertical_level(bus)) return 1024;
    // Main-header byte 4 supplies the final 256px screen index.
    const unsigned last_screen = bus->iwram_ptr()[0x3ae6];
    return last_screen < 16 ? static_cast<int>((last_screen + 1) * 256) : 4096;
}

bool goal_clear_active(const gba::GbaBus* bus) {
    const auto* ram = bus->iwram_ptr();
    const unsigned actors = read32(ram + 0x7848);
    if (actors < 0x03000000u || actors > 0x03008000u - 18u * 0x70u)
        return false;
    for (unsigned slot = 0; slot < 18; ++slot) {
        const auto* actor = ram + actors - 0x03000000u + slot * 0x70u;
        // The goal panel's idle phase is zero; subsequent phases own the
        // clear presentation. Other actors and inactive slots cannot trigger it.
        if (actor[0x16] && read16(actor + 0x18) == 0x41u && actor[0x13])
            return true;
    }
    return false;
}

bool goal_backdrop_row(const gba::GbaBus* bus, int y) {
    const auto* io = bus->io().raw();
    const unsigned control = read16(io + 10);
    const unsigned wide = (control >> 14) & 1u;
    const unsigned tx = (read16(io + 0x14) & 511u) / 8;
    const unsigned ty = ((read16(io + 0x16) + y) &
                         ((control & 0x8000u) ? 511u : 255u)) / 8;
    const unsigned screen = (ty / 32) * (wide + 1) + (wide ? tx / 32 : 0);
    const unsigned offset = ((control >> 8) & 31u) * 0x800u +
        screen * 0x800u + ((ty % 32) * 32 + tx % 32) * 2;
    return offset + 2 <= 0x10000u && read16(bus->vram_ptr() + offset) == 0x40fcu;
}

int background_coordinates(int bg, int x, int y, int* source_x, int* source_y) {
    const auto* bus = gbarecomp::active_bus();
    if (!source_x || !source_y || !(level(bus) || overworld(bus))) return 0;
    const int origin = native_origin();
    int mapped = x;
    if (bg == 0) {
        // Center the top status line and modal UI. Keep the gameplay power
        // meter at its screen edge; the overworld uses one centered HUD.
        mapped += origin;
        if (overworld(bus) || y < 136)
            mapped -= (g_output_width - 240) / 2;
        if (mapped < 0 || mapped >= 240) return -1;
    } else if ((bg == 2 || bg == 3) && level(bus)) {
        if (periodic_vertical(bus)) mapped = periodic_x(x);
        else {
            const int camera = display_scroll(bus, 0x3750, 0x14);
            // These layers move at half camera speed. Rebase only the camera
            // contribution, retaining the guest's other scroll/animation offsets.
            mapped += origin - (half_scroll(camera) - half_scroll(camera - origin));
        }
    } else return 0;
    if (mapped == x) return 0;
    *source_x = mapped;
    *source_y = y;
    return 1;
}

bool hud_card(int index, std::uint16_t attr0, std::uint16_t attr1,
              std::uint16_t attr2) {
    return index >= 0 && index < 3 &&
        (attr0 & 0xc3ffu) == 143u &&
        (attr1 & 0xfe00u) == 0x4000u &&
        (attr1 & 511u) == 189u + 14u * static_cast<unsigned>(index) &&
        (attr2 & 0xf000u) == 0x8000u;
}

int object_x_period(int index, std::uint16_t attr0, std::uint16_t attr1,
                    std::uint16_t attr2) {
    return periodic_vertical(gbarecomp::active_bus()) &&
           !hud_card(index, attr0, attr1, attr2) ? 240 : 0;
}

int screen_object_x(int index, std::uint16_t attr0, std::uint16_t attr1,
                    std::uint16_t attr2, int* out_x) {
    const auto* bus = gbarecomp::active_bus();
    if (out_x && (level(bus) || overworld(bus)) &&
        hud_card(index, attr0, attr1, attr2)) {
        // Slot geometry identifies the HUD card, including its filled faces.
        const int inset = overworld(bus) ? (g_output_width - 240) / 2
                                         : g_output_width - 240;
        *out_x = static_cast<int>(attr1 & 511u) + inset - native_origin();
        return 1;
    }
    return entity_view_object_x(index, attr0, attr1, attr2, out_x);
}

int transition_window_x(int x, int, int* out_x) {
    if (!out_x) return 0;
    const auto* bus = gbarecomp::active_bus();
    if (periodic_vertical(bus)) {
        *out_x = periodic_x(x);
        return 1;
    }
    if (overworld(bus)) {
        const auto* io = bus->io().raw();
        // The world/lives panel reveals BG3 and OBJ inside its authored
        // rectangle, while BG0-2 and OBJ draw the map outside. Its window
        // must stay aligned with those native pixels; it is not a wipe.
        if ((read16(io + 0x48) & 0x3fu) == 0x18u &&
            (read16(io + 0x4a) & 0x3fu) == 0x17u) {
            *out_x = x;
            return 1;
        }
    }
    int native_center = 120;
    int output_center = g_output_width / 2;
    if (overworld(bus)) {
        const auto* ram = bus->iwram_ptr();
        const unsigned cursor = read32(ram + 0x7824);
        const unsigned player = ram[0x2d77];
        if (cursor >= 0x03000000u && cursor <= 0x03007ff0u && player < 2) {
            const auto* p = ram + cursor - 0x03000000u;
            const int world_x = p[4 + player] + 256 * p[2 + player];
            native_center = std::clamp(world_x -
                static_cast<int>(read16(bus->io().raw() + 0x14) & 511u), 0, 239);
            output_center = native_center + native_origin();
        }
    }
    // Keep the closing wipe attached to the map cursor, and the opening wipe
    // centered in the new canvas. Use the more restrictive edge ratio so a
    // full native aperture covers the entire target width immediately.
    int native_radius = 240 - native_center;
    int output_radius = g_output_width - output_center;
    if (native_center > 0 && output_center > 0 &&
        native_center * output_radius < native_radius * output_center) {
        native_radius = native_center;
        output_radius = output_center;
    }
    const int numerator = (x + native_origin() - output_center) * native_radius;
    const int offset = numerator / output_radius -
        (numerator < 0 && numerator % output_radius != 0 ? 1 : 0);
    *out_x = native_center + offset;
    return 1;
}

int tilemap(int bg, int x, int y, std::uint16_t* entry) {
    const auto* bus = gbarecomp::active_bus();
    if (!entry || y < 0 || y >= 160)
        return gba::kWsTilemapUnavailable;
    if (overworld(bus)) {
        if (bg != 1) return gba::kWsTilemapUnavailable;
        // The map panel has an authentic eight-pixel fringe on each side of
        // its native viewport. Beyond it use the map's repeating soil texture,
        // rather than exposing duplicate nodes from the hardware scroll ring.
        if (x < -8 || x >= 248) {
            *entry = 0x50feu;
            return gba::kWsTilemapReplace;
        }
        return gba::kWsTilemapKeepWrapped;
    }
    if (!level(bus)) return gba::kWsTilemapUnavailable;
    // BG0 contains screen-space status text; never repeat it in the margins.
    if (bg == 0) return gba::kWsTilemapUnavailable;
    // These backdrop maps are complete ROM-authored, repeating images, unlike
    // the streamed BG1 geometry which must be reconstructed from the level.
    if (bg == 2 || bg == 3) return gba::kWsTilemapKeepWrapped;
    if (bg != 1) return gba::kWsTilemapUnavailable;

    // The authored goal area uses an opaque BG1 backdrop instead of the
    // normal sky. Continue its native edge into the added clear-screen margin,
    // while keeping rows containing terrain and all native columns intact.
    if (g_goal_clear && x < 0 && goal_backdrop_row(bus, y)) {
        *entry = 0x40fcu;
        return gba::kWsTilemapReplace;
    }

    const auto* ram = bus->iwram_ptr();
    const int world_x = (periodic_vertical(bus) ? periodic_x(x) : x) +
                        display_scroll(bus, 0x3750, 0x14);
    const int world_y = y + display_scroll(bus, 0x3754, 0x16);
    const bool vertical = vertical_level(bus);
    const int bottom = vertical ? (std::min<unsigned>(ram[0x3ae6], 15u) + 1) * 240 : 1024;
    if (world_x < 0 || world_x >= level_right(bus) || world_y < 0 || world_y >= bottom)
        return gba::kWsTilemapUnavailable;

    // The native vertical screen-pointer table advances 0xF0 bytes per
    // 240px screen; X bits 8-9 select 4KiB column banks. Horizontal maps
    // instead select 1KiB banks by X. Both store sixteen metatiles per row.
    const auto* map = bus->ewram_ptr();
    const unsigned index = unsigned(world_x / 256) * (vertical ? 0x1000u : 0x400u) +
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
    const auto* pointer = rom + 0x124c64u + tileset * 4u;
    unsigned address = read16(pointer) | (read16(pointer + 2) << 16);
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
    const bool field = level(bus);
    const bool map = overworld(bus);
    g_goal_clear = field && goal_clear_active(bus);
    const bool leaving = bus && bus->iwram_ptr()[0x77d8] == 1 &&
        bus->iwram_ptr()[0x3774] >= 0x1b && bus->iwram_ptr()[0x3774] <= 0x1e;
    if (frame) g_output_width = static_cast<int>(frame->view_width);
    gba::g_ws_pillarbox = field || map || entering_level(bus) || leaving ? 0 : 1;
    gba::g_ws_window_x_provider = map || entering_level(bus) || periodic_vertical(bus)
        ? transition_window_x : nullptr;
    gba::g_ws_obj_window_x_provider = periodic_vertical(bus) ? transition_window_x : nullptr;
    gba::g_ws_native_view_left = -1;
    if (field && frame) {
        if (periodic_vertical(bus)) {
            // A cyclic field has no left/right boundary. Keep the canonical
            // viewport centered and continue its actual 240px world period.
            gba::g_ws_native_view_left = static_cast<int>(frame->extra_left);
            return;
        }
        const int camera_x = display_scroll(bus, 0x3750, 0x14);
        // At the level's left boundary spend the available width on the real
        // level to the right, rather than exposing negative world coordinates.
        gba::g_ws_native_view_left = std::clamp(camera_x, 0,
                                               static_cast<int>(frame->extra_left));
        // Stop the expanded camera at the same authored level edge as the
        // native camera. Its last screen consumes all extra width on the left,
        // so the clear walk exits the canvas instead of entering unused tiles.
        const int right = level_right(bus);
        if (right >= g_output_width) {
            const int padding = g_output_width - 240;
            gba::g_ws_native_view_left = std::max(gba::g_ws_native_view_left,
                std::clamp(camera_x + g_output_width - right, 0, padding));
        }
    }
}

void install_extended_view(std::uint32_t left, std::uint32_t right) {
    g_output_width = static_cast<int>(left + 240 + right);
    gba::g_ws_tilemap_provider = tilemap;
    gba::g_ws_bg_xy_provider = background_coordinates;
    gba::g_ws_bg_xy_provider_layers = (1u << 0) | (1u << 2) | (1u << 3);
    gba::g_ws_obj_attr_x_provider = screen_object_x;
    gba::g_ws_obj_x_period_provider = object_x_period;
    gba::g_ws_authored_margin_layers = 1;
    gba::g_ws_pillarbox = 1;
    gba::g_ws_pillarbox_left = 0;
    gba::g_ws_pillarbox_right = 0;
    gba::g_ws_native_view_left = -1;
    gba::g_ws_window_x_provider = nullptr;
    gba::g_ws_obj_window_x_provider = nullptr;
    gba::publish_read_only_wide_bg_providers(nullptr, true);
}
}  // namespace smb3
