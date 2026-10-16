#include "EPD.h"
#include <SPI.h>
//#include "lut.h"
#include <math.h>
#include <Arduino.h>

void EPD_Init()
{
  // 初始化引脚
  pinMode(EPD_BUSY_PIN,  INPUT);
  pinMode(EPD_RST_PIN , OUTPUT);
  pinMode(EPD_DC_PIN  , OUTPUT);
  pinMode(EPD_CS_PIN , OUTPUT);

  digitalWrite(EPD_CS_PIN , HIGH);
  digitalWrite(EPD_DC_PIN , LOW);

  // 初始化SPI
// #if defined(ES32)
  SPI.begin(EPD_SCK_PIN,-1,EPD_SDA_PIN,EPD_CS_PIN);
// #else
//   SPI.pins(EPD_SCK_PIN,-1,EPD_SDA_PIN,EPD_CS_PIN);
//   SPI.begin();
// #endif
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

  // 初始化墨水屏
  Serial.println("Initializing EPD.");
  digitalWrite(EPD_RST_PIN,LOW);
  delay(100);
  digitalWrite(EPD_RST_PIN,HIGH);
  delay(100);



  Serial.println("EPD Initializing Complete.");
}


void hardwareSpi(uint8_t data)
{
  SPI.transfer(data);
}

void sendCmd(uint8_t cmd)
{
  digitalWrite( EPD_DC_PIN, LOW );      //DC Low
  digitalWrite( EPD_CS_PIN, LOW ); 
  hardwareSpi(cmd);
  digitalWrite( EPD_CS_PIN, HIGH );     //CS High
  // digitalWrite( DC_PIN, HIGH );     //DC High
}

void sendData(uint8_t data)
{
  digitalWrite( EPD_DC_PIN, HIGH );      //DC Low
  digitalWrite( EPD_CS_PIN, LOW ); 
  hardwareSpi(data);
  digitalWrite( EPD_CS_PIN, HIGH );     //CS High
  // digitalWrite( DC_PIN, LOW );     //DC High
}
