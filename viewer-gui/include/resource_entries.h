#pragma once

#include <psxrom/psxrom.h>

#include <string>
#include <vector>

struct ResourceEntry
{
    std::string name;
    std::string path;
    uint32_t size;
    uint32_t lba;
    bool is_directory;
    std::vector<ResourceEntry> children;
};

psxrom_result load_resource_entries(psxrom_disc* disc, std::vector<ResourceEntry>& entries);

enum class ResourceDetailType
{
    None,
    Tim,
    Tmd,
    Mmd,
    Map,
    Error
};

struct ResourceDetails
{
    ResourceDetailType type = ResourceDetailType::None;
    psxrom_result error = PSXROM_OK;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bpp = 0;
    uint32_t img_x = 0;
    uint32_t img_y = 0;
    bool has_clut = false;
    uint32_t clut_x = 0;
    uint32_t clut_y = 0;
    uint32_t clut_w = 0;
    uint32_t num_cluts = 0;
    size_t object_count = 0;
    size_t triangle_count = 0;
    uint32_t map_tiles_w = 0;
    uint32_t map_tiles_h = 0;
    size_t map_used_tiles = 0;
    int32_t map_camera_origin[3] = {};
    int32_t map_camera_target[3] = {};
    uint32_t map_view_distance = 0;
    size_t map_image_count = 0;
    uint16_t map_object_count = 0;
    uint16_t map_instance_count = 0;
    uint16_t map_digimon_count = 0;
};

ResourceDetails load_resource_details(psxrom_disc* disc, const ResourceEntry& entry);
