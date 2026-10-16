#include "gui.h"
#include "weather.h"
/*********************************************/
//Fill_Screen
//mode 1-灰度模式/0-黑白模式
//grey灰度等级或黑白颜色
/*********************************************/
void Fill_Screen(uint8_t grey,bool mode) 
{
  uint8_t color;
  uint16_t i,end;
  if(mode)
  {
    end=(uint16_t)WIDTH*HEIGHT/4; 
    color = (grey&0x03)*85;    
  }
  else
  {
    end=(uint16_t)WIDTH*HEIGHT/8; 
    if(grey) color = 0x00;
    else    color = 0xFF;
  }
  for(i=0;i<end;i++)
  {
    Display_buffer[i]= color;
  }
}
/*********************************************/
//DrawPixel
//mode 1-灰度模式/0-黑白模式
//grey灰度等级或黑白颜色
/*********************************************/
void DrawPixel(uint16_t x, uint16_t y, uint8_t grey, bool mode, uint8_t rotation)
{
  uint16_t idx,t;
  int16_t xn,yn;
  // Apply the panel mounting orientation to every drawing path.
  switch ((rotation + DISPLAY_ROTATION_OFFSET) & 0x03) {
  case 1:
    yn = x;
    xn = WIDTH - 1 - y;
    break;
  case 2:
    xn = WIDTH - 1 - x;
    yn = HEIGHT - 1 - y;
    break;
  case 3:
    xn = y;
    yn = HEIGHT - 1 - x;
    break;
  default:
    xn=x;
    yn=y;
    break;
  }
  if((xn < 0)|| (yn < 0))
    return;

  if(mode)
  {
    idx=(xn + yn * WIDTH)>>2;
    t = (3 - xn%4)*2;
  }
  else
  {
    idx=(xn + yn * WIDTH)>>3;
    t = 7 - x%8;   
  }
  if(idx<=5511)
  {
    Display_buffer[idx] &= ~(0x03 << t);
    Display_buffer[idx] |= (grey << t);
  }
}
/*********************************************/
//Get_new_xy
//换行
/*********************************************/
Location_Struct Get_new_xy(uint16_t x, uint16_t y, uint8_t width_x, uint8_t width_y, uint8_t rotation)
{
  Location_Struct Location_reposition;
  if(rotation%2)
  {
    if((x+width_x) > HEIGHT)
    {
      x = 0;
      y += width_y;
    }
  }
  else
  {
    if((x+width_x) > WIDTH)
    {
      x = 0;
      y += width_y;
    }
  }
  Location_reposition.x=x;
  Location_reposition.y=y;
  return Location_reposition;
}
/*********************************************/
//DrawLine
//mode 1-灰度模式/0-黑白模式
//grey灰度等级或黑白颜色
/*********************************************/
void DrawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t grey, bool mode, uint8_t rotation)
{
  int16_t dx,dy,dx2,dy2,x_inc,y_inc,error,index;
  
  dx=x1-x0;
  dy=y1-y0;
  if(dx>=0)
    x_inc = 1;
  else
  {
    x_inc = -1;
    dx= -dx;
  }
  if(dy>=0)
    y_inc = 1;
  else
  {
    y_inc = -1;
    dy= -dy;
  }
  dx2 = dx<<1;
  dy2 = dy<<1;
  if(dx>dy)
  {
    error=dy2-dx;
    for(index=0;index<=dx;index++)
    {
      DrawPixel(x0, y0, grey, mode, rotation);
      if(error>=0)
      {
        error -= dx2;
        y0 += y_inc;
      }
      error +=dy2;
      x0 += x_inc;
    }
  }
  else
  {
    error=dx2-dy;
    for(index=0;index<=dy;index++)
    {
      DrawPixel(x0, y0, grey, mode, rotation);
      if(error>=0)
      {
        error -= dy2;
        x0 += x_inc;
      }
      error +=dx2;
      y0 += y_inc;
    }
  }
}
/*********************************************/
//DrawCircle
//mode 1-灰度模式/0-黑白模式
//grey灰度等级或黑白颜色
/*********************************************/
void DrawCircle(uint8_t x, uint8_t y, uint8_t R, uint8_t grey, bool mode, uint8_t rotation)
{
  uint8_t a=0,b;
  float c;
  
  b=R;
  c=1.25-R;
  while(a<b)
  {
    DrawPixel(x+a, y+b, grey, mode, rotation);
    DrawPixel(x-a, y+b, grey, mode, rotation);
    DrawPixel(x+a, y-b, grey, mode, rotation);
    DrawPixel(x-a, y-b, grey, mode, rotation);
    DrawPixel(x+b, y+a, grey, mode, rotation);
    DrawPixel(x-b, y+a, grey, mode, rotation);
    DrawPixel(x+b, y-a, grey, mode, rotation);
    DrawPixel(x-b, y-a, grey, mode, rotation);
    
    if(c<0) c=c+2*(a)+1;
    else
    {
      c=c+2*(a-b)+1;
      b -=1;
    }
    a+=1;
  }
  if(a==b)
  {
    DrawPixel(x+a, y+b, grey, mode, rotation);
    DrawPixel(x-a, y+b, grey, mode, rotation);
    DrawPixel(x+a, y-b, grey, mode, rotation);
    DrawPixel(x-a, y-b, grey, mode, rotation);
    DrawPixel(x+b, y+a, grey, mode, rotation);
    DrawPixel(x-b, y+a, grey, mode, rotation);
    DrawPixel(x+b, y-a, grey, mode, rotation);
    DrawPixel(x-b, y-a, grey, mode, rotation);
  }
}

/*********************************************/
//DrawFont_Num32
//mode 1-灰度模式/0-黑白模式
//grey灰度等级或黑白颜色
/*********************************************/
void DrawFont_Weather32(uint8_t x, uint8_t y, uint8_t Wcode, uint8_t fc, uint8_t bc, bool mode, uint8_t rotation)
{
  uint8_t i,j,k,c;
  k=Wcode;
  for(i=0;i<32;i++)
  {
    for(c=0;c<4;c++)
    for(j=0;j<8;j++) 
    {
      if(Weather32x32[k*28+i*4+c]&(0x80>>j))	DrawPixel(x+c*8+j, y+i, fc, mode, rotation);
      else DrawPixel(x+c*8+j, y+i, bc, mode, rotation);
    }
  }
}
