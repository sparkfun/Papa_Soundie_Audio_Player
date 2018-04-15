/******************************************************************************
SparkFun_PapaSoundie.cpp
SparkFun Papa Soundie Audio Player Library CPP File
Mary West @ SparkFun Electronics
Original Creation Date: 3/16/2018


This file prototypes the PapaSoundie class, implemented in SparkFun_PapaSoundie.cpp

Development environment specifics:
	IDE: Arduino 1.8.5
	Hardware Platform: Arduino Uno
	SparkFun Papa Soundie Audio Player 

This code is beerware; if you see me (or any other SparkFun employee) at the
local, and you've found our code helpful, please buy us a round!

Distributed as-is; no warranty is given.
******************************************************************************/

#include "SparkFun_PapaSoundie.h"
#include "Arduino.h"

PapaSoundie::PapaSoundie( void ) 
{

}

void PapaSoundie::begin()
{
	pinMode(PapaSoundie::GPIO_PLAY01, OUTPUT);
	pinMode(PapaSoundie::GPIO_PLAY02, OUTPUT);
	pinMode(PapaSoundie::GPIO_PLAY03, OUTPUT);
	pinMode(PapaSoundie::GPIO_PLAY04, OUTPUT);
	pinMode(PapaSoundie::GPIO_PLAY05, OUTPUT);
  
	pinMode(PapaSoundie::GPIO_VLSI_POWER_BTN, OUTPUT); // The Arduino turns on the VS1000D device. 
	pinMode(PapaSoundie::GPIO_ATMEGA_POWER, OUTPUT); // Arduino status LED

	digitalWrite(PapaSoundie::GPIO_VLSI_POWER_BTN, 0); //Initialize state of powerbutton to off 
	digitalWrite(PapaSoundie::GPIO_VLSI_POWER_BTN, 1); //Turn on VS1000D device 
	delay(50); //power should only be applied to the VS1000D for 50mS. 
	digitalWrite(PapaSoundie::GPIO_VLSI_POWER_BTN, 0); //Set VS1000D power button low and allow VS1000D to work independently. 
	digitalWrite(PapaSoundie::GPIO_ATMEGA_POWER, 1);   //Turn Arduino Status LED off, set to 1 if you would like to status indicator.
}

void PapaSoundie::playFileNumber(int number) 
{
	setPinNumber(number);
	delay(100);
	setPinNumber(0);
}

//Repeats playback until stop function is called 
//Great for an annoying cricket prank
void PapaSoundie::playRepeat(int number) 
{
	setPinNumber(number);
}

void PapaSoundie::stopRepeat(void) 
{
	setPinNumber(0);
}

void PapaSoundie::setPinNumber(int number)
{
	digitalWrite(PapaSoundie::GPIO_PLAY01, number & 0x01);
	digitalWrite(PapaSoundie::GPIO_PLAY02, (number & 0x02) >> 1);
	digitalWrite(PapaSoundie::GPIO_PLAY03, (number & 0x04) >> 2);
	digitalWrite(PapaSoundie::GPIO_PLAY04, (number & 0x08) >> 3);
	digitalWrite(PapaSoundie::GPIO_PLAY05, (number & 0x10) >> 4);
}



