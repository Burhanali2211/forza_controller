/*
  ESP32 BLE GAMEPAD - FINAL WITH X BUTTON ON GPIO18
*/

#include <Arduino.h>
#include <BleGamepad.h>

// ======================= USER OPTIONS =========================
#define DISCOVERY_MODE 1
#define DEBOUNCE_MS    25
#define PRESS_REPORT_MS 250
#define ANALOG_SMOOTH_ALPHA 0.35f

// ======================= PIN DEFINITIONS ======================
#define FIRE_BUTTON       22  // A
#define RELOAD_BUTTON     21  // B
#define AIM_BUTTON        23  // Y
#define JUMP_BUTTON       25  // jump (if you still use this, kept as-is)

#define X_BUTTON          18  // ✅ X added
#define L1_BUTTON         4   // left shoulder
#define R1_BUTTON         5   // right shoulder

#define BUZZER_PIN        19

// Joysticks
#define LEFT_VRX_JOYSTICK   35
#define LEFT_VRY_JOYSTICK   34
#define RIGHT_VRX_JOYSTICK  33
#define RIGHT_VRY_JOYSTICK  32

// ======================= BUTTON ARRAYS ========================
const int buttonsPins[] = {
  FIRE_BUTTON,    // A
  RELOAD_BUTTON,  // B
  AIM_BUTTON,     // Y
  X_BUTTON,       // ✅ X
  L1_BUTTON,      // L1
  R1_BUTTON       // R1
  // JUMP_BUTTON if still using add here
};

const char* buttonsNames[] = {
  "A_BUTTON",
  "B_BUTTON",
  "Y_BUTTON",
  "X_BUTTON",       // ✅
  "L1_BUTTON",
  "R1_BUTTON"
};

// Virtual button mapping (keep same order)
int pcMap[]      = {1,2,3,4,5,6}; 
int androidMap[] = {1,2,3,4,5,6};
int psMap[]      = {1,2,3,4,5,6};

const int NUM_BUTTONS = sizeof(buttonsPins)/sizeof(buttonsPins[0]);

// ======================= MAPPABLE PINS ========================
const uint8_t mappablePins[] = {
  22,21,27,18,4,5,
  13,14,16,17,18,19,25,26,27,4,5
};
const size_t MAPPABLE_COUNT = sizeof(mappablePins)/sizeof(mappablePins[0]);

// ======================= VARIABLES ============================
typedef enum {ANDROID, PS1, PC} GamepadMode;
uint16_t lx, ly, rx, ry;
bool wasConnected = false;
GamepadMode mode = PC;
GamepadMode lastMode = PC;

BleGamepadConfiguration gamepadConfig;
BleGamepad bleGamepad("Cristy's Gamepad", "SBxCristy");

// Debounce struct
struct Debounce {
  uint8_t pin;
  bool lastStable;
  bool lastRaw;
  uint32_t lastChangeMs;
  uint32_t lastReportMs;
};
Debounce deb[MAPPABLE_COUNT];

// Analog smoothing
struct SmoothADC { bool init=false; float v=0;};
SmoothADC sLX, sLY, sRX, sRY;

// ======================= BUZZER ================================
void buzzerBeep(int duration, int frequency = 1000){
  tone(BUZZER_PIN, frequency, duration);
  delay(duration+50);
}
void buzzerConnectionSound(){
  buzzerBeep(100,800);buzzerBeep(100,1000);buzzerBeep(200,1200);
}
void buzzerDisconnectionSound(){
  buzzerBeep(150,1000);buzzerBeep(300,600);
}
void buzzerModeChangeSound(GamepadMode m){
  switch(m){
    case ANDROID:buzzerBeep(400,900);break;
    case PS1:buzzerBeep(150,1100);buzzerBeep(150,1100);break;
    case PC:buzzerBeep(100,1300);buzzerBeep(100,1300);buzzerBeep(100,1300);break;
  }
}

// ======================= FUNCTIONS ============================
inline uint32_t nowMs(){return millis();}
uint16_t applyDeadZone(uint16_t r,bool invert){
  int16_t c=r-2048;if(abs(c)<400)return 16368;
  uint16_t m; if(c>0)m=map(r,2448,4095,16368,32737); else m=map(r,0,1648,0,16368);
  m=constrain(m,0,32737); return invert?(32737-m):m;
}

bool isActiveButtonPin(uint8_t pin,int &idxOut){
  for(int i=0;i<NUM_BUTTONS;i++) if(buttonsPins[i]==pin){idxOut=i;return true;}
  idxOut=-1;return false;
}
void printMappingEvent(uint8_t pin,bool pressed){
  int idx; bool active=isActiveButtonPin(pin,idx);
  if(active) Serial.printf("%s → GPIO %u | %s (id %d)\n",pressed?"PRESS":"RELEASE",pin,buttonsNames[idx],pcMap[idx]);
  else Serial.printf("%s → GPIO %u | unmapped\n",pressed?"PRESS":"RELEASE",pin);
}

uint16_t smoothReadADC(uint8_t pin,SmoothADC &s){
  int raw=analogRead(pin);
  if(!s.init){s.init=true;s.v=raw;} else s.v=ANALOG_SMOOTH_ALPHA*raw+(1-ANALOG_SMOOTH_ALPHA)*s.v;
  return (uint16_t)(s.v+0.5f);
}

// ======================= SETUP ================================
void setup(){
  delay(400);
  Serial.begin(115200);

  for(int i=0;i<NUM_BUTTONS;i++) pinMode(buttonsPins[i],INPUT_PULLUP);
  pinMode(BUZZER_PIN,OUTPUT);
  buzzerBeep(160,550);delay(80);buzzerBeep(160,820);

  for(size_t i=0;i<MAPPABLE_COUNT;i++){
    pinMode(mappablePins[i],INPUT_PULLUP);
    bool r=digitalRead(mappablePins[i]);
    deb[i]={mappablePins[i],r,r,nowMs(),0};
  }

  BleGamepadConfiguration cfg;
  cfg.setAutoReport(false);
  cfg.setControllerType(CONTROLLER_TYPE_GAMEPAD);
  cfg.setVid(0xe502); cfg.setPid(0xabcd);
  cfg.setHatSwitchCount(1);
  cfg.setButtonCount(16);
  bleGamepad.begin(&cfg);

  Serial.println("Gamepad Ready");
}

// ======================= LOOP ================================
void loop(){

  bool conn=bleGamepad.isConnected();
  if(conn!=wasConnected){
    if(conn){Serial.println("Connected");buzzerConnectionSound();}
    else{Serial.println("Disconnected");buzzerDisconnectionSound();}
    wasConnected=conn;
  }

  if(DISCOVERY_MODE){
    uint32_t t=nowMs();
    for(size_t i=0;i<MAPPABLE_COUNT;i++){
      bool raw=digitalRead(deb[i].pin);
      if(raw!=deb[i].lastRaw){deb[i].lastRaw=raw;deb[i].lastChangeMs=t;}
      if((t-deb[i].lastChangeMs)>=DEBOUNCE_MS && raw!=deb[i].lastStable){
        deb[i].lastStable=raw;
        if(t-deb[i].lastReportMs>=PRESS_REPORT_MS){
          deb[i].lastReportMs=t;
          printMappingEvent(deb[i].pin,raw==LOW);
        }
      }
    }
  }

  if(conn){
    static uint32_t lastChange[32]={0};
    static bool lastRaw[32]={0};
    static bool stable[32]={1};
    uint32_t t=nowMs();

    for(int i=0;i<NUM_BUTTONS;i++){
      bool r=digitalRead(buttonsPins[i]);
      if(r!=lastRaw[i]){lastRaw[i]=r;lastChange[i]=t;}
      if((t-lastChange[i])>=DEBOUNCE_MS) stable[i]=r;
    }

    uint16_t rawLX=smoothReadADC(LEFT_VRX_JOYSTICK,sLX);
    uint16_t rawLY=smoothReadADC(LEFT_VRY_JOYSTICK,sLY);
    uint16_t rawRX=smoothReadADC(RIGHT_VRX_JOYSTICK,sRX);
    uint16_t rawRY=smoothReadADC(RIGHT_VRY_JOYSTICK,sRY);

    lx=applyDeadZone(rawLX,true);
    ly=applyDeadZone(rawLY,true);
    rx=applyDeadZone(rawRX,true);
    ry=applyDeadZone(rawRY,false);

    for(int i=0;i<NUM_BUTTONS;i++){
      bool pressed=(stable[i]==LOW);
      int id=pcMap[i];
      pressed?bleGamepad.press(id):bleGamepad.release(id);
    }

    bleGamepad.setX(lx); bleGamepad.setY(ly);
    bleGamepad.setZ(rx); bleGamepad.setRZ(ry);
    bleGamepad.sendReport();
  }

  delay(15);
}
