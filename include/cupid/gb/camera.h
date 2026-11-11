#ifndef CUPID_GB_CAMERA_H
#define CUPID_GB_CAMERA_H

#include "cupid/gb/gb.h"

enum { CUPID_GB_CAMERA_MAX_BUSY_T_CYCLES = 129792u + 2048u + 65535u * 64u + 4u };

void cupid_gb_camera_mapper_write(CupidGb *gb, uint16_t address, uint8_t value);
void cupid_gb_camera_tick(CupidGbCamera *camera, unsigned cpu_tcycles);
uint8_t cupid_gb_camera_read_register(const CupidGbCamera *camera, uint16_t address);
void cupid_gb_camera_write_register(CupidGbCamera *camera, uint16_t address, uint8_t value);
uint8_t cupid_gb_camera_read_tile(const CupidGbCamera *camera, unsigned offset);

#endif
