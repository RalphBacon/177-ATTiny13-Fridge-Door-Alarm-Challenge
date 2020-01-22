/* Fridge Door Alarm for the ATTiny13[A]
 *
 * Functionality:
 *
 * Door opens, door open chime.
 * After 60 seconds, door-open warning beep.
 * If door still open, warning beeps every 30 seconds.
 * After 5 minutes, continous alarm signal.
 * Door closes, door close chime.
 * If Silent Running pressed, no door chimes for ONE operation.
 *
 * Using MiniCore (MCUDude / Nerd Ralph) 1.05
 *
 * Ralph Bacon January 2020
 */

/*
 * ATTiny13A pins used as follows
 *
 *  -  pin 8 VCC
 *  -  pin 4 GND
 * PB5 pin 1 Reset
 * PB0 pin 5 Buzzer
 * PB1 pin 6 Tx Serial Output
 * PB2 pin 7 door contact
 * PB3 pin 2 Touch Sensor
 * PB4 pin 3 Silent Running LED
 *
 */

// Makes digitalWrite/Read smaller (but less safe. eg PWM)
#ifdef SAFEMODE
#undef SAFEMODE
#warning SAFEMODE undefined here
#endif

// Allows some limited debugging statements
#define ISDEBUG 1

#include "Arduino.h"
#include <avr/io.h>
//#include <CapacitiveSensor.h>
#include <avr/wdt.h>

//#ifdef ISDEBUG
//#include <BasicSerial.h>
//#endif

// Prevents the expectation of a fixed value delay
// #define __DELAY_BACKWARD_COMPATIBLE__
#include <util/delay.h>

// Silent running - no door open/close beeps
bool noNoise = false;
#define BUZZER_PIN PB0
#define TOUCH_PAD 3

// Inintially (on power up) we assume door is closed
bool doorIsOpen = false;
bool doorWasClosed = true;

// The opposite of above but easier to read
#define doorIsClosed !doorIsOpen
#define doorWasOpen !doorWasClosed

// Door reed switch on interrupt capable pin
#define doorRelayPin PB2
#define LEDpin PB4

// General Serial Print class (type independent) for debugging
//#ifdef ISDEBUG
//template<typename T>
//void debugPrint(T printMe) {
//  Serial.println(printMe);
//}
//#endif

//Forward declarations
void doorStatus();
void doBeep(uint16_t duration);
static void tone(uint8_t note);
static void killTone();
void doorOpen();
void doorClose();
void readTouchPad();

// -------------------------------------------------------------
// SETUP    SETUP    SETUP    SETUP    SETUP
// -------------------------------------------------------------
int main(void) {
  // Because we might be starting here due to WD TimeOut
  // reset and disable the WDT
  wdt_reset();
  MCUSR = 0;
  wdt_disable();
#ifdef ISDEBUG
  Serial.println(F("Go"));
#endif

  // set Data Direction Register
  //pinMode(BUZZER_PIN, OUTPUT);
  DDRB |= _BV(BUZZER_PIN);

  // Set Buzzer Pin PB0 LOW
  PORTB = 0b00000000; // all pins LOW

  //digitalWrite(BUZZER_PIN, LOW);
  PORTB = ~_BV(BUZZER_PIN);

  // LED pin
  DDRB |= _BV(LEDpin); // OUTPUT
  PORTB &= ~_BV(LEDpin); // LOW

  // Touch Pad INPUT PULLUP
  pinMode(TOUCH_PAD, INPUT_PULLUP);
  DDRB &= ~_BV(TOUCH_PAD); //INPUT...
  PORTB |= _BV(TOUCH_PAD); //   ...PULLUP

  // Door relay must be held high (brought to ground when opened)
  // pinMode(doorRelayPin, INPUT_PULLUP);
  DDRB &= ~_BV(doorRelayPin); // Set pin as input
  PORTB |= _BV(doorRelayPin); // Enable pullup resistors

  //Timer/Counter Control Register TCCR0A
  TCCR0A |= (1 << WGM01); // set timer mode to Fast PWM
  TCCR0A |= (1 << COM0A0); // connect PWM pin to Channel A of Timer0

  // Set up the door interrupt, on pin change (takes 250 bytes)
  //attachInterrupt(digitalPinToInterrupt(doorRelayPin), getDoorState, CHANGE);

  // All OK
  doorOpen();

  // Start the WatchDog Timer WDT for 8 second interval,
  // The WDT uses a separate, internal 128KHz oscillator
  // and will give an interrupt or reset the system. An
  // interrupt is usually used to wake from sleep mode, but
  // we want a system reset here.
  wdt_enable(WDTO_8S); // Note: this is all assembler code

  // For reset (not interrupt) clear bit 6, enable bit 3
  WDTCR |= ~_BV(WDTIE) | _BV(WDE);

// -------------------------------------------------------------
// LOOP     LOOP     LOOP     LOOP     LOOP
// -------------------------------------------------------------
  while (1) {
    wdt_reset();

    // Check door
    doorStatus();

    // Silent running?
    readTouchPad();

    // When the door opens (ISR triggered) things happen. Until hten, nothing.
    if (doorIsOpen && doorWasClosed) {
#ifdef ISDEBUG
      Serial.println("Open");
#endif
      // Reset this flag ready for door to be closed
      doorWasClosed = false;

      // Sound door opening chime
      doorOpen();

      // Beep every X seconds (eg 30 seconds)
      // If door is closed during this time, exit.
      unsigned char beepCnt = 0;
      for (auto cnt = 1; cnt < 300; cnt++) {
        wdt_reset();

        // Break out of this loop if door is now shut
        doorStatus();
        if (doorIsClosed)
          break;

        // Every X seconds beep a warning
        // first warn is silent allowing 60 seconds initially
        // before beep then at 30 secs thereafter
        if (cnt % 30 == 0) {
          Serial.println("Warn");

          for (uint8_t cnt2 = 0; cnt2 < beepCnt; cnt2++) {
            doBeep(300);
            _delay_ms(50);
          }

          // Exit if door now closed
          doorStatus();
          if (doorIsClosed)
            break;

          // Increment number of beeps per warning
          beepCnt++;
        }

        // Delay for 1 second but break out as soon as door is closed
        for (uint8_t cnt = 0; cnt < 10; cnt++) {
          _delay_ms(100); // Saves 2 bytes
          doorStatus();
          if (doorIsClosed)
            break;
        }
      }

      // We're still here! Door is still open! Alarm! Alarm!
      while (doorIsOpen) {
#ifdef ISDEBUG
        Serial.println("Alarm");
#endif
        doBeep(500);
        _delay_ms(250);
        doorStatus();
      }
    }

    // When the door closes do some different stuff
    if (doorIsClosed && doorWasOpen) {
#ifdef ISDEBUG
      Serial.println("Shut");
#endif
      // Reset this flag ready for door to be closed
      doorWasClosed = true;

      // Sound door closing chime
      doorClose();

      // Turn off Silent Door LED
      PORTB &= ~_BV(LEDpin);
      // digitalWrite(ledPin, LOW);

      // Reset everything ready for the next door opening event
      doorIsOpen = false;
      noNoise = false;
    }
  }
}

// Get touch signal if we haven't already touched
void readTouchPad() {

  // stop all tones
  killTone();

  if (noNoise == false) {
    if (digitalRead(TOUCH_PAD) == LOW) {
#ifdef ISDEBUG
      Serial.println("Touch");
#endif
      // Set the flag so that no door beeps happen
      noNoise = true;

      // Light the LED
      //digitalWrite(pinLED, HIGH);
      PORTB |= _BV(LEDpin);
    }
  }
}

// Is door open or closed?
void
doorStatus()
{
  // As we poll the door status often, reset WDT here
  wdt_reset();

  //return (digitalRead(doorRelayPin));
  uint8_t doorStatus = !!(PINB & _BV(doorRelayPin));
  doorIsOpen = doorStatus ? true : false;
}

// Single reminder beep
void doBeep(uint16_t duration) {
  tone(23);
  _delay_ms(duration);
  killTone();
}

// Door Open Sound
void doorOpen() {
// Door opening sound
  if (!noNoise) {
    for (uint8_t j = 24; j > 17; j--) {
      tone(j);
      _delay_ms(30);
    }
  }
  killTone();
}

// Door Close sound
void doorClose() {
// Door closing sound
  if (!noNoise) {
    for (uint8_t j = 17; j < 25; j++) {
      tone(j);
      _delay_ms(30);
    }
  }
  killTone();
}

// simple PWM tone generator - thanks to Lucasz
static void tone(uint8_t note) {
// Fixed octave
  TCCR0B = (TCCR0B & ~((1 << CS02) | (1 << CS01) | (1 << CS00))) | _BV(CS01);

// Use notes from 24 to 18 (highest)
  OCR0A = note - 1;
}

// Stop any sound
static void killTone(void) {
  TCCR0B &= ~((1 << CS02) | (1 << CS01) | (1 << CS00)); // stop the timer
}
