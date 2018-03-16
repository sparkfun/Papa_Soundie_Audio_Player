/*
Papa Soundie Getting Started Guide: Template
Mary West
SparkFun Electronics
https://www.sparkfun.com/products/14346

Set up the GPIO, turn on the VS1000D, set Arduino status LED & introduce the play file number function. 

Follow the tutorial at: (insert link)

This code is released under the MIT License (http://opensource.org/licenses/MIT)

******************************************************************************/

/*
   GPIO PINS:
*/

const int GPIO_PLAY01 = 2;
const int GPIO_PLAY02 = 3;
const int GPIO_PLAY03 = 4;
const int GPIO_PLAY04 = 5;
const int GPIO_PLAY05 = 6;
const int GPIO_BUTTON1 = 9;
const int GPIO_BUTTON2 = 10;
const int GPIO_VLSI_POWER_BTN = 7;
const int GPIO_ATMEGA_POWER = A2;


/**
   Maps a file number to it's binary representation on the GPIO pins e.g.
   5 = 00101, GPIO_PLAY05 = 0, GPIO_PLAY04 = 0, GPIO_PLAY03 = 1, GPIO_PLAY02 = 0, GPIO_PLAY01 = 1
   7 = 00111, GPIO_PLAY05 = 0, GPIO_PLAY04 = 0, GPIO_PLAY03 = 1, GPIO_PLAY02 = 1, GPIO_PLAY01 = 1
   23 = 10111 GPIO_PLAY05 = 1, GPIO_PLAY04 = 0, GPIO_PLAY03 = 1, GPIO_PLAY02 = 1, GPIO_PLAY01 = 1
*/


/**
   Sets the required GPIO pins as output
*/
void setupGPIOPins() {
  pinMode(GPIO_PLAY01, OUTPUT);
  pinMode(GPIO_PLAY02, OUTPUT);
  pinMode(GPIO_PLAY03, OUTPUT);
  pinMode(GPIO_PLAY04, OUTPUT);
  pinMode(GPIO_PLAY05, OUTPUT);
}

void setup() {

  //Set up buttons & sensors as needed.
  
  pinMode(GPIO_VLSI_POWER_BTN, OUTPUT); // The arduino turns on the VS1000D device. 
  pinMode(GPIO_ATMEGA_POWER, OUTPUT); // Arduino status LED

  digitalWrite(GPIO_VLSI_POWER_BTN, 0); //Initialize state of powerbutton to off 
  digitalWrite(GPIO_VLSI_POWER_BTN, 1); //Turn on VS1000D device 
  delay(50); //power should only be applied to the VS1000D for 50mS. 
  digitalWrite(GPIO_VLSI_POWER_BTN, 0); //Set VS1000D power button low and allow VS1000D to work independently. 
  digitalWrite(GPIO_ATMEGA_POWER, 0);   //Turn Arduino Status LED off, set to 1 if you would like to status indicator. 
  
  setupGPIOPins(); //Arduino is getting ready to trigger some pins. 
}

// the loop function runs over and over again forever
void loop() {
  //Insert conditions for playback here
  playFileNumber(); //Insert file number. Can be from a random function or a loop. 
}
void playFileNumber(int number) {
  digitalWrite(GPIO_PLAY01, number & 0x01);
  digitalWrite(GPIO_PLAY02, number & 0x02);
  digitalWrite(GPIO_PLAY03, number & 0x04);
  digitalWrite(GPIO_PLAY04, number & 0x08);
  digitalWrite(GPIO_PLAY05, number & 0x10);
}
