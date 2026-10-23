#pragma once
#include <stdint.h>

typedef struct __attribute__((packed)) {
    char signature[4];
    uint16_t version;
    uint16_t oem_str_off;
    uint16_t oem_str_seg;
    uint32_t capabilities;
    uint16_t mode_list_off;
    uint16_t mode_list_seg;
    uint16_t total_memory; // in 64KB blocks
} vbe_info_t;

typedef struct __attribute__((packed)) {
    uint16_t attributes;
    uint8_t window_a, window_b;
    uint16_t granularity;
    uint16_t window_size;
    uint16_t segment_a, segment_b;
    uint32_t win_func_ptr;
    uint16_t pitch; // bytes per scanline
    uint16_t width, height;
    uint8_t w_char, y_char, planes, bpp, banks;
    uint8_t memory_model, bank_size, image_pages;
    uint8_t reserved0;

    uint8_t red_mask_size, red_field_pos;
    uint8_t green_mask_size, green_field_pos;
    uint8_t blue_mask_size, blue_field_pos;
    uint8_t rsv_mask_size, rsv_field_pos;
    uint8_t directcolor_mode_info;

    uint32_t physbase; // phys addr of framebuffer
    uint32_t offscreen_off;
    uint16_t offscreen_size;
} vbe_mode_info_t;

int vbe_find_mode(uint16_t desired_w, uint16_t desired_h, uint8_t desired_bpp, uint16_t *mode_out, vbe_mode_info_t *info_out);
int vbe_set_mode(uint16_t mode);
extern uint32_t framebuffer_phys;
extern uint16_t framebuffer_pitch;
extern uint16_t framebuffer_width;
extern uint16_t framebuffer_height;
extern uint8_t framebuffer_bpp;
