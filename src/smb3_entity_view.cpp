#include "smb3_entity_view.h"
#include "gba_bus.h"
#include "runtime.h"
#include "runtime_arm.h"
#include "runtime_bus_bridge.h"
#include "mod_state.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace smb3 {
namespace {
bool enabled = false;
std::uint32_t entity = 0;
struct Publication {
    std::uint32_t entity, world_x;
    int x;
    std::uint16_t a0, a1, a2, id;
};
std::array<Publication, 128> publications{};
std::array<Publication, 128> previous_publications{};
unsigned publication_count = 0;
unsigned previous_count = 0;
bool initial_scan = false;
int initial_camera = 0;
std::array<std::uint8_t, 0x400> before_oam{};
const gba::GbaBus* context_bus = nullptr;
unsigned long long context_epoch = 0;
bool restored_context = false;
void clear_context() {
    entity = 0;
    publication_count = previous_count = 0;
    initial_scan = false;
    initial_camera = 0;
    restored_context = false;
}
void validate_context(const gba::GbaBus* bus) {
    if (context_bus != bus || context_epoch != g_runtime_state_epoch) {
        // Snapshot restoration happens before the runtime advances its epoch.
        // Adopt only metadata supplied by that explicit restore, on that bus.
        // Every ordinary bus/epoch change discards the speculative cache.
        if (context_bus != bus || !restored_context) clear_context();
        context_bus = bus;
        context_epoch = g_runtime_state_epoch;
    }
    restored_context = false;
}
std::uint32_t u32(const std::uint8_t* p) {
    return p[0] | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
unsigned u16(const std::uint8_t* p) { return p[0] | (unsigned(p[1]) << 8); }
bool field(const gba::GbaBus* bus) {
    if (!enabled || !bus) return false;
    const auto* r = bus->iwram_ptr();
    return r[0x77d8] == 1 && (r[0x3774] == 0x19 || r[0x3774] == 0x1a) &&
           r[0x3cb3] == 0 && r[0x3849] < 19;
}
int camera(const gba::GbaBus* bus) {
    return static_cast<std::int16_t>(u16(bus->iwram_ptr() + 0x3750));
}
int origin(int cam) { return std::clamp(cam, 0, 120); }
bool sprite_address(const gba::GbaBus* bus, std::uint32_t address) {
    const auto start = u32(bus->iwram_ptr() + 0x7848);
    return start >= 0x03000000u && start <= 0x03007820u &&
           address >= start && address < start + 18u * 0x70u &&
           (address - start) % 0x70u == 0;
}
// These two reads feed only native horizontal visibility/despawn predicates.
// Map the wider interior into their accepted interval, retaining distances
// outside both edges. Stored positions, physics and collision reads are intact.
std::uint32_t visibility_position(std::uint32_t fixed_x, int cam) {
    int x = (static_cast<std::int32_t>(fixed_x) >> 8) - cam + origin(cam);
    x = x < 0 ? x : x >= 480 ? x - 240 : std::min(x, 239);
    return (std::uint32_t(cam + x) << 8) | (fixed_x & 255u);
}

bool valid_entity(std::uint32_t address) {
    return address >= 0x03000000u && address <= 0x03007f90u &&
           (address & 3u) == 0;
}
using gbarecomp::debug::SnapshotReader;
using gbarecomp::debug::SnapshotWriter;
struct SavedContext {
    std::uint32_t entity = 0;
    bool scan = false;
    int camera = 0;
    std::array<std::uint8_t, 0x400> before{};
    std::array<Publication, 128> current{}, previous{};
    unsigned current_count = 0, previous_count = 0;
};
void write_publications(SnapshotWriter& out,
                        const std::array<Publication, 128>& entries,
                        unsigned count) {
    out.u16(static_cast<std::uint16_t>(count));
    for (unsigned i = 0; i < count; ++i) {
        const auto& p = entries[i];
        out.u32(p.entity); out.u32(p.world_x); out.u32(std::uint32_t(p.x));
        out.u16(p.a0); out.u16(p.a1); out.u16(p.a2); out.u16(p.id);
    }
}
bool read_publications(SnapshotReader& in,
                       std::array<Publication, 128>& entries, unsigned& count) {
    count = in.u16();
    if (count > entries.size()) return false;
    for (unsigned i = 0; i < count; ++i) {
        auto& p = entries[i];
        p.entity = in.u32(); p.world_x = in.u32();
        p.x = static_cast<std::int32_t>(in.u32());
        p.a0 = in.u16(); p.a1 = in.u16(); p.a2 = in.u16(); p.id = in.u16();
        if (!valid_entity(p.entity) || p.x < -32768 || p.x > 32767 ||
            (std::uint32_t(p.x) & 511u) != (p.a1 & 511u) || p.id > 0x12cu)
            return false;
    }
    return in.ok();
}
bool read_context(SnapshotReader& in, SavedContext& state) {
    state.entity = in.u32();
    const unsigned scan = in.u8();
    state.scan = scan != 0;
    state.camera = static_cast<std::int16_t>(in.u16());
    in.bytes(state.before.data(), state.before.size());
    return (state.entity == 0 || valid_entity(state.entity)) && scan <= 1 &&
        read_publications(in, state.current, state.current_count) &&
        read_publications(in, state.previous, state.previous_count) &&
        in.ok() && in.remaining() == 0;
}
bool save_context(void*, SnapshotWriter& out, std::string*) {
    validate_context(gbarecomp::active_bus());
    out.u32(entity); out.boolean(initial_scan);
    out.u16(static_cast<std::uint16_t>(initial_camera));
    out.bytes(before_oam.data(), before_oam.size());
    write_publications(out, publications, publication_count);
    write_publications(out, previous_publications, previous_count);
    return true;
}
bool preflight_context(void*, SnapshotReader& in, std::string* error) {
    SavedContext state;
    if (read_context(in, state)) return true;
    if (error) *error = "invalid SMB3 entity publication state";
    return false;
}
void restore_context(void*, SnapshotReader& in) {
    SavedContext state;
    (void)read_context(in, state); // Identical payload already preflighted.
    entity = state.entity; initial_scan = state.scan; initial_camera = state.camera;
    before_oam = state.before;
    publications = state.current; publication_count = state.current_count;
    previous_publications = state.previous; previous_count = state.previous_count;
    context_bus = gbarecomp::active_bus();
    context_epoch = g_runtime_state_epoch;
    restored_context = true;
}
}

bool register_entity_view_state() {
    static bool registered = false;
    if (registered) return true;
    std::string error;
    registered = gbarecomp::debug::register_mod_state_provider(
        {"sma4.entity-view", 1, nullptr, save_context,
         preflight_context, restore_context}, &error);
    if (!registered) std::fprintf(stderr, "%s\n", error.c_str());
    return registered;
}

void set_entity_view_enabled(bool value) {
    enabled = value;
    clear_context();
    context_bus = nullptr;
}

int entity_view_read(std::uint32_t pc, std::uint32_t address,
                     std::uint32_t width, std::uint32_t value,
                     std::uint32_t* out) {
    // Filter before touching RAM: ordinary guest reads keep a cheap fast exit.
    if (pc != 0x08049cf6u && pc != 0x0804a82au &&
        pc != 0x080461beu && pc != 0x080481d4u &&
        pc != 0x080462bau && pc != 0x080462c0u) return 0;
    const auto* bus = gbarecomp::active_bus();
    validate_context(bus);
    if (!out || !field(bus)) return 0;
    const int cam = camera(bus);
    const int left = origin(initial_scan ? initial_camera : cam);
    if (pc == 0x08049cf6u && width == 2) {
        // Native source-column activation: right edge +32, left edge -48.
        // Keep one canonical 480px simulation area even at a 240px window.
        if (address == 0x08155394u && value == 272u) {
            *out = std::uint16_t(512 - left); return 1;
        }
        if (address == 0x08155396u && value == 0xffd0u) {
            *out = std::uint16_t(-48 - left); return 1;
        }
    }
    // The initial source scan walks every 16px column before gameplay; extend
    // its end by exactly the additional 240px. Its temporary camera is native.
    if (pc == 0x0804a82au && width == 2 &&
        address == 0x08155394u && value == 272u) {
        *out = 512u; return 1;
    }
    if ((pc == 0x080461beu || pc == 0x080481d4u) && width == 4 &&
        sprite_address(bus, address)) {
        *out = visibility_position(value, cam); return 1;
    }
    if (pc == 0x080462bau && width == 2 && address >= 12u &&
        sprite_address(bus, address - 12u)) {
        *out = std::uint16_t(value + left); return 1;
    }
    if (pc == 0x080462c0u && width == 4 &&
        address == 0x08046334u && value == 0x011f0000u) {
        *out = 0x020f0000u; return 1;
    }
    return 0;
}

void observe_entity_view_entry(std::uint32_t pc) {
    if (pc != 0x08042b60u && pc != 0x080429c8u && pc != 0x08047ad4u &&
        pc != 0x0804a4b0u && pc != 0x0804a928u)
        return;
    const auto* bus = gbarecomp::active_bus();
    validate_context(bus);
    if (!field(bus)) { entity = 0; publication_count = previous_count = 0;
        initial_scan = false; return; }
    if (pc == 0x0804a4b0u) {
        initial_scan = true; initial_camera = camera(bus); return;
    }
    if (pc == 0x0804a928u) { initial_scan = false; return; }
    if (pc == 0x08042b60u) {
        previous_publications = publications;
        previous_count = publication_count;
        entity = 0; publication_count = 0; return;
    }
    if (pc == 0x080429c8u) {
        entity = sprite_address(bus, g_cpu.R[0]) ? g_cpu.R[0] : 0;
        if (entity) std::memcpy(before_oam.data(), bus->iwram_ptr() + 0x2dc8,
                                before_oam.size());
        return;
    }
    // The native loop calls 47AD4 immediately after ProcSprite, before OAM
    // compaction. Observe only changed publications from that entity, including
    // writers using the reverse allocation pool. No OAM-index guess is needed.
    if (!entity || entity != g_cpu.R[0]) return;
    const auto* ram = bus->iwram_ptr();
    const auto world = u32(ram + entity - 0x03000000u);
    const int cam = camera(bus);
    const int relative = (static_cast<std::int32_t>(world) >> 8) - cam;
    for (unsigned i = 0; i < 128 && publication_count < publications.size(); ++i) {
        const auto* p = ram + 0x2dc8 + i * 8u;
        if (std::memcmp(p, before_oam.data() + i * 8u, 6) == 0 || p[0] == 160)
            continue;
        const auto a0 = std::uint16_t(u16(p)), a1 = std::uint16_t(u16(p+2));
        int x = a1 & 511;
        while (x - relative > 256) x -= 512;
        while (relative - x > 256) x += 512;
        if (x - relative < -128 || x - relative > 128) continue;
        if (x < -32768 || x > 32767) continue;
        publications[publication_count++] = {entity, world, x,
            a0, a1, std::uint16_t(u16(p+4)),
            std::uint16_t(u16(ram + entity - 0x03000000u + 0x18u))};
    }
}

int entity_view_object_x(int index, std::uint16_t a0, std::uint16_t a1,
                         std::uint16_t a2, int* x) {
    const auto* bus = gbarecomp::active_bus();
    validate_context(bus);
    if (!x || index < 3 || !field(bus)) return 0;
    const auto* ram = bus->iwram_ptr();
    bool found = false;
    int authored = 0;
    // OAM DMA and the guest's next update can straddle the presentation frame.
    // Retain both publications; attributes establish which one is displayed.
    for (unsigned i = 0; i < publication_count + previous_count; ++i) {
        const auto& p = i < publication_count ? publications[i] :
            previous_publications[i - publication_count];
        if (p.a0 != a0 || p.a1 != a1 || p.a2 != a2 ||
            !sprite_address(bus, p.entity)) continue;
        const auto* sprite = ram + p.entity - 0x03000000u;
        const auto delta = std::int64_t(static_cast<std::int32_t>(u32(sprite))) -
                           static_cast<std::int32_t>(p.world_x);
        if (!sprite[0x16] || u16(sprite + 0x18u) != p.id ||
            delta < -128 * 256 || delta > 128 * 256) continue;
        if (found && authored != p.x) return 0; // ambiguous publication
        found = true;
        authored = p.x;
    }
    if (found) *x = authored;
    return found ? 1 : 0;
}
}
