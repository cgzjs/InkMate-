#ifndef __GUI_H
#define __GUI_H
#include "EPaper_213_Driver.h"
typedef struct
{
  uint16_t x;
  uint16_t y;
} Location_Struct;
extern uint8_t Display_buffer[5618];

static constexpr uint8_t DISPLAY_ROTATION_OFFSET = 2;

void Fill_Screen(uint8_t grey,bool mode);
void DrawPixel(uint16_t x, uint16_t y, uint8_t grey, bool mode, uint8_t rotation);
void DrawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t grey, bool mode, uint8_t rotation);
void DrawCircle(uint8_t x, uint8_t y, uint8_t R, uint8_t grey, bool mode, uint8_t rotation);
void DrawFont_GBK16(uint8_t x, uint8_t y, uint8_t *s, uint8_t fc,uint8_t bc, bool mode, uint8_t rotation);
void DrawFont_Num32(uint8_t x, uint8_t y, uint8_t *s, uint8_t fc, uint8_t bc, bool mode, uint8_t rotation);
void DrawFont_GBK24(uint8_t x, uint8_t y, uint8_t *s, uint8_t fc,uint8_t bc, bool mode, uint8_t rotation);
void DrawFont_Weather32(uint8_t x, uint8_t y, uint8_t Wcode, uint8_t fc, uint8_t bc, bool mode, uint8_t rotation);

#endif
