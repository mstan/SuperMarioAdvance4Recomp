#include "smb3_extended_view.h"
#include "gba_bus.h"
#include "gba_ppu.h"
#include "runtime.h"
#include "runtime_bus_bridge.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>

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
    gbarecomp::set_active_bus(nullptr);
    return 0;
}
