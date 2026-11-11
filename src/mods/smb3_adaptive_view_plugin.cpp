#include "mod_runtime.h"

namespace {
void reset_view() { (void)gba_mod_set_adaptive_view_enabled(0); }
void activate_view() {
    (void)gba_mod_set_view_width(288);
    (void)gba_mod_set_adaptive_view_enabled(1);
}
}
GBA_MOD_CONSTRUCTOR(smb3_register_adaptive_view) {
    (void)gba_mod_register_reset_callback(reset_view);
    (void)gba_mod_register_activation_plugin("sma4.adaptive-view", activate_view);
}
