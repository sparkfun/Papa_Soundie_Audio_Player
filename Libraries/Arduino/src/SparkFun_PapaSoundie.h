/******************************************************************************
SFE_PapaSoundie.h
SFE_PapaSoundie Library Header File
Mary West @ SparkFun Electronics
Original Creation Date: 3/16/2018


This file sets the pinmode functions and declares the necessary variables
to use the playFileNumber() function. With this function alone any form of trigger
playback is possible. 

Development environment specifics:
	IDE: Arduino 1.8.5
	Hardware Platform: Arduino Uno
	SparkFun Papa Soundie 

This code is beerware; if you see me (or any other SparkFun employee) at the
local, and you've found our code helpful, please buy us a round!

Distributed as-is; no warranty is given.
******************************************************************************/
#include "Arduino.h"


class PapaSoundie   
{
 public: 
	PapaSoundie(void);

	void begin();
	void playFileNumber(int number);
	void playRepeat(int number);
	void stopRepeat(void);
	static const int GPIO_PLAY01 = 2;
	static const int GPIO_PLAY02 = 3;
	static const int GPIO_PLAY03 = 4;
	static const int GPIO_PLAY04 = 5;
	static const int GPIO_PLAY05 = 6;
	static const int GPIO_VLSI_POWER_BTN = 7;
	static const int GPIO_ATMEGA_POWER = A2;
	
 
 private:
 	
	void setPinNumber(int number);

};

