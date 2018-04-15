/*
Papa Soundie Getting Started Guide: Software Example 
Mary West
SparkFun Electronics

This code is released under the MIT License (http://opensource.org/licenses/MIT)

******************************************************************************/
#include "SparkFun_PapaSoundie.h"

PapaSoundie sfx = PapaSoundie();

void setup() {
  sfx.begin();
  Serial.begin(115200);
}

// the loop function runs over and over again forever
void loop() {
	for(int i = 0; i < 25; i++)
	{
		Serial.print("Playing: ");
		Serial.println(i);
		sfx.playFileNumber(i);
		delay(2000);
	}
}

