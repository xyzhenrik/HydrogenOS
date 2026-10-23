#pragma once

#include <stddef.h>

int elf64_validate(const void *image, size_t size);
void elf64_dump_info(const void *image);
int elf64_load_image(const void *image, size_t size);
