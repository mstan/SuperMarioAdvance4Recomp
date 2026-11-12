#pragma once
#include <cstdint>
namespace gbarecomp { struct ExtendedViewFrameInfo; }
namespace gba { class GbaBus; }
namespace smb3 {
bool horizontal_level_loaded(const gba::GbaBus*);
void install_extended_view(std::uint32_t, std::uint32_t);
void update_extended_view(const gbarecomp::ExtendedViewFrameInfo*);
}
