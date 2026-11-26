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
    if (!is_tim && !is_tmd && !is_mmd && !is_map)
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
