#include "EPaper_213_Driver.h"

const uint8_t bw2grey[] =
{
  /*0b00000000,0b00000011,0b00001100,0b00001111,
  0b00110000,0b00110011,0b00111100,0b00111111,
  0b11000000,0b11000011,0b11001100,0b11001111,
  0b11110000,0b11110011,0b11111100,0b11111111*/
  0x00,0x03,0x0C,0x0F,0x30,0x33,0x3C,0x3F,0xC0,0xC3,0xCC,0xCF,0xF0,0XF3,0xFC,0xFF
};
//00b: VCM_DC     01b: 15V+VCM_DC (VCOMH)     10b: -15V+VCM_DC (VCOML)     11b: Floating
//vcom ->(0*15+0*20)*1+(0*10+0*6)*4+(0*10+0*10)*15+(0*3+0*3)*12+(0*6+0*10)*0
//w    ->(0*15+0*20)*1+(0*10+1*6)*4+(2*10+1*10)*15+(2*3+1*3)*12+(2*6+0*10)*4
//b    ->(0*15+0*20)*1+(2*10+0*6)*4+(2*10+1*10)*15+(2*3+1*3)*12+(0*6+1*10)*4
//g1   ->(2*15+2*20)*1+(2*10+0*6)*4+(2*10+1*10)*15+(2*3+1*3)*12+(2*6+0*10)*4
//g2   ->(2*15+2*20)*1+(2*10+0*6)*4+(2*10+1*10)*15+(2*3+1*3)*12+(0*6+0*10)*4
const uint8_t lut_20_vcom0[]  = {0x0E,0x14,0x01,0x0A,0x06,0x04,0x0A,0x0A,0x0F,0x03,0x03,0x0C,0x06,0x0A,0x00};
const uint8_t lut_21_w[]  = {0x0E,0x14,0x01,0x0A,0x46,0x04,0x8A,0x4A,0x0F,0x83,0x43,0x0C,0x86,0x0A,0x04};
const uint8_t lut_22_b[]  = {0x0E,0x14,0x01,0x8A,0x06,0x04,0x8A,0x4A,0x0F,0x83,0x43,0x0C,0x06,0x4A,0x04};
const uint8_t lut_23_g1[]  = {0x8E,0x94,0x01,0x8A,0x06,0x04,0x8A,0x4A,0x0F,0x83,0x43,0x0C,0x06,0x0A,0x04};
const uint8_t lut_24_g2[]  = {0x8E,0x94,0x01,0x8A,0x86,0x04,0x8A,0x4A,0x0F,0x83,0x43,0x0C,0x86,0x0A,0x04};
//const uint8_t lut_24_g2[]  = {0x8E,0x94,0x01,0x8A,0x06,0x04,0x8A,0x4A,0x0F,0x83,0x43,0x0C,0x06,0x0A,0x04};
//const uint8_t lut_25_vcom1[]  = {0x03,0x1D,0x01,0x01,0x08,0x23,0x37,0x37,0x01,0x00,0x00,0x00,0x00,0x00,0x00 };
//const uint8_t lut_26_red0[]  = {0x83,0x5D,0x01,0x81,0x48,0x23,0x77,0x77,0x01,0x00,0x00,0x00,0x00,0x00,0x00 };
//const uint8_t lut_27_red1[]  = {0x03,0x1D,0x01,0x01,0x08,0x23,0x37,0x37,0x01,0x00,0x00,0x00,0x00,0x00,0x00 };
const uint8_t lut_25_vcom1[]  = {0x03,0x1D,0x00,0x01,0x08,0x00,0x37,0x37,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
const uint8_t lut_26_red0[]  = {0x83,0x5D,0x00,0x81,0x48,0x00,0x77,0x77,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
const uint8_t lut_27_red1[]  = {0x03,0x1D,0x00,0x01,0x08,0x00,0x37,0x37,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

#define WriteCommand    sendCmd
#define WriteData       sendData

// void delay(uint16_t time)
// {
//   SysTickDelayMs(time);
// }
/**************************************************************************/
// void WriteData(uint8_t Dat)
// {
// 	SET_CS_LOW;
// 	SPI_Sendchar(Dat);
// 	SET_CS_HIGH;
// }
// /**************************************************************************/
// void WriteCommand(uint8_t Command)
// {
// 	SET_DC_LOW;
// 	SET_CS_LOW;
// 	SPI_Sendchar(Command);
// 	SET_CS_HIGH;
// 	SET_DC_HIGH;
// }
/**************************************************************************/
void WriteMultiData(const uint8_t *p,uint8_t length)
{
	uint8_t i;
	SET_CS_LOW;
	for(i=0;i<length;i++)
	{
		sendData(p[i]);
	}
	SET_CS_HIGH;
}
/**************************************************************************/
uint8_t Wait_Busy(uint16_t time_out)
{
	uint16_t time_cnt=0;
	while(!EPD_BUSY_VAL&&(time_cnt<time_out))
	{
		delay(1);
		time_cnt++;
	}
	if(time_cnt >= time_out)
		return 1;
	else
		return 0;
}
/**************************************************************************/
void EPaper_PowerOff(void)
{
  WriteCommand(0x50);
  WriteData(0x17);    //BD floating
  WriteCommand(0x82);     //to solve Vcom drop
  WriteData(0x00);
  WriteCommand(0x01);     //power setting
  WriteData(0x02);    //gate switch to external
  WriteData(0x00);
  WriteData(0x00);
  WriteData(0x00);
  WriteCommand(0x02); // power off
  Wait_Busy(power_off_time);
}
/**************************************************************************/
uint8_t EPaper_PowerOn(void)
{
	WriteCommand(0x04);
	return Wait_Busy(power_on_time);
}
/**************************************************************************/
void EPaper_reset(void)
{
	SET_RST_LOW;
	delay(10);
	SET_RST_HIGH;
	delay(10);
}
/**************************************************************************/
void EPaper_InitDisplay(void)
{
  EPaper_reset();
  WriteCommand(0x01);
  WriteData(0x07);
  WriteData(0x00);
  WriteData(0x08);
  WriteData(0x00);
  WriteCommand(0x06);
  WriteData(0x07);
  WriteData(0x07);
  WriteData(0x07);
  EPaper_PowerOn(); 
  WriteCommand(0x00);
  WriteData(0xcf);
  WriteCommand(0x50);
  WriteData(0x37);
  WriteCommand(0x30);
  WriteData(0x39);
  WriteCommand(0x61);
  WriteData(WIDTH);
  WriteData(0x00);
  WriteData(HEIGHT);
  WriteCommand(0x82);
  WriteData(0x0E);
}
/**************************************************************************/
void EPaper_Init(void)
{
  EPaper_InitDisplay();
  WriteCommand(0x20);
  WriteMultiData(lut_20_vcom0,15);
  WriteCommand(0x21);
  WriteMultiData(lut_21_w,15);
  WriteCommand(0x22);
  WriteMultiData(lut_22_b, 15);
  WriteCommand(0x23);
  WriteMultiData(lut_23_g1, 15);
  WriteCommand(0x24);
  WriteMultiData(lut_24_g2, 15);
  WriteCommand(0x25);
  WriteMultiData(lut_25_vcom1, 15);
  WriteCommand(0x26);
  WriteMultiData(lut_26_red0, 15);
  WriteCommand(0x27);
  WriteMultiData(lut_27_red1, 15);
}
/**************************************************************************/
void EPaper_refresh(void)
{
  WriteCommand(0x12); //display refresh
  Wait_Busy(full_refresh_time);
}
/**************************************************************************/
void EPaper_WriteBWImage(const uint8_t* black,uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool invert, bool mirror_y)
{
  uint8_t i,j,wb;
  uint8_t data_buf;
  uint16_t idx;
  if((w > (uint8_t)WIDTH)||(h > (uint8_t)HEIGHT))
    return;
  wb=(w + 7) / 8;
  x -= x % 8; // byte boundary
  w = wb * 8; // byte boundary
  EPaper_Init();
  WriteCommand(0x10);
  for (i = 0; i < (uint8_t)HEIGHT; i++)
  {
    for (j = 0; j < (uint8_t)WIDTH; j += 8)
    {			
      if ((j >= x) && (j < x + w) && (i >= y) && (i < y + h))
      {
        idx = mirror_y ? (j - x) / 8 + ((h - 1 - (i - y))) * wb : (j - x) / 8 + (i - y) * wb;
        data_buf = black[idx];
          
        if (invert) data_buf = ~data_buf;
      }
      else
        data_buf=0xFF;
      WriteData(bw2grey[(data_buf & 0xF0) >> 4]);
      WriteData(bw2grey[data_buf & 0x0F]);
    }
  }
  EPaper_refresh();
  EPaper_PowerOff();
}
/**************************************************************************/
void EPaper_WriteGreyImage(const uint8_t* Grey,uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool invert, bool mirror_y)
{
  uint8_t i,j,wb;
  uint8_t data_buf;
  uint16_t idx;
  if((w > (uint8_t)WIDTH)||(h > (uint8_t)HEIGHT))
    return ;
  wb=(w + 7) / 8;
  x -= x % 8; // byte boundary
  w = wb * 8; // byte boundary
  EPaper_Init();
  WriteCommand(0x10);
  for (i = 0; i < (uint8_t)HEIGHT; i++)
  {
    for (j = 0; j < (uint8_t)WIDTH; j += 8)
    {			
      if ((j >= x) && (j < x + w) && (i >= y) && (i < y + h))
      {
        idx = mirror_y ? (j - x) / 4 + ((h - 1 - (i - y))) * wb * 2 : (j - x) / 4 + (i - y) * wb * 2;
        data_buf = Grey[idx++];
        if (invert) data_buf = ~data_buf;
        WriteData(data_buf);
        data_buf = Grey[idx];
        if (invert) data_buf = ~data_buf;
        WriteData(data_buf);
      }
      else
      {
        WriteData(0xFF);
        WriteData(0xFF);
      }
    }
  }
  EPaper_refresh();
  EPaper_PowerOff();
}
/**************************************************************************/
