#pragma once
#include <cstdint>
namespace gbarecomp { struct ExtendedViewFrameInfo; }
namespace smb3 {
void install_extended_view(std::uint32_t, std::uint32_t);
void update_extended_view(const gbarecomp::ExtendedViewFrameInfo*);
}
