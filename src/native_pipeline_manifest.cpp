#include "native_pipeline_manifest.h"
#include "native_formats.h"
#include <cstring>
#include <fstream>
#include <string_view>

namespace sfr {
namespace {
constexpr char magic[8] = {'S', 'F', 'R', 'P', 'L', 'M', '0', '1'};
constexpr uint32_t record_version = 1;
constexpr uint32_t maximum_record_bytes = 64 * 1024;
constexpr uint32_t maximum_elements = 64;
constexpr uint8_t unknown_semantic = 0xFF;

struct Writer {
    std::vector<uint8_t>& out;
    void u8(uint8_t v) { out.push_back(v); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i))); }
    void u64(uint64_t v) { for (int i = 0; i < 8; ++i) out.push_back(uint8_t(v >> (8 * i))); }
    template <class E> void e(E v) { u32(uint32_t(v)); }
};
struct Reader {
    std::span<const uint8_t> in;
    size_t at = 0;
    bool ok = true;
    uint64_t take(int bytes) {
        if (!ok || in.size() - at < size_t(bytes)) { ok = false; return 0; }
        uint64_t v = 0;
        for (int i = 0; i < bytes; ++i) v |= uint64_t(in[at + i]) << (8 * i);
        at += size_t(bytes);
        return v;
    }
    uint8_t u8() { return uint8_t(take(1)); }
    uint32_t u32() { return uint32_t(take(4)); }
    uint64_t u64() { return take(8); }
    template <class E> E e() { return E(u32()); }
};

uint8_t usage_of(const char* name) {
    if (!name) return unknown_semantic;
    for (uint32_t usage = 0; usage <= 13; ++usage) {
        const char* known = declaration_semantic(usage);
        if (known && std::string_view(known) == name) return uint8_t(usage);
    }
    return unknown_semantic;
}

void put_face(Writer& w, const plume::RenderStencilFaceDesc& f) {
    w.e(f.passOp); w.e(f.failOp); w.e(f.depthFailOp); w.e(f.compareFunction);
}
void get_face(Reader& r, plume::RenderStencilFaceDesc& f) {
    f.passOp = r.e<plume::RenderStencilOp>(); f.failOp = r.e<plume::RenderStencilOp>();
    f.depthFailOp = r.e<plume::RenderStencilOp>(); f.compareFunction = r.e<plume::RenderComparisonFunction>();
}
}

uint64_t shader_identity(std::span<const uint8_t> container) noexcept {
    uint64_t hash = 0xcbf29ce484222325ull;
    for (uint8_t byte : container) hash = (hash ^ byte) * 0x100000001b3ull;
    return hash ^ (uint64_t(container.size()) << 40);
}

std::vector<uint8_t> serialize_pipeline_record(const NativeDraw& draw, const PipelineShaders& shaders) {
    std::vector<uint8_t> out;
    if (draw.elements.size() > maximum_elements) return out;
    Writer w{out};
    w.u32(record_version);
    w.u64(shaders.vertex); w.u64(shaders.pixel); w.u32(shaders.pixel_link); w.u8(shaders.pixel_linked);
    w.u32(draw.pixel_spec_constants);
    w.u32(draw.stride); w.e(draw.topology);
    w.u32(uint32_t(draw.elements.size()));
    for (const auto& element : draw.elements) {
        const uint8_t usage = usage_of(element.semanticName);
        if (usage == unknown_semantic) return {};
        w.u8(usage); w.u32(element.semanticIndex); w.u32(element.location); w.e(element.format);
        w.u32(element.slotIndex); w.u32(element.alignedByteOffset);
    }
    const auto& b = draw.blend;
    w.e(b.srcBlend); w.e(b.dstBlend); w.e(b.blendOp); w.e(b.srcBlendAlpha); w.e(b.dstBlendAlpha);
    w.e(b.blendOpAlpha); w.u8(b.blendEnabled);
    w.u8(draw.write_mask); w.u8(draw.depth_enabled); w.u8(draw.depth_write); w.e(draw.depth_function);
    w.e(draw.cull);
    w.u8(draw.stencil_enabled);
    if (draw.stencil_enabled) {
        w.u8(draw.stencil_reference); w.u8(draw.stencil_read_mask); w.u8(draw.stencil_write_mask);
        put_face(w, draw.stencil_front); put_face(w, draw.stencil_back);
    }
    return out;
}

bool deserialize_pipeline_record(std::span<const uint8_t> record, NativeDraw& draw, PipelineShaders& shaders) {
    Reader r{record};
    if (r.u32() != record_version) return false;
    shaders.vertex = r.u64(); shaders.pixel = r.u64(); shaders.pixel_link = r.u32(); shaders.pixel_linked = r.u8() != 0;
    draw.pixel_spec_constants = r.u32();
    draw.stride = r.u32(); draw.topology = r.e<plume::RenderPrimitiveTopology>();
    const uint32_t count = r.u32();
    if (!r.ok || count > maximum_elements) return false;
    draw.elements.clear();
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t usage = r.u8();
        const char* name = usage <= 13 ? declaration_semantic(usage) : nullptr;
        const uint32_t index = r.u32(), location = r.u32();
        const auto format = r.e<plume::RenderFormat>();
        const uint32_t slot = r.u32(), offset = r.u32();
        if (!r.ok || !name) return false;
        draw.elements.emplace_back(name, index, location, format, slot, offset);
    }
    auto& b = draw.blend;
    b.srcBlend = r.e<plume::RenderBlend>(); b.dstBlend = r.e<plume::RenderBlend>();
    b.blendOp = r.e<plume::RenderBlendOperation>(); b.srcBlendAlpha = r.e<plume::RenderBlend>();
    b.dstBlendAlpha = r.e<plume::RenderBlend>(); b.blendOpAlpha = r.e<plume::RenderBlendOperation>();
    b.blendEnabled = r.u8() != 0;
    draw.write_mask = r.u8(); draw.depth_enabled = r.u8() != 0; draw.depth_write = r.u8() != 0;
    draw.depth_function = r.e<plume::RenderComparisonFunction>();
    draw.cull = r.e<plume::RenderCullMode>();
    draw.stencil_enabled = r.u8() != 0;
    if (draw.stencil_enabled) {
        draw.stencil_reference = r.u8(); draw.stencil_read_mask = r.u8(); draw.stencil_write_mask = r.u8();
        get_face(r, draw.stencil_front); get_face(r, draw.stencil_back);
    }
    return r.ok && r.at == record.size();
}

std::vector<std::vector<uint8_t>> load_pipeline_manifest(const std::filesystem::path& path) {
    std::vector<std::vector<uint8_t>> records;
    std::ifstream file(path, std::ios::binary);
    char header[sizeof(magic)];
    if (!file.read(header, sizeof(header)) || std::memcmp(header, magic, sizeof(magic)) != 0) return records;
    for (;;) {
        uint8_t length_bytes[4];
        if (!file.read(reinterpret_cast<char*>(length_bytes), 4)) break;
        const uint32_t length = uint32_t(length_bytes[0]) | uint32_t(length_bytes[1]) << 8 |
                                uint32_t(length_bytes[2]) << 16 | uint32_t(length_bytes[3]) << 24;
        if (!length || length > maximum_record_bytes) break;
        std::vector<uint8_t> record(length);
        if (!file.read(reinterpret_cast<char*>(record.data()), length)) break;
        records.push_back(std::move(record));
    }
    return records;
}

bool append_pipeline_manifest(const std::filesystem::path& path, std::span<const std::vector<uint8_t>> records) {
    std::error_code ignored;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ignored);
    bool fresh = true;
    {
        std::ifstream existing(path, std::ios::binary);
        char header[sizeof(magic)];
        fresh = !existing.read(header, sizeof(header)) || std::memcmp(header, magic, sizeof(magic)) != 0;
    }
    std::ofstream file(path, std::ios::binary | (fresh ? std::ios::trunc : std::ios::app));
    if (!file) return false;
    if (fresh) file.write(magic, sizeof(magic));
    for (const auto& record : records) {
        const uint32_t length = uint32_t(record.size());
        const uint8_t length_bytes[4] = {uint8_t(length), uint8_t(length >> 8), uint8_t(length >> 16), uint8_t(length >> 24)};
        file.write(reinterpret_cast<const char*>(length_bytes), 4);
        file.write(reinterpret_cast<const char*>(record.data()), std::streamsize(record.size()));
    }
    return bool(file);
}
}
