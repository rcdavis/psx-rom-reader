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
    const size_t dot = entry.name.find_last_of('.');
    if (entry.is_directory || dot == std::string::npos || entry.name.size() - dot != 4 ||
        std::tolower(static_cast<unsigned char>(entry.name[dot + 1])) != 't' ||
        std::tolower(static_cast<unsigned char>(entry.name[dot + 2])) != 'i' ||
        std::tolower(static_cast<unsigned char>(entry.name[dot + 3])) != 'm')
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
