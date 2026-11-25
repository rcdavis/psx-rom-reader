#include "resource_entries.h"

#include <algorithm>
#include <cstring>

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
    siblings.push_back({entry->name, entry->is_dir != 0, {}});
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
