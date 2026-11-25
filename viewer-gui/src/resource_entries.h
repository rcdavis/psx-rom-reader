#pragma once

#include <psxrom/psxrom.h>

#include <string>
#include <vector>

psxrom_result load_resource_entries(psxrom_disc* disc, std::vector<std::string>& entries);
