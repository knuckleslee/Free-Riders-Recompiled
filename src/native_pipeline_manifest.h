#pragma once
#include "native_renderer.h"
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace sfr {
// The pipelines a run needed, written down so the next one can build them in
// the background while the title loads instead of at the first draw that
// meets each (a pipeline built there stalls the frame: docs/roadmap.md).
//
// A record is a pipeline's whole description in terms that survive a restart:
// shaders by identity (a hash of the original container, see
// shader_identity), vertex semantics by usage number, enums by value. The
// draw's per-draw data (constants, vertices) is not part of it.
struct PipelineShaders {
    uint64_t vertex = 0, pixel = 0;
    // D3D12 links a pixel shader that has specialization constants; pixel_link
    // is the value (NativeRenderer::specialized) when pixel_linked.
    uint32_t pixel_link = 0;
    bool pixel_linked = false;
};

uint64_t shader_identity(std::span<const uint8_t> container) noexcept;

// An empty result: the draw uses something a record cannot hold.
std::vector<uint8_t> serialize_pipeline_record(const NativeDraw& draw, const PipelineShaders& shaders);
// Fills draw's pipeline state (not its shaders, which the caller resolves from
// shaders). False for a malformed record.
bool deserialize_pipeline_record(std::span<const uint8_t> record, NativeDraw& draw, PipelineShaders& shaders);

// Records of the file at path; a missing, foreign or truncated file yields the
// complete records before the damage (possibly none).
std::vector<std::vector<uint8_t>> load_pipeline_manifest(const std::filesystem::path& path);
// Adds records to the file, starting a new one when it is missing or foreign.
bool append_pipeline_manifest(const std::filesystem::path& path, std::span<const std::vector<uint8_t>> records);
}
