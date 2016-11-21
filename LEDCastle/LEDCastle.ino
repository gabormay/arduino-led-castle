#include <SPI.h>

const int rckPin = 5;
const int nLED = 32;

const unsigned int MODE_NORMAL = 0;
const unsigned int MODE_RAID   = 1;
const unsigned int MODE_CANDLE = 2;
const unsigned int MODE_FLASH2 = 3;
const unsigned int MODE_FLASH1 = 4;

const unsigned int FLASH_11 = 0;
const unsigned int FLASH_12 = 1;
const unsigned int FLASH_21 = 2;
const unsigned int FLASH_13 = 3;


// bits 0-4: current level (0-31) / pwm duty cycle
// bits 5-9: desired level 
// bits 10-12: mode
//    0 - normal (fade to desired)
//    1 - tv/raid 
//    2 - candle
//    3 - flashing, 1/2 sec unit
//    4 - flashing, 1  sec unit
//    5-7 - reserved
// bits 13-15: flash cycle (on/off)
//    0 - 1/1
//    1 - 1/2
//    2 - 2/1
//    3 - 1/3

unsigned int LEDs[nLED]; 

const byte MASK_CURRENT_LEVEL = 0x1F;

#define LED_GET_CURRENT_LEVEL(led)  ((led) & 31)
#define LED_GET_DESIRED_LEVEL(led)  (((led)>>5) & 31)
#define LED_GET_MODE(led)           (((led)>>10) & 7)
#define LED_GET_FLASH_CYCLE(led)    (((led)>>13) & 3)

#define LED_SET_CURRENT_LEVEL(led, level)   led = (((led) & ~0x001F) | ((level) & 31))  
#define LED_SET_DESIRED_LEVEL(led, level)   led = (((led) & ~(31<<5) ) | (((level) & 31)<<5))  

// scale pwm exponentially for desired brightness
// x(i) = (32^(1/30))^i
const byte LVL2PWM[] = {
  0, 1, 1, 1, 1, 2, 2, 2, 
  2, 3, 3, 3, 4, 4, 4, 5, 
  6, 6, 7, 8, 9, 10, 11, 13, 
  14, 16, 18, 20, 23, 25, 29, 31};

void setup() {
  // set the slaveSelectPin as an output:
  pinMode(rckPin, OUTPUT);
  pinMode(13, OUTPUT);
  digitalWrite(rckPin, LOW); 
  // initialize SPI:
  SPI.begin();
  Serial.begin(57600);
  delay(100);

  for (int i=0; i<nLED; i++)
    LEDs[i] = 0;

  LEDs[0] =                    (MODE_CANDLE << 10)             + 0;
  LEDs[1] =                    (MODE_NORMAL << 10) + (31 << 5) + 0;
  LEDs[2] = (FLASH_12 << 13) + (MODE_FLASH2 << 10) + (0  << 5) + 0;
  LEDs[3] = (FLASH_11 << 13) + (MODE_FLASH1 << 10)             + 0;

  Serial.println(LEDs[0]);
  Serial.println(LEDs[1]);
  Serial.println(LEDs[2]);
  Serial.println(LEDs[3]);
  
  SPI.beginTransaction(SPISettings(14000000, MSBFIRST, SPI_MODE0));
}

unsigned long clk = 0; // incremented on every display cycle

void loop() {
  unsigned int dcount = clk & 31; // display duty cycle counter (0-31)
  
  unsigned int current, desired, mode;
  
  unsigned long bank = 0;
  for (int i=0; i<nLED; i++) {
    current = LED_GET_CURRENT_LEVEL(LEDs[i]);
    if (current && dcount <= LVL2PWM[current])
      bank = bank | 1 << i;
  }
  writeBank(bank);

  // take the time to update one LED at a time during a display cycle
  // this way, all LEDs take their next value by the time the display cycle ends
  current = LED_GET_CURRENT_LEVEL(LEDs[dcount]);
  desired = LED_GET_DESIRED_LEVEL(LEDs[dcount]);
  mode = LED_GET_MODE(LEDs[dcount]);

  if (mode == MODE_NORMAL) {
    if (current < desired)
      current++;
    else if (current > desired)
      current--;
  }

  if (mode == MODE_RAID) {
    current = random(32);
  }

  if (mode == MODE_CANDLE) {
    if (random(10)==0)
      current = 20 + random (12);
  }

  if ((mode == MODE_FLASH2) || (mode == MODE_FLASH1))
  {
    unsigned int flash = LED_GET_FLASH_CYCLE(LEDs[dcount]);
    // note: desired is phase control
    unsigned int fc = (mode == MODE_FLASH2) ? ((clk - desired) >> 10) : ((clk - desired) >> 11);
    unsigned int dc = ((clk - desired) >> 5) & 31;

    switch (flash) {
      case FLASH_11:
        current = ((fc & 1) == 0) ? 31 : 0;
        break;
      case FLASH_12:
        switch (fc % 3) {
          case 0 : current = 31; break;
          case 1 : current = 0; break;
          case 2 : current = 0; break;
        }
        break;
      case FLASH_21:
        switch (fc % 3) {
          case 0 : current = 31; break;
          case 1 : current = 31; break;
          case 2 : current = 0; break;
        }
        break;
      case FLASH_13:
        switch (fc & 3) {
          case 0 : current = 31;break; // dc; break;
          case 1 : 
          case 2 : 
          case 3 : current = 0; break;
        }
        break;
    }
  }

  LED_SET_CURRENT_LEVEL(LEDs[dcount], current);

  delayMicroseconds(340);
  clk++;
}

void writeBank(unsigned long bank) {
  SPI.transfer16(bank >> 16);
  SPI.transfer16(bank & 0xFFFF);
  delayMicroseconds(1);
  digitalWrite(rckPin, HIGH);
  delayMicroseconds(1);
  digitalWrite(rckPin, LOW);
}

