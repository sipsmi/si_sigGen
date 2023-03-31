// $URL: https://svn.askitsdone.net/svn/arduino/sketchbook/rfgen/rfgen_main/rfgen_main.ino $
// $Id: rfgen_main.ino 1354 2021-03-07 17:31:23Z smiths789 $

// includes -----------------------------------------------------
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
// modified library works better than eotherkit
#include <RWE_si5351.h>

// defines ------------------------------------------------------
#define SWITCHPIN  5
#define RUNMODE 1
#define STEPMODE 2
#define MEMMODE  3
#define MAXMODE 3
#define MINMODE 1

// encoder variables
static int pinA = 2; // first interrupt pin is  2
static int pinB = 3; // second interrupt pin is  3
volatile byte aFlag = 0; // rising edge on pinA - arrived at a detent
volatile byte bFlag = 0; // rising edge on pinB - arrived at a detent (opposite direction to aFlag is set)
volatile byte encoderPos = 0;
volatile byte oldEncPos = 0;
volatile byte reading = 0; // direct values read from interrupt pins before checking
volatile boolean isIncrement = false;

// intialise libraries - classes
Si5351 si5351;
LiquidCrystal_I2C lcd(0x27,16,2);

// global variables
char freqStr[16];
char dstr[32];
volatile unsigned long frequency = 10000000UL;
static unsigned long minFreq =   10000UL;
static unsigned long maxFreq =   160000000UL;
static unsigned long freqStart = 10000000UL;
static unsigned long oldFreq =   10200000UL;
static boolean doDebug = true;

//
// steps and stepping
//
volatile unsigned long stepl = 1000000UL; // start step at 1MHz
unsigned long mySteps[] = {1UL,10Ul,100Ul,1000UL,5000UL,6250UL,10000UL,12500UL,25000UL,50000UL,100000UL,1000000UL,10000000UL};
static int stepIndex = 10;

//
// memories
//
int memIndex = 0;
unsigned long myMemory[] = {27781250UL,29600000UL,145500000UL,14122000UL,10000000UL,10240000UL,20335000UL };
char* myMemoryStr[] = { "UKCB CH19","10M FMCALL","VHF S20","20M TST","Ref10Mhz","Xtl_10.24","CB 20.335" };
int memMax = 6;

//
// Switch state
//
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 100;    // the debounce time; increase if the output flickers
int buttonState;             // the current reading from the input pin
int lastButtonState = HIGH;   // the previous reading from the input pin
int mode = RUNMODE;
boolean isStepUp = true;

/**************************************/
/* Setup                              */
/**************************************/
void setup()   {

  Serial.begin(9600);

  pinMode(pinA, INPUT_PULLUP);
  pinMode(pinB, INPUT_PULLUP);
  pinMode(SWITCHPIN, INPUT_PULLUP);
  attachInterrupt(0,PinA,RISING);
  attachInterrupt(1,PinB,RISING);

//  display.begin(SH1106_SWITCHCAPVCC, SCREEN_ADDRESS);
  si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0);si5351.set_correction(76190);
  si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_2MA);
  si5351.output_enable(SI5351_CLK1, 0);
  si5351.output_enable(SI5351_CLK2, 0);
  si5351.update_status();

  // init frequency
  frequency = freqStart ;
  dtostrf(frequency/1000000.0,10,6,freqStr);
  dispDebug(freqStr,doDebug);
  setFreq(  );
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);lcd.print(freqStr);lcd.print("MHz");
  display_mode();
  display_step();
}


/**************************************/
/* Debug */
/**************************************/
void dispDebug(String msg,boolean doDebug)
{
    if ( doDebug ) {   Serial.println(msg);    }
}

/**************************************/
/* Main Loop */
/**************************************/
void loop() {

  if ( checkSwitch() ) // use switch to change run mode
  {
     // incrementStep();
     incrementMode();
  }

  if(oldEncPos != encoderPos) { // only if encoder has moved
    Serial.println(encoderPos);
    oldEncPos = encoderPos;     // reset change detection

    if ( mode == RUNMODE ) {  // Change frequency mode
		dispDebug( freqStr ,doDebug);
		if ( isIncrement )
		{
			if ( (frequency + stepl) < maxFreq )
			frequency += stepl;
			dispDebug("Step +ve " + (String)  +stepl , doDebug);
		}
		else
		{
			if ( ((frequency - stepl) > minFreq ) && ( stepl < frequency)  )
			frequency -= stepl;
			dispDebug("Step -ve " + (String) stepl , doDebug);
		}
       setFreq(  );
    }
    else if ( mode == STEPMODE )  // change step mode
    {
        isStepUp = ( isIncrement ) ? true : false;
        incrementStep(  );

	}
	else if ( mode == MEMMODE )   // select memory mode
	{
		// select mem - recall when called back
        isStepUp = ( isIncrement ) ? true : false;
        incrementMemory(  );

	}

	}

  delay(1);
}


/**************************************/
/* Set the frequency and update display */
/**************************************/
void setFreq (  )
{
	if ( oldFreq == frequency ) { return; }
	oldFreq = frequency;
	si5351.set_freq( frequency * 100ULL, 0ULL, SI5351_CLK0);
	dtostrf(frequency /1000000.0,10,6,freqStr);
	dispDebug("setFreq:"+(String) frequency ,doDebug);
	lcd.setCursor(0,0);lcd.print(freqStr);lcd.print("MHz");
}

/**************************************/
/* Handle the interupt PINS on EC11 */
/**************************************/
void PinA(){
  cli(); 
  reading = PIND & 0xC; // read all eight pin values then strip away all but pinA and pinB's values
  if(reading == B00001100 && aFlag) { 
	  //check that we have both pins at detent (HIGH) and that we are expecting detent on this pin's rising edge
    encoderPos --;  isIncrement = false;
    bFlag = 0;     aFlag = 0;
  }
  else if (reading == B00000100) bFlag = 1; //signal that we're expecting pinB to signal the transition to detent from free rotation
  sei(); 
}

void PinB(){
  cli(); 
  reading = PIND & 0xC; 
  if (reading == B00001100 && bFlag) { 
   encoderPos ++;  isIncrement = true;
    bFlag = 0;    aFlag = 0;
  }
  else if (reading == B00001000) aFlag = 1;
  sei(); 
}

/**************************************/
/* Check the switch status and debounce */
/**************************************/
boolean checkSwitch()
{
  boolean retval = false;
  buttonState = digitalRead(SWITCHPIN);
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // if the button state has changed:
    if ( (buttonState ==  LOW) && (lastButtonState == HIGH ) ) {
      lastDebounceTime = millis();
      retval = true;
    }
    lastButtonState = buttonState;
  }
  return retval;
}

/**************************************/
/* Increment the step */
/**************************************/
void incrementStep()
{
	// dont do anythign if we have reached the limits
   if ( isStepUp &&  stepl > 9999990 ) { return; }
   if ( (!isStepUp) &&  stepl < 2  ) { return; }
   else
   {

       //stepl = ( isStepUp) ? stepl * 10UL : stepl / 10UL ; }
       stepIndex = ( isStepUp) ? stepIndex + 1 : stepIndex -1 ;
       stepl = mySteps[stepIndex];

   }

    dispDebug( "New Step "+(String) stepl + "Hz", doDebug);
    display_step();
}

/**************************************/
/* Increment the memory */
/**************************************/
void incrementMemory()
{
  // dont do anythign if we have reached the limits
   if ( isStepUp && memIndex == memMax ) { return; }
   if ( !isStepUp && memIndex == 0 ) { return; }
   memIndex = ( isStepUp) ? memIndex + 1 : memIndex -1 ;
    frequency = myMemory[memIndex];
    setFreq(  );
    //dispDebug( "New Mem "+(String) stepl + "Hz", doDebug);
    display_mem();
}

/**************************************/
/* Increment the mode */
/**************************************/
void incrementMode()
{
   if ( mode == MAXMODE  ) { mode = MINMODE ; }
   else
   { mode ++;  }
    dispDebug( "New Mode "+(String) mode , doDebug);
    display_mode();
}

/**************************************/
/* Displays the frequency change step */
/**************************************/
void display_step()
{
  lcd.setCursor(5, 1);
  switch ( stepl )
  {
    case 1UL:
      lcd.print("1Hz");
      break;
    case 10UL:
      lcd.print("10Hz");
      break;
    case 100UL:
      lcd.print("100Hz");
      break;
    case 1000UL:
      lcd.print("1kHz");
      break;
    case 5000UL:
      lcd.print("5kHz");
      break;
    case 6250UL:
      lcd.print("6.25kHz");
      break;
    case 10000UL:
      lcd.print("10kHz");
      break;
    case 12500UL:
      lcd.print("12.5kHz");
      break;
    case 25000UL:
      lcd.print("25kHz");
      break;
    case 50000UL:
      lcd.print("50kHz");
      break;
    case 100000UL:
      lcd.print("100kHz");
      break;
    case 1000000UL:
      //lcd.setCursor(9, 1);
      lcd.print("1MHz");
      break;
    case 10000000UL:
      lcd.print("10MHz");
      break;
  }
   lcd.print(" Step     ");
}


/**************************************/
/* Displays the curent memory */
/**************************************/
void display_mem()
{
  lcd.setCursor(5, 1);
  lcd.print( myMemoryStr[memIndex] );
   lcd.print(" Mem   ");
}


/**************************************/
/* Displays the curent mode */
/**************************************/
void display_mode()
{
  lcd.setCursor(0, 1);
  switch ( mode )
  {
    case 1:
      lcd.print("Run  ");
      break;
    case 2:
      lcd.print("Step ");
      break;
    case 3:
      lcd.print("Mem  ");
      break;
  }
}
