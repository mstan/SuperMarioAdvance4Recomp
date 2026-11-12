#include "mod_runtime.h"
#include "runtime_arm.h"
#include "../smb3_entity_view.h"

namespace {
void reset_view() {
    smb3::set_entity_view_enabled(false);
    g_runtime_bus_read_override = nullptr;
    g_runtime_fn_entry_hook = nullptr;
    (void)gba_mod_set_adaptive_view_enabled(0);
}
void activate_view() {
    smb3::set_entity_view_enabled(true);
    g_runtime_bus_read_override = smb3::entity_view_read;
    g_runtime_fn_entry_hook = smb3::observe_entity_view_entry;
    (void)gba_mod_set_view_width(288);
    (void)gba_mod_set_adaptive_view_enabled(1);
}
}
GBA_MOD_CONSTRUCTOR(smb3_register_adaptive_view) {
    (void)gba_mod_register_reset_callback(reset_view);
    (void)gba_mod_register_activation_plugin("sma4.adaptive-view", activate_view);
}
