#include "resource_entries.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>

namespace
{

struct CollectContext
{
    std::vector<std::vector<ResourceEntry>*> levels;
    bool invalid = false;
};

int collect_entry(const psxrom_entry* entry, void* user)
{
    auto& context = *static_cast<CollectContext*>(user);
    auto& levels = context.levels;
    const size_t depth = std::count(entry->path, entry->path + std::strlen(entry->path), '/') - 1;
    if (std::strchr(entry->name, '/') != nullptr || depth >= levels.size())
    {
        context.invalid = true;
        return 1;
    }
    levels.resize(depth + 1);
    auto& siblings = *levels.back();
    siblings.push_back({entry->name, entry->path, entry->size, entry->lba, entry->is_dir != 0, {}});
    if (entry->is_dir)
        levels.push_back(&siblings.back().children);
    return 0;
}

bool has_extension(const std::string& name, const char* extension)
{
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || name.size() - dot != std::strlen(extension))
        return false;
    for (size_t i = 0; extension[i] != '\0'; ++i)
        if (std::tolower(static_cast<unsigned char>(name[dot + i])) != extension[i])
            return false;
    return true;
}

bool read_u16(const uint8_t* data, size_t size, size_t offset, uint16_t& value)
{
    if (offset > size || size - offset < 2)
        return false;
    value = static_cast<uint16_t>(data[offset] | (static_cast<uint16_t>(data[offset + 1]) << 8));
    return true;
}

bool read_u32(const uint8_t* data, size_t size, size_t offset, uint32_t& value)
{
    if (offset > size || size - offset < 4)
        return false;
    value = static_cast<uint32_t>(data[offset]) |
            (static_cast<uint32_t>(data[offset + 1]) << 8) |
            (static_cast<uint32_t>(data[offset + 2]) << 16) |
            (static_cast<uint32_t>(data[offset + 3]) << 24);
    return true;
}

struct VhbChunkDetails
{
    size_t size = 0;
    uint16_t program_count = 0;
    uint16_t tone_count = 0;
    uint16_t sample_count = 0;
    size_t body_size = 0;
    size_t sequence_count = 0;
    bool has_sequences = false;
};

bool parse_vhb_chunk(const uint8_t* data, size_t available, size_t header_words, VhbChunkDetails& chunk)
{
    uint32_t header_size = 0;
    if (!read_u32(data, available, 0, header_size))
        return false;
    if (header_words == 0)
    {
        if (header_size != 12 && header_size != 16)
            return false;
        header_words = header_size / 4;
    }
    if (header_size != header_words * 4 || available < header_size)
        return false;

    const uint32_t vh_offset = header_size;
    uint32_t vb_offset = 0;
    uint32_t sequence_offset = 0;
    uint32_t end_offset = 0;
    if (!read_u32(data, available, 4, vb_offset))
        return false;
    if (header_words == 4)
    {
        if (!read_u32(data, available, 8, sequence_offset) ||
            !read_u32(data, available, 12, end_offset))
            return false;
    }
    else if (!read_u32(data, available, 8, end_offset))
        return false;

    if (vh_offset != header_size || vb_offset < vh_offset ||
        (header_words == 4 && (sequence_offset < vb_offset || end_offset < sequence_offset)) ||
        (header_words == 3 && end_offset < vb_offset) || end_offset > available)
        return false;
    if (static_cast<size_t>(vb_offset) - vh_offset < 32 ||
        std::memcmp(data + vh_offset, "pBAV", 4) != 0)
        return false;

    uint16_t programs = 0;
    uint16_t tones = 0;
    uint16_t samples = 0;
    if (!read_u16(data, available, vh_offset + 18, programs) ||
        !read_u16(data, available, vh_offset + 20, tones) ||
        !read_u16(data, available, vh_offset + 22, samples) || programs > 128)
        return false;
    const size_t vh_size = 32 + 128 * 16 + static_cast<size_t>(programs) * 16 * 32 + 512;
    if (static_cast<size_t>(vb_offset) - vh_offset != vh_size)
        return false;

    chunk.size = end_offset;
    chunk.program_count = programs;
    chunk.tone_count = tones;
    chunk.sample_count = samples;
    chunk.body_size = (header_words == 4 ? sequence_offset : end_offset) - vb_offset;
    chunk.has_sequences = header_words == 4 && sequence_offset < end_offset;
    if (chunk.has_sequences)
    {
        const size_t sequence_size = end_offset - sequence_offset;
        uint32_t table_size = 0;
        if (!read_u32(data + sequence_offset, sequence_size, 0, table_size) ||
            table_size < 4 || table_size % 4 != 0 || table_size > sequence_size)
            return false;
        chunk.sequence_count = table_size / 4;
        for (size_t i = 0; i < chunk.sequence_count; ++i)
        {
            uint32_t offset = 0;
            if (!read_u32(data + sequence_offset, sequence_size, i * 4, offset) ||
                offset < table_size || offset > sequence_size)
                return false;
        }
    }
    return true;
}

bool parse_vhb(const uint8_t* data, size_t size, const std::string& name, ResourceDetails& details)
{
    std::string upper_name = name;
    std::transform(upper_name.begin(), upper_name.end(), upper_name.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });

    size_t header_words = 0;
    size_t stride = 0;
    bool sector_chunks = false;
    if (upper_name == "FAALL.VHB")
    {
        header_words = 4;
        stride = 39 * 2048;
    }
    else if (upper_name == "VBALL.VHB")
        stride = 7 * 2048;
    else if (upper_name == "VLALL.VHB")
        stride = 15 * 2048;
    else if (upper_name == "ESALL.VHB")
        sector_chunks = true;

    details.vhb_indexed = size >= 4 && std::memcmp(data, "SIDX", 4) == 0;
    VhbChunkDetails first_chunk;
    size_t chunk_count = 0;
    if (details.vhb_indexed)
    {
        uint16_t count = 0;
        uint16_t index_sectors = 0;
        if (!read_u16(data, size, 4, count) || !read_u16(data, size, 6, index_sectors) || count == 0)
            return false;
        const size_t index_bytes = 8 + static_cast<size_t>(count) * 4;
        if (index_sectors == 0 || index_bytes > static_cast<size_t>(index_sectors) * 2048 ||
            index_bytes > size)
            return false;
        size_t previous_end = static_cast<size_t>(index_sectors) * 2048;
        for (size_t i = 0; i < count; ++i)
        {
            uint16_t sector = 0;
            uint16_t sectors = 0;
            if (!read_u16(data, size, 8 + i * 4, sector) ||
                !read_u16(data, size, 10 + i * 4, sectors) || sectors == 0)
                return false;
            const size_t offset = static_cast<size_t>(sector) * 2048;
            const size_t available = static_cast<size_t>(sectors) * 2048;
            if (offset < previous_end || offset > size || available > size - offset)
                return false;
            VhbChunkDetails chunk;
            if (!parse_vhb_chunk(data + offset, available, header_words, chunk) || chunk.size > available)
                return false;
            if (i == 0)
                first_chunk = chunk;
            previous_end = offset + available;
            ++chunk_count;
        }
    }
    else if (stride != 0)
    {
        if (size == 0 || size % stride != 0)
            return false;
        for (size_t offset = 0; offset < size; offset += stride)
        {
            VhbChunkDetails chunk;
            if (!parse_vhb_chunk(data + offset, stride, header_words, chunk) || chunk.size > stride)
                return false;
            if (chunk_count++ == 0)
                first_chunk = chunk;
        }
    }
    else if (sector_chunks)
    {
        size_t offset = 0;
        while (offset < size)
        {
            VhbChunkDetails chunk;
            if (!parse_vhb_chunk(data + offset, size - offset, header_words, chunk) ||
                chunk.size == 0 || chunk.size > size - offset)
                return false;
            if (chunk_count++ == 0)
                first_chunk = chunk;
            const size_t padded_size = (chunk.size + 2047) / 2048 * 2048;
            if (padded_size > size - offset)
                return false;
            offset += padded_size;
        }
    }
    else
    {
        VhbChunkDetails chunk;
        if (!parse_vhb_chunk(data, size, header_words, chunk) || chunk.size > size)
            return false;
        first_chunk = chunk;
        chunk_count = 1;
    }

    if (chunk_count == 0)
        return false;
    details.type = ResourceDetailType::Vhb;
    details.vhb_chunk_count = chunk_count;
    details.vhb_program_count = first_chunk.program_count;
    details.vhb_tone_count = first_chunk.tone_count;
    details.vhb_sample_count = first_chunk.sample_count;
    details.vhb_body_size = first_chunk.body_size;
    details.vhb_sequence_count = first_chunk.sequence_count;
    details.vhb_has_sequences = first_chunk.has_sequences;
    return true;
}

}

psxrom_result load_resource_entries(psxrom_disc* disc, std::vector<ResourceEntry>& entries)
{
    entries.clear();
    CollectContext context{{&entries}};
    psxrom_result result = psxrom_walk(disc, collect_entry, &context);
    if (result == PSXROM_OK && context.invalid)
        result = PSXROM_ERR_FORMAT;
    if (result != PSXROM_OK)
        entries.clear();
    return result;
}

ResourceDetails load_resource_details(psxrom_disc* disc, const ResourceEntry& entry)
{
    ResourceDetails details;
    if (entry.is_directory)
        return details;
    const bool is_tim = has_extension(entry.name, ".tim");
    const bool is_tmd = has_extension(entry.name, ".tmd");
    const bool is_mmd = has_extension(entry.name, ".mmd");
    const bool is_map = has_extension(entry.name, ".map");
    const bool is_tfs = has_extension(entry.name, ".tfs");
    const bool is_vhb = has_extension(entry.name, ".vhb");
    if (!is_tim && !is_tmd && !is_mmd && !is_map && !is_tfs && !is_vhb)
        return details;

    details.type = ResourceDetailType::Error;
    if (entry.size > std::numeric_limits<uint32_t>::max() - 2047)
    {
        details.error = PSXROM_ERR_RANGE;
        return details;
    }

    psxrom_entry file{};
    file.lba = entry.lba;
    file.size = entry.size;
    uint8_t* data = nullptr;
    size_t size = 0;
    details.error = psxrom_read_file(disc, &file, &data, &size);
    if (details.error != PSXROM_OK)
        return details;

    const std::unique_ptr<uint8_t, decltype(&std::free)> buffer(data, &std::free);
    if (is_vhb)
    {
        if (!parse_vhb(buffer.get(), size, entry.name, details))
            details.error = PSXROM_ERR_FORMAT;
        return details;
    }

    if (is_tfs)
    {
        psxrom_tfs tfs{};
        details.error = psxrom_tfs_parse(buffer.get(), size, &tfs);
        if (details.error != PSXROM_OK)
            return details;

        details.type = ResourceDetailType::Tfs;
        details.tfs_width = tfs.width;
        details.tfs_height = tfs.height;
        details.tfs_tile_count = tfs.num_tiles;
        details.tfs_palette_count = tfs.num_palettes;
        psxrom_tfs_free(&tfs);
        return details;
    }

    if (is_map)
    {
        psxrom_map map{};
        details.error = psxrom_map_parse(buffer.get(), size, &map);
        if (details.error != PSXROM_OK)
            return details;

        details.type = ResourceDetailType::Map;
        details.map_tiles_w = map.tiles_w;
        details.map_tiles_h = map.tiles_h;
        for (size_t i = 0; i < static_cast<size_t>(map.tiles_w) * map.tiles_h; ++i)
            details.map_used_tiles += map.tiles[i] != UINT32_MAX;
        std::copy_n(map.camera_origin, 3, details.map_camera_origin);
        std::copy_n(map.camera_target, 3, details.map_camera_target);
        details.map_view_distance = map.view_distance;
        details.map_image_count = map.num_images;
        details.map_object_count = map.num_objects;
        details.map_instance_count = map.num_instances;
        details.map_digimon_count = map.num_digimon;
        psxrom_map_free(&map);
        return details;
    }

    if (is_tmd || is_mmd)
    {
        psxrom_tmd tmd{};
        details.error = is_mmd
            ? psxrom_mmd_parse(buffer.get(), size, nullptr, 0, &tmd)
            : psxrom_tmd_parse(buffer.get(), size, &tmd);
        if (details.error != PSXROM_OK)
            return details;

        details.type = is_mmd ? ResourceDetailType::Mmd : ResourceDetailType::Tmd;
        details.object_count = tmd.num_objects;
        for (size_t i = 0; i < tmd.num_objects; ++i)
            details.triangle_count += tmd.objects[i].num_tris;
        psxrom_tmd_free(&tmd);
        return details;
    }

    psxrom_tim tim{};
    details.error = psxrom_tim_parse(buffer.get(), size, &tim);
    if (details.error != PSXROM_OK)
        return details;

    details.type = ResourceDetailType::Tim;
    details.width = tim.width;
    details.height = tim.height;
    details.bpp = tim.bpp;
    details.img_x = tim.img_x;
    details.img_y = tim.img_y;
    details.has_clut = tim.has_clut != 0;
    details.clut_x = tim.clut_x;
    details.clut_y = tim.clut_y;
    details.clut_w = tim.clut_w;
    details.num_cluts = tim.num_cluts;
    return details;
}
