/*
 * Copyright (c) 2018 Particle Industries, Inc.  All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "flash_mal.h"
#include "ota_module.h"

namespace {

const module_info_t* get_module_info(const module_bounds_t* bounds, uint32_t* offset = nullptr) {
    // Note: This function uses XIP to access the module info in the OTA flash section
    return FLASH_ModuleInfo(FLASH_INTERNAL, bounds->start_address, offset);
}

} // namespace

// NB: Modules in external flash are made to appears as if they are located in Internal flash by means of
// XiP - the external flash is mapped to a region of addressable memory, and can be access transparently via
// a pointer to the memory region. That is why the OTA module can be accessed via FLASH_INTERNAL.

/**
 * Determines if a given address is in range.
 * @param test      The address to test
 * @param start     The start range (inclusive)
 * @param end       The end range (inclusive)
 * @return {@code true} if the address is within range.
 */
inline bool in_range(uint32_t test, uint32_t start, uint32_t end)
{
    return test>=start && test<=end;
}


/**
 * Find the module_info at a given address. No validation is done so the data
 * pointed to should not be trusted.
 * @param bounds
 * @return
 */
const module_info_t* locate_module(const module_bounds_t* bounds) {
    return get_module_info(bounds);
}

/**
 * Fetches and validates the module info found at a given location.
 * @param target        Receives the module into
 * @param bounds        The location where to retrieve the module from.
 * @param userDepsOptional
 * @return {@code true} if the module info can be read via the info, crc, suffix pointers.
 */
// FIXME: This function accesses the module info via XIP and may fail to parse it correctly under
// some not entirely clear circumstances. Disabling compiler optimizations helps to work around
// the problem
__attribute__((optimize("O0")))
bool fetch_module(hal_module_t* target, const module_bounds_t* bounds, bool userDepsOptional, uint16_t check_flags)
{
    memset(target, 0, sizeof(*target));

    target->bounds = *bounds;
    target->info = get_module_info(bounds, &target->module_info_offset);
    if (target->info)
    {
        target->validity_checked = MODULE_VALIDATION_RANGE | MODULE_VALIDATION_DEPENDENCIES | MODULE_VALIDATION_PLATFORM | check_flags;
        target->validity_result = 0;
        const uint8_t* module_end = (const uint8_t*)target->info->module_end_address;
        // find the location of where the module should be flashed to based on its module co-ordinates (function/index/mcu)
        const module_bounds_t* expected_bounds = find_module_bounds(module_function(target->info), module_index(target->info), module_mcu_target(target->info));
        if (expected_bounds && in_range(uint32_t(module_end), expected_bounds->start_address, expected_bounds->end_address)) {
            target->validity_result |= MODULE_VALIDATION_RANGE;
            target->validity_result |= (PLATFORM_ID==module_platform_id(target->info)) ? MODULE_VALIDATION_PLATFORM : 0;
            // the suffix ends at module_end, and the crc starts after module end
            target->crc = (module_info_crc_t*)module_end;
            target->suffix = (module_info_suffix_t*)(module_end-sizeof(module_info_suffix_t));
            if (validate_module_dependencies(bounds, userDepsOptional, target->validity_checked & MODULE_VALIDATION_DEPENDENCIES_FULL))
                target->validity_result |= MODULE_VALIDATION_DEPENDENCIES | (target->validity_checked & MODULE_VALIDATION_DEPENDENCIES_FULL);
            if ((target->validity_checked & MODULE_VALIDATION_INTEGRITY) && FLASH_VerifyCRC32(FLASH_INTERNAL, bounds->start_address, module_length(target->info)))
                target->validity_result |= MODULE_VALIDATION_INTEGRITY;
        }
        else
            target->info = NULL;
    }
    return target->info!=NULL;
}

