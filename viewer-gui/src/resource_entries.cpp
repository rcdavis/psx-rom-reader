#include "resource_entries.h"

namespace
{

int collect_entry(const psxrom_entry* entry, void* user)
{
    auto& entries = *static_cast<std::vector<std::string>*>(user);
    entries.emplace_back(entry->path);
    if (entry->is_dir)
        entries.back() += '/';
    return 0;
}

}

psxrom_result load_resource_entries(psxrom_disc* disc, std::vector<std::string>& entries)
{
    entries.clear();
    const psxrom_result result = psxrom_walk(disc, collect_entry, &entries);
    if (result != PSXROM_OK)
        entries.clear();
    return result;
}
