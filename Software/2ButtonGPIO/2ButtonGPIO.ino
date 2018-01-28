/*
 * GPIO PINS: 
 *  
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
 * Maps a file number to it's binary representation on the GPIO pins e.g. 
 * 5 = 00101, GPIO_PLAY05 = 0, GPIO_PLAY04 = 0, GPIO_PLAY03 = 1, GPIO_PLAY02 = 0, GPIO_PLAY01 = 1
 * 7 = 00111, GPIO_PLAY05 = 0, GPIO_PLAY04 = 0, GPIO_PLAY03 = 1, GPIO_PLAY02 = 1, GPIO_PLAY01 = 1
 * 23 = 10111 GPIO_PLAY05 = 1, GPIO_PLAY04 = 0, GPIO_PLAY03 = 1, GPIO_PLAY02 = 1, GPIO_PLAY01 = 1
 */
void playFileNumber(int number) {
  digitalWrite(GPIO_PLAY01, number & 0x01);  
  digitalWrite(GPIO_PLAY02, number & 0x02);
  digitalWrite(GPIO_PLAY03, number & 0x04);
  digitalWrite(GPIO_PLAY04, number & 0x08);
  digitalWrite(GPIO_PLAY05, number & 0x10);
}

/**
 * Sets the required GPIO pins as output
 */
void setupGPIOPins() {
  pinMode(GPIO_PLAY01, OUTPUT);
  pinMode(GPIO_PLAY02, OUTPUT);
  pinMode(GPIO_PLAY03, OUTPUT);
  pinMode(GPIO_PLAY04, OUTPUT);
  pinMode(GPIO_PLAY05, OUTPUT);
}

void setup() {
  pinMode(GPIO_BUTTON1, INPUT);
  pinMode(GPIO_BUTTON2, INPUT);
  pinMode(GPIO_VLSI_POWER_BTN, OUTPUT);
  pinMode(GPIO_VLSI_POWER_BTN, OUTPUT);
  pinMode(GPIO_ATMEGA_POWER, OUTPUT);

  digitalWrite(GPIO_VLSI_POWER_BTN, 0);
  digitalWrite(GPIO_VLSI_POWER_BTN, 1);
  delay(50);
  digitalWrite(GPIO_VLSI_POWER_BTN, 0);
  digitalWrite(GPIO_ATMEGA_POWER, 1);
  setupGPIOPins();
}

// the loop function runs over and over again forever
void loop() {
  
  int button1 = digitalRead(GPIO_BUTTON1);
  int button2 = digitalRead(GPIO_BUTTON2);
  playFileNumber(button2 << 1 | button1);
}
