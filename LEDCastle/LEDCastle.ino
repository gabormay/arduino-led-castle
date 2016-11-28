#include <SPI.h>

// number of LEDs served
const int nLED = 32;

// pin mappings
const int pinRCK= 5;          // RCK signal for the shift registers
const int pinModeSwitch = 4;  // Progamming Switch pin
const int pinBlack = 3;       // pin for Black button
const int pinRed = 2;         // pin for Red button

/*********************************/
/*** LED modes and definitions ***/
/*********************************/

// LED program modes
//    0 - normal (fade to desired)
//    1 - tv/raid 
//    2 - candle
//    3 - flashing, 1/2 sec unit
//    4 - flashing, 1  sec unit
//    5-7 - reserved
const unsigned int MODE_NORMAL = 0;
const unsigned int MODE_RAID   = 1;
const unsigned int MODE_CANDLE = 2;
const unsigned int MODE_FLASH2 = 3;
const unsigned int MODE_FLASH1 = 4;

// LED flash cycle defs
//    0 - 1/1 (1 cycle on, 1 cycle off)
//    1 - 1/2 (1 cycle on, 2 cycles off)
//    2 - 2/1 ...etc.
//    3 - 1/3
const unsigned int FLASH_11 = 0;
const unsigned int FLASH_12 = 1;
const unsigned int FLASH_21 = 2;
const unsigned int FLASH_13 = 3;

// A LED is represented by a 16 bit value
// bits 0-4: current level (0-31), brightness
// bits 5-9: desired level (or phase shift, see modes)
// bits 10-12: mode
// bits 13-15: flash cycle
// FIXME: might be more elegant with a bit field struct?
typedef unsigned int t_led;

// "Getters"
#define LED_GET_CURRENT_LEVEL(led)  ((led) & 31)
#define LED_GET_DESIRED_LEVEL(led)  (((led)>>5) & 31)
#define LED_GET_MODE(led)           (((led)>>10) & 7)
#define LED_GET_FLASH_CYCLE(led)    (((led)>>13) & 3)

// "Setters"
#define LED_SET_CURRENT_LEVEL(led, level)   led = (((led) & ~0x001F) | ((level) & 31))  

// "Constructors"
#define LED_FLASH(init, desired, mode, flash)   (((flash) << 13) + ((mode) << 10) + ((desired)  << 5) + (init))
#define LED_NORMAL(init, desired)               (((MODE_NORMAL) << 10) + ((desired)  << 5) + (init))
#define LED_OTHER(init, desired, mode)          (((mode) << 10) + ((desired)  << 5) + (init))


// Represents our array (pun intended!) of LEDs
t_led LEDs[nLED]; 

/*********************************/
/*** User patch (program)      ***/
/*********************************/

// A patch is a collection of the simultaneous individual LED programs

int bPatching = 0; // 0 - normal, 1 - programming
int patchLED; // current LED under programming
int userPatchChanged = 0;

// the LED configurations representing a user patch
t_led userPatchLEDs[nLED];

int currentPatch = 0; // the current patch that is playing (unless bPatching)
const int nPatch = 4; // number of patches

/*********************************/
/*** Buttons and switches      ***/
/*********************************/
typedef struct tagButton {
  byte current : 1; // current value as read from the i/o pin
  byte stable  : 1; // last stable value
  byte last    : 1; // last read value
  byte rising  : 1; // 1 if rising edge
  byte falling : 1; // 1 if falling edge
  unsigned int countLast; // count of cycles since last change
} t_button;

void initButton(t_button *b) {
  b->current = 0;
  b->stable = 0;
  b->last = 0;
  b->rising = 0;
  b->falling = 0;
  b->countLast = 0;
}

// Note: will clear rising/falling
void readButton(int pin, t_button *b) {
 
  b->rising = b->falling = 0;
  b->current = digitalRead(pin);

  if (b->last == b->current && b->countLast > 80)
  {
    // stable reading
    if (b->current == HIGH && b->stable == LOW) {
      b->rising = 1;
    } else if (b->current == LOW && b->stable == HIGH) {
      b->falling = 1;
    }
    b->stable = b->current;
  } else if (b->last != b->current) {
    b->countLast = 0;
    b->last = b->current;
  }
  b->countLast++;
}

t_button buttonBlack, buttonRed, buttonSwitch;

/*********************************/
/*** Initialization            ***/
/*********************************/
void setup() {
  // init buttons & switches
  pinMode(pinModeSwitch, INPUT);
  pinMode(pinBlack, INPUT);
  pinMode(pinRed, INPUT);
  initButton(&buttonSwitch);
  initButton(&buttonBlack);
  initButton(&buttonRed);
  
  // RCK signal for the registers
  pinMode(pinRCK, OUTPUT);
  digitalWrite(pinRCK, LOW); 

  // initialize SPI
  SPI.begin();
  SPI.beginTransaction(SPISettings(14000000, MSBFIRST, SPI_MODE0));
  writeBank(0l);

  // init Serial (for test/debug output)
  Serial.begin(57600);

  // init the user patch to all LEDs on
  for (int i=0; i<nLED; i++)
    userPatchLEDs[i] = LED_NORMAL(31, 31);

  // load the first patch (the user one)
  loadPatch(0); // USER

  // for good measure
  delay(10);
}


/*********************************/
/*** Main loop                 ***/
/*********************************/

unsigned long clk = 0; // incremented on every display cycle
void loop() {
  unsigned int dcount = clk & 31; // display duty cycle counter (0-31)
  unsigned int current, desired, mode;

  // read buttons
  readButton(pinModeSwitch, &buttonSwitch);
  readButton(pinBlack, &buttonBlack);
  readButton(pinRed, &buttonRed);
  bPatching = buttonSwitch.stable;
  
  displayPWMCycle(dcount);
  
  if (!bPatching) { // normal operation 
    
    if (buttonBlack.rising) { // patch change requested
      currentPatch = (currentPatch + 1) % nPatch;
      Serial.print("New program: "); Serial.println(currentPatch);
      loadPatch(currentPatch);
    } else if (buttonSwitch.falling) { // coming out of patching mode
      loadPatch(currentPatch);
    } else { // just continue to run the LED programs
      updateLED(dcount);
    }
    
  } else {  // patching
    if (buttonSwitch.rising) { // entering patching mode
      //patchLED = 0;
      for (int i=0; i<nLED; i++)
        LEDs[i] = (i==patchLED) ? userPatchLEDs[patchLED] : 0;
    }
    if (buttonBlack.rising) { // advance LED under patching
      LEDs[patchLED] = 0; 
      patchLED = (patchLED + 1) % 16; //nLED;
      Serial.print("Patching: advance LED "); Serial.println(patchLED);
      LEDs[patchLED] = userPatchLEDs[patchLED]; 
    }
    updateLED(dcount);

    if (buttonRed.rising) { // prog change for current LED
      Serial.print("New prog for led "); Serial.println(patchLED);
      
      int mode = LED_GET_MODE(userPatchLEDs[patchLED]);
      int desired = LED_GET_DESIRED_LEVEL(userPatchLEDs[patchLED]);
      int mode2 = (mode + 1) % 5;
      if (mode2 == MODE_NORMAL) {
        desired = 31;
      } else if (mode == MODE_NORMAL && desired > 5) {
        mode2 = MODE_NORMAL;
        desired = (desired == 31) ? 16 : 1;
      }
      mode = mode2;
      if (mode != MODE_NORMAL)
        desired = random(32);
      LEDs[patchLED] = userPatchLEDs[patchLED] = LED_OTHER(LED_GET_CURRENT_LEVEL(LEDs[patchLED]), desired, mode);
    }
  }

  delayMicroseconds(320);
  clk++;
}


void loadPatch(int patch) {
  switch (patch) {
    case 0: // USER
      for (int i=0; i<nLED; i++)
        LEDs[i] = LED_FLASH(LED_GET_CURRENT_LEVEL(LEDs[i]), LED_GET_DESIRED_LEVEL(userPatchLEDs[i]), LED_GET_MODE(userPatchLEDs[i]), LED_GET_FLASH_CYCLE(userPatchLEDs[i]));
      break;
    case 1: // ALL ON
      for (int i=0; i<nLED; i++)
        LEDs[i] = LED_NORMAL(LED_GET_CURRENT_LEVEL(LEDs[i]), 31);
      break;
    case 2: // MILD RANDOM
      break;
    case 3: // RAID
      for (int i=0; i<nLED; i++)
        LEDs[i] = LED_OTHER(LED_GET_CURRENT_LEVEL(LEDs[i]), random(32), MODE_RAID);
      break;
  }
  
}

void updateLED(int dcount)
{
  unsigned int current, desired, mode;
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
}

/*********************************/
/*** DISPLAY RELATED           ***/
/*********************************/

// scale pwm exponentially for desired brightness
// x(i) = (32^(1/30))^i
const byte LVL2PWM[] = {
  0, 1, 1, 1, 1, 2, 2, 2, 
  2, 3, 3, 3, 4, 4, 4, 5, 
  6, 6, 7, 8, 9, 10, 11, 13, 
  14, 16, 18, 20, 23, 25, 29, 31};

// handles one cycle of PWM (dcount = 0..31)
void displayPWMCycle(int dcount) {
  // display cycle
  unsigned long bank = 0;
  int current;
  for (int i=0; i<nLED; i++) {
    current = LED_GET_CURRENT_LEVEL(LEDs[i]);
    if (current && dcount <= LVL2PWM[current])
      bank = bank | 1 << i;
  }
  writeBank(bank);
}

// write LED values out to the shift regs
void writeBank(unsigned long bank) {
  SPI.transfer16(bank >> 16);
  SPI.transfer16(bank & 0xFFFF);
  delayMicroseconds(1);
  digitalWrite(pinRCK, HIGH);
  delayMicroseconds(1);
  digitalWrite(pinRCK, LOW);
}

