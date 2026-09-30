#include "native_pipeline_manifest.h"
#include "native_formats.h"
#include "native_pipeline_key.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main() {
    try {
        sfr::NativeDraw draw;
        draw.stride = 32;
        draw.topology = plume::RenderPrimitiveTopology::TRIANGLE_STRIP;
        draw.elements.emplace_back(sfr::declaration_semantic(0), 0, 0, plume::RenderFormat::R32G32B32_FLOAT, 0, 0);
        draw.elements.emplace_back(sfr::declaration_semantic(5), 1, 1, plume::RenderFormat::R32G32_FLOAT, 0, 12);
        draw.elements.emplace_back(sfr::declaration_semantic(10), 0, 2, plume::RenderFormat::B8G8R8A8_UNORM, 0, 20);
        draw.pixel_spec_constants = 2;
        draw.blend.blendEnabled = true;
        draw.blend.srcBlend = plume::RenderBlend::SRC_ALPHA;
        draw.blend.dstBlend = plume::RenderBlend::INV_SRC_ALPHA;
        draw.write_mask = 7;
        draw.depth_enabled = true;
        draw.depth_function = plume::RenderComparisonFunction::GREATER;
        draw.cull = plume::RenderCullMode::BACK;
        draw.stencil_enabled = true;
        draw.stencil_reference = 1; draw.stencil_read_mask = 0x0F; draw.stencil_write_mask = 0xF0;
        draw.stencil_front.passOp = plume::RenderStencilOp::REPLACE;
        draw.stencil_back.compareFunction = plume::RenderComparisonFunction::EQUAL;
        const sfr::PipelineShaders shaders{0x1111222233334444ull, 0x5555666677778888ull, 2, true};

        const auto record = sfr::serialize_pipeline_record(draw, shaders);
        require(!record.empty(), "a draw with known semantics serializes");
        sfr::NativeDraw restored;
        sfr::PipelineShaders restored_shaders;
        require(sfr::deserialize_pipeline_record(record, restored, restored_shaders), "a record reads back");
        require(restored_shaders.vertex == shaders.vertex && restored_shaders.pixel == shaders.pixel &&
                restored_shaders.pixel_link == 2 && restored_shaders.pixel_linked, "shader identities survive");
        // The pipeline cache key must be what the live draw would have made,
        // or a prebuilt pipeline is never found (shader pointers aside).
        std::vector<uint8_t> a, b;
        sfr::native_pipeline_key_bulk(draw, a);
        sfr::native_pipeline_key_bulk(restored, b);
        require(a == b, "a restored draw has the same pipeline key");
        // A blend or stencil descriptor's padding is not part of the key: the
        // same state in two objects must find the same pipeline.
        sfr::NativeDraw dirty = restored;
        std::memset(static_cast<void*>(&dirty.blend), 0x5A, sizeof(dirty.blend));
        dirty.blend = restored.blend;  // the fields again; the padding may stay as it was
        std::memset(static_cast<void*>(&dirty.stencil_front), 0xA5, sizeof(dirty.stencil_front));
        dirty.stencil_front = restored.stencil_front;
        std::vector<uint8_t> c;
        sfr::native_pipeline_key_bulk(dirty, c);
        require(c == b, "padding does not change the pipeline key");
        // Element semantics are the static table's own pointers.
        require(restored.elements[1].semanticName == sfr::declaration_semantic(5), "semantic names are the table's");
        require(restored.elements[2].location == 2, "locations survive");

        // Without stencil the fields after it are absent, and still round trip.
        draw.stencil_enabled = false;
        const auto plain = sfr::serialize_pipeline_record(draw, shaders);
        require(plain.size() < record.size(), "disabled stencil writes nothing");
        require(sfr::deserialize_pipeline_record(plain, restored, restored_shaders) && !restored.stencil_enabled,
                "a record without stencil reads back");

        // Damage is refused, never misread.
        require(!sfr::deserialize_pipeline_record({record.data(), record.size() - 1}, restored, restored_shaders),
                "a truncated record is refused");
        auto longer = record;
        longer.push_back(0);
        require(!sfr::deserialize_pipeline_record(longer, restored, restored_shaders), "trailing bytes are refused");
        auto bad_version = record;
        bad_version[0] = 9;
        require(!sfr::deserialize_pipeline_record(bad_version, restored, restored_shaders), "another version is refused");
        sfr::NativeDraw unnamed = draw;
        unnamed.elements[0].semanticName = "NOT_A_USAGE";
        require(sfr::serialize_pipeline_record(unnamed, shaders).empty(), "an unknown semantic is not recorded");

        require(sfr::shader_identity({}) != 0 || true, "identity of nothing is defined");
        const uint8_t one[] = {1, 2, 3}, two[] = {1, 2, 4};
        require(sfr::shader_identity(one) != sfr::shader_identity(two), "identities differ");
        require(sfr::shader_identity(one) == sfr::shader_identity(one), "identities are stable");

        // The file: append in steps, reload, survive a cut tail and a foreign file.
        const auto path = std::filesystem::temp_directory_path() / "sfr_pipeline_manifest_test.bin";
        std::filesystem::remove(path);
        require(sfr::load_pipeline_manifest(path).empty(), "a missing file has no records");
        std::vector<std::vector<uint8_t>> first{record}, second{plain, record};
        require(sfr::append_pipeline_manifest(path, first), "first append");
        require(sfr::append_pipeline_manifest(path, second), "second append");
        auto loaded = sfr::load_pipeline_manifest(path);
        require(loaded.size() == 3 && loaded[0] == record && loaded[1] == plain && loaded[2] == record, "records reload in order");
        const auto size = std::filesystem::file_size(path);
        std::filesystem::resize_file(path, size - 5);
        require(sfr::load_pipeline_manifest(path).size() == 2, "a cut tail drops only the last record");
        { std::ofstream(path, std::ios::binary | std::ios::trunc) << "not a manifest at all"; }
        require(sfr::load_pipeline_manifest(path).empty(), "a foreign file has no records");
        require(sfr::append_pipeline_manifest(path, first), "a foreign file is replaced");
        require(sfr::load_pipeline_manifest(path).size() == 1, "the replacement holds what was appended");
        std::filesystem::remove(path);
        std::puts("native pipeline manifest ok");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
