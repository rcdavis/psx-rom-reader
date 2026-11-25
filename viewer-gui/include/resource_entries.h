#pragma once

#include <psxrom/psxrom.h>

#include <string>
#include <vector>

struct ResourceEntry
{
    std::string name;
    bool is_directory;
    std::vector<ResourceEntry> children;
};

psxrom_result load_resource_entries(psxrom_disc* disc, std::vector<ResourceEntry>& entries);
