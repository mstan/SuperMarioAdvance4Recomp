#pragma once
#include <cstdint>

namespace smb3 {
// Register once during startup, before any save/load. This adds a build-local
// snapshot section; prior snapshots without this provider are rejected.
bool register_entity_view_state();
// The catalog controls this fixed simulation profile, independently of window
// size. All peers must use the same profile. Disabling restores native rules.
void set_entity_view_enabled(bool enabled);
int entity_view_read(std::uint32_t pc, std::uint32_t address,
                     std::uint32_t width, std::uint32_t value,
                     std::uint32_t* replacement);
// Read-only provenance for native sprite publications; chain with other entry
// observers and OBJ providers rather than replacing them.
void observe_entity_view_entry(std::uint32_t pc);
int entity_view_object_x(int index, std::uint16_t attr0, std::uint16_t attr1,
                         std::uint16_t attr2, int* x);
}
