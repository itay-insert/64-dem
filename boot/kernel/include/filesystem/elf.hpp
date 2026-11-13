#pragma once

#include "elf_internal.h"
#include "filesystem/fs.hpp"

u64 elf_calculate_size(u64 file_handle, BootFileSystem *fat32);
void load_elf_64(u64 file_handle, BootFileSystem *fat32, void *buff);
