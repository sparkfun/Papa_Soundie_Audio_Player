#include <SparkFun_PapaSoundie.h>


#define PIR_DOUT 9   // PIR digital output on D2

int count = 0;

PapaSoundie sfx = PapaSoundie();

void setup() {
  Serial.begin(115200);  // Serial is used to view Analog out
  // Analog and digital pins should both be set as inputs:
  pinMode(PIR_DOUT, INPUT);
  sfx.begin();

}

void loop() {
  Serial.println(count);
  int motionStatus = digitalRead(PIR_DOUT);
  if (motionStatus == HIGH)
  {
    sfx.playFileNumber(1);
    delay(3000);
    count = count +1; 
    if (count == 5)
    {
      sfx.playFileNumber(2);
      delay(3000);
      count = 0;
    }
  }
}




