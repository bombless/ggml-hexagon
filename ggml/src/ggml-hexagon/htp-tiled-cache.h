#pragma once

#include <cstdint>
#include <cstddef>

// On-disk sidecar format shared by the offline converter and FastRPC loader.
// All offsets are absolute byte offsets from the beginning of the .htpbin file.
namespace htp_tiled_cache {
static constexpr char MAGIC[8] = { 'H', 'T', 'P', 'B', 'I', 'N', '0', '1' };
static constexpr uint32_t VERSION = 1;
static constexpr size_t NAME_CAPACITY = 256;

#pragma pack(push, 1)
struct Header {
    char magic[8];
    uint32_t version;
    uint32_t entry_count;
    uint64_t source_size;
    uint64_t source_signature;
    uint64_t entries_offset;
    uint64_t data_offset;
};

struct Entry {
    char name[NAME_CAPACITY];
    uint32_t type;
    uint64_t ne[4];
    uint64_t offset;
    uint64_t size;
};
#pragma pack(pop)
}