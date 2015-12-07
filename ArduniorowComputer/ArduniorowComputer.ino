/* Arduino row Computer
 * Uses an UNO + LCD keypad shield like this: http://www.lightinthebox.com/16-x-2-lcd-keypad-shield-for-arduino-uno-mega-duemilanove_p340888.html?currency=GBP&litb_from=paid_adwords_shopping
 * principles behind calculations are here : http://www.atm.ox.ac.uk/rowing/physics/ergometer.html#section7
 * 13% memory used for LCD / menu
 * 41% total
 */

#include <avr/sleep.h>
#include <LiquidCrystal.h>
#define UseLCD // comment out this line to not use a 16x2 LCD keypad shield, and just do serial reporting.

// when we use esp8266... https://www.bountysource.com/issues/27619679-request-event-driven-non-blocking-wifi-api
// initialize the library with the numbers of the interface pins

//end of changes to resolution.

//code for keypad:
static int DEFAULT_KEY_PIN = 0; 
static int UPKEY_ARV = 144; //that's read "analogue read value"
static int DOWNKEY_ARV = 329;
static int LEFTKEY_ARV = 505;
static int RIGHTKEY_ARV = 0;
static int SELKEY_ARV = 721;
static int NOKEY_ARV = 1023;
static int _threshold = 50;

//KEY index definitions
#define NO_KEY 0
#define UP_KEY 3
#define DOWN_KEY 4
#define LEFT_KEY 2
#define RIGHT_KEY 5
#define SELECT_KEY 1

//session(menu) definitions
#define JUST_ROW 0
#define DISTANCE 1
#define TIME 2
#define INTERVAL 3
#define DRAGFACTOR 4
#define RPM 5
#define WATTS 6
#define SETTINGS 7
#define BACKLIGHT 8
#define ERGTYPE 9
#define BOATTYPE 10
#define WEIGHT 11
#define POWEROFF 12
#define BACK 13

#define LCDSlowDown 0
#define LCDSpeedUp 1
#define LCDJustFine 3

//erg type definitions
#define ERGTYPEVFIT 0 // V-Fit air rower.
#define ERGTYPEC2 1 //Concept 2

//boat type defintions.
#define BOAT4 0
#define BOAT8 1
#define BOAT1 2

short ergType = ERGTYPEVFIT;
short boatType = BOAT4;

long targetDistance = 2000;

long targetSeconds = 20*60;
long intervalSeconds = 60;
int numIntervals = 5;
int intervals = 0;//number of intervals we have done.

int sessionType = JUST_ROW;
//end code for keypad

//int backLight = 13;

const int switchPin = 2;                    // switch is connected to pin 6
const int analogPin = 1;                    // analog pin (Concept2)

int peakrpm = 0;

bool AnalogSwitch = false;                            // if we are connected to a concept2
float AnalogSwitchlim = 10;                             // limit to detect a rotation from C2  (0.00107421875mV per 1, so 40 = 42mV

int clicksPerRotation = 1;                  // number of magnets , or clicks detected per rotation.

unsigned long lastlimittime = 0;
float AnalogSwitchMax = 10;
float AnalogSwitchMin = 0;
int lastAnalogSwitchValue = 0;                        // the last value read from the C2
bool AnalogDropping = false;

int val;                                    // variable for reading the pin status
int buttonState;                            // variable to hold the button state
short numclickspercalc = 3;        // number of clicks to wait for before doing anything, if we need more time this will have to be reduced.
short currentrot = 0;

unsigned long utime;                        // time of tick in microseconds
unsigned long mtime;                        // time of tick in milliseconds

unsigned long laststatechangeus;            // time of last switch.
unsigned long timetakenus;                  // time taken in milliseconds for a rotation of the flywheel
unsigned long lastrotationus;               // milliseconds taken for the last rotation of the flywheel
unsigned long startTimems = 0;              // milliseconds from startup to first sample
unsigned long clicks = 0;                   // number of clicks since start
unsigned long laststrokeclicks = 0;         // number of clicks since last drive
unsigned long laststroketimems = 0;         // milliseconds from startup to last stroke drive
unsigned long strokems;                     // milliseconds from last stroke to this one
int totalStroke = 0;
unsigned long clicksInDistance = 0;         // number of clicks already accounted for in the distance.
float driveLengthm = 0;                     // last stroke length in meters
static const short numRpms = 100;
int rpmhistory[numRpms];                      // array of rpm per rotation for debugging
unsigned long microshistory[numRpms];           // array of the amount of time taken in calc/display for debugging.

short nextrpm = 0;                              // currently measured rpm, to compare to last.

float previousDriveAngularVelocity;         // fastest angular velocity at end of previous drive
float driveAngularVelocity;                 // fastest angular velocity at end of drive
float recoveryAngularVelocity;              // angular velocity at the end of the recovery  (before drive)
int diffclicks;                             // clicks from last stroke to this one

int screenstep=0;                           // int - which part of the display to draw next.
int k3 = 185;
int k2 = 185;
int k1 = 185;
float k = 0.000185;                         //  drag factor nm/s/s (displayed *10^6 on a Concept 2) nm/s/s == W/s/s
                                            //  The displayed drag factor equals the power dissipation (in Watts), at an angular velocity of the flywheel of 100 rad/sec.
                                            
float c = 2.8;                              //The figure used for c is somewhat arbitrary - selected to indicate a 'realistic' boat speed for a given output power. c/p = (v)^3 where p = power in watts, v = velocity in m/s  so v = (c/p)^1/3 v= (2.8/p)^1/3
                                            //Concept used to quote a figure c=2.8, which, for a 2:00 per 500m split (equivalent to u=500/120=4.17m/s) gives 203 Watts. 
                                            
float mPerClick = 0;                        // meters per rotation of the flywheel

unsigned long driveStartclicks;             // number of clicks at start of drive.
float mStrokePerRotation = 0;               // meters of stroke per rotation of the flywheel to work out how long we have pulled the handle in meters from clicks.

const unsigned int consecutivedecelerations = 3;//number of consecutive decelerations before we are decelerating
const unsigned int consecutiveaccelerations = 3;// number of consecutive accelerations before detecting that we are accelerating.

unsigned int decelerations = consecutivedecelerations +1;             // number of decelerations detected.
unsigned int accelerations = 0;             // number of acceleration rotations;



unsigned long driveEndms;                   // time of the end of the last drive (beginning of recovery.
unsigned long recoveryEndms;                // time of the end of the recovery
unsigned long driveBeginms;                 // time of the start of the last drive
unsigned int lastDriveTimems;               // time that the last drive took in milliseconds
float secondsdecel =0;                      // number of seconds spent decelerating.

//Stats for display
float split;                                // split time for last stroke in seconds
float power;                                // last stroke power in watts
unsigned long spm = 0;                      // current strokes per minute.  
float distancem = 0;                        // distance rowed in meters.
float RecoveryToDriveRatio = 0;                // the ratio of time taken for the whole stroke to the drive , should be roughly 3:1

//Constants that vary with machine:
float I = 0.04;                             // moment of  interia of the wheel - 0.1001 for Concept2, ~0.05 for V-Fit air rower.*/;

#ifdef UseLCD
  LiquidCrystal lcd(8, 9, 4, 5, 6, 7);
#endif

void setup() 
{
   pinMode(switchPin, INPUT_PULLUP);        // Set the switch pin as input
   Serial.begin(115200);                    // Set up serial communication at 115200bps
   buttonState = digitalRead(switchPin);    // read the initial state
   // set up the LCD's number of columns and rows: 
   #ifdef UseLCD
    lcd.begin(16, 2);  
    lcd.clear();
   #endif
  //pinMode(backLight, OUTPUT);
  //digitalWrite(backLight, HIGH); // turn backlight on. Replace 'HIGH' with 'LOW' to turn it off.
  analogReference(DEFAULT);//analogReference(INTERNAL);
  delay(100);
  if(analogRead(analogPin) == 0 & digitalRead(switchPin) ==  HIGH) 
  {//Concept 2 - set I and flag for analogRead.
    setErgType(ERGTYPEC2);
    Serial.println("Concept 2 detected on pin 3");
  }
  else
  {
    setErgType(ERGTYPEVFIT);
    Serial.println("No Concept 2 detected on Analog pin 3");
  }

  #ifdef UseLCD
    startMenu();
  #endif
  // Print a message to the LCD.
}

void setBoatType(short BoatType)
{
  boatType = BoatType;
  switch(BoatType)
  {
    case BOAT4:
      c = 2.8;
    break;
    case BOAT8:
      c = 2.8;
    break;
    case BOAT1:
      c = 2.8;
    break;
  }
}

void setErgType(short newErgType)
{
  ergType = newErgType;
  switch(newErgType)
  {
    case ERGTYPEVFIT:
        AnalogSwitch = false;
        I = 0.032;
        clicksPerRotation = 1;
        numclickspercalc = 1;
        k3 = 85;
        k2 = 85;
        k1 = 85;
        k = 0.000085;  
        mStrokePerRotation = 0;//meters of stroke per rotation of the flywheel - V-fit.
        break;
    default:
        AnalogSwitch = true;
        I = 0.101;
        //do the calculations less often to allow inaccuracies to be averaged out.
        numclickspercalc = 1;//take out a lot of noise before we detect drive / recovery.
        //number of clicks per rotation is 3 as there are three magnets.
        clicksPerRotation = 3;
        k3 = 125;
        k2 = 125;
        k1 = 125;
        k = 0.000125;
        mStrokePerRotation = 0.08;//meters of stroke per rotation of the flywheel - C2.
        ergType = ERGTYPEC2;    
        break;
  }
  mPerClick = pow((k/c),(0.33333333333333333))*2*PI/clicksPerRotation;
}

void loop()
{
  mtime = millis();
  utime = micros(); 
  if(AnalogSwitch)
  {
    //simulate a reed switch from the coil
    int analog = analogRead(analogPin);
    val = HIGH;
    if(!AnalogDropping)
    {
      if(analog < lastAnalogSwitchValue && (utime- laststatechangeus) >5000)
      {//we are starting to drop - mark the value as low, and analog as dropping.
        val = LOW;
        AnalogDropping = true;
      }
    }
    if(analog== 0) AnalogDropping = false;//we have reached 0 - reset analog dropping so we can monitor for it once analog starts to drop.
    lastAnalogSwitchValue = analog;
  }
  else
  {
    val = digitalRead(switchPin);            // read input value and store it in val                       
  }
       if (val != buttonState && val == LOW && (utime- laststatechangeus) >5000)            // the button state has changed!
          { 
            currentrot ++;
            clicks++;
            if(currentrot >= numclickspercalc)
            {
              currentrot = 0;
              //initialise the start time
              if(startTimems == 0) 
              {
                startTimems = mtime;
                 #ifdef UseLCD
                    lcd.clear();
                 #endif
              }
              timetakenus = utime - laststatechangeus;
              rpmhistory[nextrpm] = (60000000.0*numclickspercalc/clicksPerRotation)/timetakenus;
              nextrpm ++;
              const int rpmarraycount = 3;
              int rpms[rpmarraycount] = {getRpm(0), getRpm(-1), getRpm(-2)};//,getRpm(-3),getRpm(-4)};
              if(nextrpm >=numRpms) nextrpm = 0;//wrap around to the start again.
              int currentmedianrpm = median(rpms,rpmarraycount);
              int rpms2[rpmarraycount] = {getRpm(-3), getRpm(-4),getRpm(-5)};//,getRpm(-8),getRpm(-9)};
              int previousmedianrpm = median(rpms2, rpmarraycount);
              if(currentmedianrpm > peakrpm) peakrpm = currentmedianrpm;
              float radSec = (float)currentmedianrpm/60*2*PI;
              float prevradSec = (float)previousmedianrpm/60*2*PI;
              float angulardeceleration = (prevradSec-radSec)/((float)timetakenus/1000000.0);
              //Serial.println(nextinstantaneousrpm);
              if(radSec > prevradSec || (accelerations > consecutiveaccelerations && radSec == prevradSec))//faster, or previously going faster and the same rpm
                { //lcd.print("Acc");        
                  //on first acceleration - work out the total time decelerating.
                  if(accelerations == 0 && decelerations > consecutivedecelerations) 
                    {//first acceleration - capture the seconds decelerating and #
                      float singleaccelrotationms = 1000.0/(driveAngularVelocity/(2.0*PI));  
                      float singledecelrotationms = 1000.0/(radSec/(2.0*PI));
                      //time for a single rotation at drive (which we will have included in secondsdecel but shouldn't have.
                      secondsdecel = (float)((float)mtime- driveEndms - singleaccelrotationms -singledecelrotationms)/1000;
                    }
                  accelerations ++;
                  if(accelerations == consecutiveaccelerations && decelerations > consecutivedecelerations)
                    {//beginning of drive /end recovery - we have been consistently decelerating and are now consistently accelerating
                      totalStroke++;
//                      if(totalStroke >9)
//                      {
//                        dumprpms();
//                      }
                      Serial.println("\n");
                      Serial.print("Total strokes:");
                      Serial.print(totalStroke);
                      Serial.print("\tSecondsDecelerating:\t");
                      //the number of seconds to add to deceleration which we missed as we were waiting for consecutive accelerations before we detected it.
                      driveBeginms = mtime;
                      Serial.println(float(secondsdecel));
                      float nextk = I * ((1.0/recoveryAngularVelocity)-(1.0/driveAngularVelocity))/(secondsdecel)*1000000;
                      driveAngularVelocity = radSec;
                      if(nextk > 0 && nextk < 300)
                      {//if drag factor detected is positive and reasonable
                        k3 = k2; 
                        k2 = k1;
                        k1 = nextk;  //nm/s/s == W/s/s
                        int karr[3] = {k1,k2,k3};
                        k = (float)median(karr,3)/1000000;  //adjust k by half of the difference from the last k
//                        Serial.print("k:"); Serial.println(nextk);
//                        Serial.print("recw:"); Serial.println(recoveryAngularVelocity);
//                        Serial.print("dw"); Serial.println(driveAngularVelocity);
//                        Serial.print("sdecel"); Serial.println(secondsdecel);
                        mPerClick = pow((k/c),(0.33333333333333333))*2*PI/clicksPerRotation;//v= (2.8/p)^1/3  
                      }
                      else
                      {
                        //Serial.print("k:");
                        //Serial.print(nextk);
                        //Serial.print("recoveryrad");
                        //Serial.print(recoveryAngularVelocity);
                        //Serial.print("driverad");
                        //Serial.print(driveAngularVelocity);
                        //Serial.print("recoverySeconds");
                        //Serial.print(secondsdecel);
                      }
                      decelerations = 0;
                      driveStartclicks = clicks;
                      //safest time to write a screen or to serial (slowest rpm)
                    }
                    else if(accelerations > consecutiveaccelerations)
                    {
                      if(radSec > driveAngularVelocity) driveAngularVelocity = radSec;
                      driveEndms = mtime;
                      lastDriveTimems = driveEndms - driveBeginms;
                      //recovery is the stroke minus the drive, drive is just drive
                      RecoveryToDriveRatio = (strokems-lastDriveTimems) / lastDriveTimems;
                      //driveAngularVelocity = radSec;//and start monitoring the next drive (make the drive angular velocity low,
                      decelerations = 0;
                    }
                }
                else
                {
                    decelerations ++;
                    if(decelerations == consecutivedecelerations)
                    {//still decelerating (more than three decelerations in a row).
                      previousDriveAngularVelocity = driveAngularVelocity;    //store the previous deceleration
                      diffclicks = clicks - laststrokeclicks;
                      strokems = mtime - laststroketimems;
                      spm = 60000 /strokems;
                      laststrokeclicks = clicks;
                      laststroketimems = mtime;
                      split =  ((float)strokems)/((float)diffclicks*mPerClick*2) ;//time for stroke /1000 for ms *500 for 500m = /(*2)
                      power = 2.8 / pow((split / 500),3.0);//watts = 2.8/(split/500)³ (from concept2 site)
                      //Serial.print(split);
                      //store the drive speed
                      accelerations = 0;//reset accelerations counter
                    }
                    else if(decelerations > consecutivedecelerations)
                    {
                      driveLengthm = (float)(clicks - driveStartclicks) * mStrokePerRotation;
                      accelerations = 0;//reset accelerations counter
                      recoveryAngularVelocity=radSec;
                      recoveryEndms = mtime;
                    }
                }
                
                if(mPerClick <= 20)
                {
                  distancem += (clicks-clicksInDistance)*mPerClick;
                  clicksInDistance = clicks;
                }
                //if we are spinning slower than 10ms per spin then write the next screen
                if(timetakenus > 10000) writeNextScreen();
                lastrotationus = timetakenus;
                //watch out for integer math problems here
                //Serial.println((nextinstantaneousrpm - instantaneousrpm)/timetakenms); 
            } 
            laststatechangeus=utime;
          }

          if((millis()-mtime) >=6)
          {
            Serial.print("warning - loop took (ms):");
            Serial.println(millis()-mtime);
            Serial.print("screen:");
            Serial.println(screenstep);
          }
          if((mtime-startTimems)/1000 > targetSeconds)
          {
            switch(sessionType)
            {
              case INTERVAL:
                if(intervals <= numIntervals)
                {
                  showInterval(intervalSeconds); 
                  //then reset the start time for count down to now for the next interval.
                  startTimems = millis();
                }
                else
                {//stop.
                  Serial.println("Done");
                  while(true);
                }
                
                intervals ++;
                break;
              case TIME:
                Serial.println("Done");
                while(true);
                break;
              default:
              //default = = do nothing.
              break;
            }
          }
//          microshistory[nextrpm] = micros()-utime;
  buttonState = val;                       // save the new state in our variable
}

//take time and display how long remains on the screen.
void showInterval(long numSeconds)
{
  Serial.print("Interval ");
  Serial.println(intervals);
  long startTime = millis()/1000;
  long currentTime = millis()/1000;
  while(startTime + numSeconds > currentTime)
  {
    delay(200);
    currentTime = millis()/1000;
    writeTimeLeft(startTime+numSeconds-currentTime);
  }
}

void writeTimeLeft(long totalSeconds)
{
  #ifdef UseLCD
    lcd.clear();
    lcd.print("Interval ");
    lcd.print(intervals);
    lcd.setCursor(0,1);
    int minutes = totalSeconds/60;
    if(minutes <10) lcd.print ("0");
    lcd.print(minutes);
    int seconds = totalSeconds - (minutes*60);
  #endif
}

void writeNextScreen()
{
    #ifdef UseLCD
        if(sessionType == DRAGFACTOR)
        {
          lcd.setCursor(0,1);
          lcd.print(k*1000000,0);
          lcd.print("  ");
          return;//no need for other screen stuff.
        }else if (sessionType == RPM)
        {
          lcd.clear();
          lcd.print("R");
          lcd.print(getRpm(0));
          lcd.setCursor(0,1);
          lcd.print("M:");
          int rpms2[3] = {getRpm(0), getRpm(-1), getRpm(-2)};
          lcd.print(median(rpms2,5));
          int x = getKey();
          if(x == SELECT_KEY)
          {
            dumprpms();
          }
          return;//no need for other screen stuff.
        }
        else if(sessionType == WATTS)
        {
          lcd.clear();
          lcd.print(power);
        }
     #endif
  //Display Format:
  // 2:16  r3.1SPM:35
  //5000m     10:00
  int timemins, timeseconds, splitmin, splits, dm;
  screenstep++;
  switch(screenstep)
  {//only write a little bit to the screen to save time.
    case 0:
    //lcd.clear();
    #ifdef UseLCD
      lcd.setCursor(0,0);
    #endif
    break;
    case 1:
    {
      splitmin = (int)(split/60);
      int splits = (int)(((split-splitmin*60)));
        if(splitmin < 10)
        {//only display the split if it is less than 10 mins per 500m
          #ifdef UseLCD
            //lcd.print("S");
            lcd.print(splitmin);//minutes in split.
            lcd.print(":");
            if(splits <10) lcd.print("0");
            lcd.print(splits);//seconds
            //lcd 0->5, 0  used
          #endif
        }
        Serial.print("\tSplit:\t");
        Serial.print(splitmin);
        Serial.print(":");
        Serial.print(splits);
    }
    break;
    case 2:
      //lcd 10->16,0 used
      #ifdef UseLCD
        lcd.setCursor(10,0);
        lcd.print("S:");
        lcd.print(spm);
        lcd.print("  ");
      #endif
    Serial.print("\tSPM:\t");
    Serial.print(spm);
    break;
    case 3:
      #ifdef UseLCD
       lcd.setCursor(0,1);
       //Distance in meters:
      if(sessionType == DISTANCE)
      {
        long distanceleft = targetDistance - distancem;
        if(distanceleft <1000) lcd.print("0");
        if(distanceleft <100) lcd.print("0");
        if(distanceleft <10) lcd.print("0");
        lcd.print((int)distanceleft);
        lcd.print("m");
      }
      else
      {
        if(distancem <1000) lcd.print("0");
        if(distancem <100) lcd.print("0");
        if(distancem <10) lcd.print("0");
        lcd.print((int)distancem);
        lcd.print("m");
      }
      #endif
      Serial.print("\tDistance:\t");
      Serial.print(distancem);
      Serial.print("m");
      
      //lcd 0->5, 1 used
      //Drag factor
      /*lcd.print("D:");
      lcd.println(k*1000000);*/
    break;
    case 4:
      //lcd 11->16 used
      
      #ifdef UseLCD
        lcd.setCursor(11,1);
        if(sessionType == TIME)
        {//count-down from the target time.
          timemins = (targetSeconds - (mtime-startTimems)/1000)/60;
          if(timemins < 0) timemins = 0;
          timeseconds = (targetSeconds - (mtime-startTimems)/1000) - timemins * 60 ;
          if(timeseconds < 0) timeseconds = 0;
        }
        else
        {
          timemins = (mtime-startTimems)/60000;
          timeseconds = (long)((mtime)-startTimems)/1000 - timemins*60;
        }
          if(timemins <10) lcd.print("0");
          lcd.print(timemins);//total mins
          lcd.print(":");
          if(timeseconds < 10) lcd.print("0");
          lcd.print(timeseconds);//total seconds.*/
      #endif
        Serial.print("\tTime:\t");
        Serial.print(timemins);
        Serial.print(":");
        Serial.print(timeseconds);
        //lcd.print(k);
        //Serial.println(k);
//      lcd.print("s");
//      lcd.print(diffclicks);
      break;
    case 5://next lime
    #ifdef UseLCD
      //lcd 6->9 , 0
       lcd.setCursor(6,0);
       if(RecoveryToDriveRatio > 2.1)
       {
        lcd.print(LCDSpeedUp);
       }
       else if (RecoveryToDriveRatio < 1.9)
        {
          lcd.print(LCDSlowDown);
        }
        else
        {
          lcd.print(LCDJustFine);
        }       
       //lcd.print(RecoveryToDriveRatio,1);
    #endif
       Serial.print("\tDrag factor:\t");
       Serial.print(k*1000000);
       //Serial.print("anMin\t");
       //Serial.println(AnalogSwitchMin);
       //Serial.print("anMax\t");
       //Serial.println(AnalogSwitchMax);
      //lcd.setCursor(10,1);
      //lcd.print(" AvS:");
      //lcd.print(clicks/(millis()-startTime));     
      break;
   case 6:
    //lcd 6->9 , 0
      // lcd.setCursor(4,0);
      // lcd.print("r");
      // lcd.print(RecoveryToDriveRatio);

      Serial.print("\tDrive angularve: ");
      Serial.print(driveAngularVelocity);

      Serial.print("\tRPM:\t");
      {
        int rpms1[5] = {getRpm(0), getRpm(-1), getRpm(-2),getRpm(-3),getRpm(-4)};
        Serial.print(median(rpms1,5));
      }
      Serial.print("\tPeakrpm:\t");
      Serial.println(peakrpm);
      //lcd.setCursor(10,1);
      //lcd.print(" AvS:");
      //lcd.print(clicks/(millis()-startTime));   
    break;

    default:
      screenstep = -1;//will clear next time, as 1 is added and if 0 will be cleared.
  }    
}

void dumprpms()
{
    Serial.println("Rpm dump");
    for(int i = 0; i < 190; i++)
    {
      Serial.println(rpmhistory[i]);
    }
    //nextrpm = 0;
}

//Current selection (Distance, Time)
#ifdef UseLCD
void startMenu()
{
  menuType();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Begin Rowing");
}

void menuType()
{
  int c = NO_KEY;
  while(getKey() == SELECT_KEY) delay(300);
  writeType();
  while(c != SELECT_KEY)
  {
    c = getKey();
    if(c == DOWN_KEY)
    {
      sessionType ++;
      writeType();
      delay(500);
    }
    else if (c == UP_KEY)
    {
      sessionType --;
      writeType();
      delay(500);
    }
  }
  while(c == SELECT_KEY) c = getKey();//wait until select is unpressed.
  delay(200);
  switch(sessionType)
  {
    case DISTANCE:
      menuSelectDistance();
      break;
    case TIME: 
      targetSeconds = menuSelectTime(targetSeconds);
      break;
    case INTERVAL:
     targetSeconds = menuSelectTime(targetSeconds);
     lcd.clear();
     lcd.setCursor(0,0);
     lcd.print("Rest");
     intervalSeconds = menuSelectTime(intervalSeconds);
     lcd.clear();
     lcd.setCursor(0,0);
     lcd.print("Repeats");
     numIntervals = menuSelectNumber(numIntervals);
      break;
    case SETTINGS:
      menuSettings();    
      menuType();  
    default:
      //just row with that as a setting (rpm / drag factor etc..)
    break;
      
  }
}

long menuSelectNumber(long initialValue)
{
  int c = NO_KEY;
  while(getKey() == SELECT_KEY) delay(300);
  c = getKey();
  printNumber(initialValue);
  while(c != SELECT_KEY)
  {
    c = getKey();
      if(c == DOWN_KEY)
    {
      initialValue--;
      printNumber(initialValue);
      delay(500);
    }
    else if (c == UP_KEY)
    {
      initialValue ++;
      printNumber(initialValue);
      delay(500);
    }
  }
  return initialValue;
}

void printNumber(long num)
{
  lcd.setCursor(0,1);
  lcd.print(num);
  lcd.print(" ");
}

void menuSettings()
{
  int c = NO_KEY;
  while(getKey() == SELECT_KEY) delay(300);
  writeSettingsMenu();
  while(c != SELECT_KEY)
  {
    c = getKey();
    if(c == DOWN_KEY)
    {
      sessionType ++;
      writeSettingsMenu();
      delay(500);
    }
    else if (c == UP_KEY)
    {
      sessionType --;
      writeSettingsMenu();
      delay(500);
    }
  }
  while(c == SELECT_KEY) c = getKey();//wait until select is unpressed.
  delay(200);
  switch(sessionType)
  {
    case BACKLIGHT:
      menuSelectBacklight();
      menuSettings();
      break;
    case ERGTYPE:
      menuSelectErgType();
      menuSettings();
      break;
    case BOATTYPE:
      menuSelectBoatType();
      menuSettings();
      break;
    case POWEROFF:
      menuSleep();
      menuSettings();
      break;
    default:
      //back to other menu
    break;
      
  }
}

void menuSelectBoatType()
{
    menuDisplayBoatType();
  int c = NO_KEY;
  while(c!=SELECT_KEY)
  {
    c = getKey();
    if(c==UP_KEY) 
    {
      boatType ++;
      if(boatType > BOAT1) boatType = BOAT4;
      menuDisplayErgType();
      delay(500);
    }
    if(c==DOWN_KEY) 
    {
      boatType --;
      if(boatType < ERGTYPEVFIT) ergType = ERGTYPEC2;
      menuDisplayErgType();
      delay(500);
    }
    setBoatType(boatType);
  }
}

void menuDisplayBoatType()
{
  lcd.setCursor(0,1);
  switch(boatType)
  {
    case BOAT1:
      lcd.print("Single  ");
      break;
    case BOAT4:
      lcd.print("Four    ");
      break;
    case BOAT8:
      lcd.print("Eight   ");
      break;
  }
}

void writeSettingsMenu()
{
    lcd.clear();
    while(getKey() == SELECT_KEY) delay(300);
    if(sessionType <= SETTINGS) sessionType = BACK;
    if(sessionType > BACK) sessionType = BACKLIGHT;
    switch(sessionType)
    {
      case BACKLIGHT:
        menuDisplay("Back Light");
        break;
      case ERGTYPE:
        menuDisplay("Erg Type");
        break;
      case BOATTYPE:
        menuDisplay("Boat Type");
        break;
      case WEIGHT:
        menuDisplay("Weight");
        break;
      case POWEROFF:
        menuDisplay("Sleep");
        break;
      case BACK:
        menuDisplay("Back");
        break;
    }
}

void writeType()
{
  lcd.clear();
  //rollback around.
  if(sessionType <=-1) sessionType = SETTINGS;
  if(sessionType > SETTINGS) sessionType = JUST_ROW;
    switch(sessionType)
    {
      case DISTANCE:
        menuDisplay("Distance");
      break;
      case TIME:
        menuDisplay("Time");
      break;
      case INTERVAL:
        menuDisplay("Interval");
        break;
      case DRAGFACTOR:
        menuDisplay("Drag Factor");
        break;
      case RPM:
        menuDisplay("RPM");
        break;
      case SETTINGS:
        menuDisplay("Settings");
        break;

      case WATTS:
        menuDisplay("Watts");
        break;
      default:
        sessionType = JUST_ROW;
        menuDisplay("Just Row");
        break;
    }
}

void menuSelectErgType()
{
  menuDisplayErgType();
  int c = NO_KEY;
  while(c!=SELECT_KEY)
  {
    c = getKey();
    if(c==UP_KEY) 
    {
      ergType ++;
      if(ergType > ERGTYPEC2) ergType = ERGTYPEVFIT;
      menuDisplayErgType();
      delay(500);
    }
    if(c==DOWN_KEY) 
    {
      ergType --;
      if(ergType < ERGTYPEVFIT) ergType = ERGTYPEC2;
      menuDisplayErgType();
      delay(500);
    }
    setErgType(ergType);
  }
}

void menuDisplayErgType()
{
  lcd.setCursor(0,1);
  switch(ergType)
  {
    case ERGTYPEC2:
      lcd.print("Concept 2");
      break;
    case ERGTYPEVFIT:
      lcd.print("V-Fit    ");
      break;
  }
}

//go to sleep
void menuSleep()
{
  sleep_enable();
  digitalWrite(10,LOW);
  attachInterrupt(0, pin2_isr, LOW);
  /* 0, 1, or many lines of code here */
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  cli();
  sleep_bod_disable();
  sei();
  sleep_cpu();
  /* wake up here */
  sleep_disable();
}

//interrupt sleep routine
void pin2_isr()
{
  sleep_disable();
  detachInterrupt(0);
 // pin2_interrupt_flag = 1;
}

void menuSelectBacklight()
{
  pinMode(10,OUTPUT);
  int state = HIGH;
  digitalWrite(10,state);
  showBacklightState(state);
  int c = NO_KEY;
  while(c!=SELECT_KEY)
  {
    c = getKey();
    if(c==UP_KEY || c==DOWN_KEY)
    {
      if(state == LOW) 
      {
        state = HIGH;
      }
      else
      {
        state = LOW;
      }
      showBacklightState(state);
      digitalWrite(10,state);
      delay(500);
    }
  }
}

void showBacklightState(int state)
{
  lcd.setCursor(0,1);
  if(state == LOW)
  {
      lcd.print("Off");
  }
  else
  {
      lcd.print("On ");
  }
}

void menuSelectDistance()
{
  int c = NO_KEY;
  long increment = 1000;
  writeCurrentDistanceamount(increment);
  delay(600);
  while(c!= SELECT_KEY)
  {
    c = getKey();
    if(c==UP_KEY)
    {
      targetDistance += increment;
      if(targetDistance < 0) targetDistance = 0;
      writeCurrentDistanceamount(increment);
    }
    else if(c== DOWN_KEY)
    {
      targetDistance -= increment;
      if(targetDistance < 0) targetDistance = 0;
      writeCurrentDistanceamount(increment);
    }
    else if(c== RIGHT_KEY)
    {
      increment /=10;
      if(increment == 0) increment = 1;
      writeCurrentDistanceamount(increment);
    }
    else if(c==LEFT_KEY)
    {
      increment*=10;
      if(increment >= 10000) increment = 10000;
      writeCurrentDistanceamount(increment);
    }
  }
}

void writeCurrentDistanceamount(int increment)
{
    lcd.setCursor(0,1);
    if(targetDistance < 10000) lcd.print("0");
    if(targetDistance < 1000) lcd.print("0");
    if(targetDistance < 100) lcd.print("0");
    if(targetDistance < 10) lcd.print("0");
    lcd.print(targetDistance);
    switch(increment)
    {
       case 1:
        lcd.setCursor(4,1);
        break;
      case 10:
        lcd.setCursor(3,1);
        break;
      case 100:
        lcd.setCursor(2,1);
        break;
      case 1000:
        lcd.setCursor(1,1);
        break;
      case 10000:
        lcd.setCursor(0,1);
        break;
       default:
        lcd.setCursor(1,0);
        lcd.print(increment);
       break;
    }
    lcd.cursor();
    delay(200);
}

long menuSelectTime(long initialSeconds)
{
  int charpos = 3;
  //charpos is the current selected character on the display, 
  //0 = 10s of hours, 1 = hours, 3 = tens of minutes, 4 = minutes, 6 = tens of seconds, 7 = seconds.
  writeTargetTime(charpos, initialSeconds);
  int c = getKey();
  while(c == SELECT_KEY) 
  {
    c = getKey();
    delay(200);
  }
  while(c!= SELECT_KEY)
  {
    c = getKey();
    long incrementseconds = 0;
    switch (charpos)
    {
      case 0://10s of hours.
        incrementseconds = 36000;
        break;
      case 1://hours
        incrementseconds = 3600;
        break;
      case 3://ten mins
        incrementseconds = 600;
        break;
      case 4://mins
        incrementseconds = 60;
        break;
      case 6://ten seconds
        incrementseconds = 10;
        break;
      case 7:
        incrementseconds = 1;
        break;
      default:
        charpos = 0;
        incrementseconds= 36000;
    }
    if(c==UP_KEY)
    {
      initialSeconds += incrementseconds;
      delay(500);
      writeTargetTime(charpos, initialSeconds);
    }
    else if(c== DOWN_KEY)
    {
      initialSeconds -= incrementseconds;
      if(targetSeconds < 0) 
      {
        targetSeconds = 0;
      }
      delay(500);
      writeTargetTime(charpos, initialSeconds);
    }
    else if (c== RIGHT_KEY)
    {
      charpos ++;
      if(charpos ==2 || charpos == 5) charpos ++;
      delay(500);
      writeTargetTime(charpos, initialSeconds);
    }
    else if (c == LEFT_KEY)
    {
      charpos --;
      if(charpos ==2 || charpos == 5) charpos --;
      delay(500);
      writeTargetTime(charpos, initialSeconds);
    }
  }
  return initialSeconds;
}

void writeTargetTime(int charpos, long numSeconds)
{
  int targetHours= numSeconds /60/60;
    lcd.setCursor(0,1);
    if(targetHours < 10) lcd.print("0");
    lcd.print(targetHours);
    lcd.print(":");
    int targetMins = numSeconds /60 - (targetHours *60);
    if(targetMins < 10) lcd.print("0");
    lcd.print(targetMins);
    lcd.print(":");
    int Seconds = numSeconds - (targetMins*60) - (targetHours*60*60);;
    if(Seconds < 10) lcd.print("0");
    lcd.print(Seconds);
    lcd.setCursor(charpos,1);
    lcd.cursor();
}

int getKey()
{
  int _curInput = analogRead(0);
  //Serial.println(_curInput);
  int _curKey;
  if (_curInput > UPKEY_ARV - _threshold && _curInput < UPKEY_ARV + _threshold ) _curKey = UP_KEY;
      else if (_curInput > DOWNKEY_ARV - _threshold && _curInput < DOWNKEY_ARV + _threshold ) _curKey = DOWN_KEY;
      else if (_curInput > RIGHTKEY_ARV - _threshold && _curInput < RIGHTKEY_ARV + _threshold ) _curKey = RIGHT_KEY;
      else if (_curInput > LEFTKEY_ARV - _threshold && _curInput < LEFTKEY_ARV + _threshold ) _curKey = LEFT_KEY;
      else if (_curInput > SELKEY_ARV - _threshold && _curInput < SELKEY_ARV + _threshold ) _curKey = SELECT_KEY;
      else _curKey = NO_KEY;
  return _curKey;
  delay(100);
}

//

void menuDisplay(char* text)
{
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(text);
}

void graphics() {
  byte SlowDown[8] = {
                    B00100,
                    B00100,
                    B10101,
                    B01110,
                    B00100,
                    B00000,
                    B00000,
                    B00000
                    };
  byte SpeedUp[8] = {
                    B00100,
                    B01110,
                    B10101,
                    B00100,
                    B00100,
                    B00000,
                    B00000,
                    B00000
                    };
  byte JustFine[8] = {
                    B00000,
                    B00100,
                    B00100,
                    B11111,
                    B00100,
                    B00100,
                    B00000,
                    B00000
                    };
  lcd.createChar(LCDSlowDown, SlowDown);
  lcd.createChar(LCDSpeedUp, SpeedUp);
  lcd.createChar(LCDJustFine, JustFine);
}

#endif

int getRpm(short offset)
{
  int index = nextrpm - 1 + offset;
    while (index >= numRpms)
    {
      index -= numRpms;
    }
    while(index < 0) 
    {
      index += numRpms;
    }
  return rpmhistory[index];
}

int median(int new_array[], int num){
     //ARRANGE VALUES
    for(int x=0; x<num; x++){
         for(int y=0; y<num-1; y++){
             if(new_array[y]>new_array[y+1]){
                 int temp = new_array[y+1];
                 new_array[y+1] = new_array[y];
                 new_array[y] = temp;
             }
         }
     }
    //CALCULATE THE MEDIAN (middle number)
    if(num % 2 != 0){// is the # of elements odd?
        int temp = ((num+1)/2)-1;
        //cout << "The median is " << new_array[temp] << endl;
  return new_array[temp];
    }
    else{// then it's even! :)
        //cout << "The median is "<< new_array[(num/2)-1] << " and " << new_array[num/2] << endl;
  return ((new_array[(num/2)-1] + new_array[num/2]) / 2); 
    }
}

