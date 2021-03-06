
#define ACTUATOR_ID "a0"

#define TR2_AN_SSID "TR2_AN_111222333"
#define TR2_AN_PASS "MATHI78741"

#define PI 3.1415926535897932384626433832795
#define TAU (PI * 2)

#include "Motor.h"
#include "Esp8266.h"
#include "ems22a.h"
#include "PID.h"

#define CMD_SET_MODE 0x10
#define CMD_SET_POS 0x11
#define CMD_RESET_POS 0x12
#define CMD_ROTATE 0x13
#define CMD_RETURN_STATUS 0x14
#define CMD_STOP_RELEASE 0x15
#define CMD_STOP_EMERGENCY 0x16
#define CMD_FLIP_MOTOR 0x17

#define MODE_SERVO 0x10
#define MODE_BACKDRIVE 0x11
#define MODE_ROTATE 0x12

// Pins for components, can be overwritten in EEPROM
// EEPROM integration is in case of arduino-level pin failure
// while maintaining a single code-base for actuators
int MotorP1 = 9;
int MotorP2 = 10;
int MotorP3 = 11;
int EmsP1 = 4;
int EmsP2 = 3;
int EmsP3 = 2;
int EmsTP1 = 12;
int EmsTP2 = 6;
int EmsTP3 = 5;

double pidPos, pidOut, pidGoal = 0;
double pidThreshold = 0.003;
double pidMaxSpeed = 100;
PID pid = PID(&pidPos, &pidOut, &pidGoal, 9.0, 5.0, 0.2, DIRECT);
//PID pid = PID(&pidPos, &pidOut, &pidGoal, 8.5, 8.0, 1.2, DIRECT);

HardwareSerial Serial1(PA_10, PA_9);
Esp8266 esp8266(&Serial1);
Motor motor;
Ems22a ems22a;
Ems22a ems22aT;

long lastCfgSent = 0;
uint8_t prevLap = 0;
bool flipMotorPins = false;

int mode = MODE_ROTATE;
bool stopEmergency = false;

double formatAngle (double x) {
  if (x < -TAU) {
    x += TAU;
    return formatAngle(x);
  } else if (x > TAU) {
    x -= TAU;
    return formatAngle(x);
  } else {
    return x;
  }
}

void request (uint8_t packet[16]) {
  uint8_t msgId = packet[0];
  uint8_t msgCmd = packet[3];

  if (msgCmd == CMD_SET_MODE) {
    uint8_t msgMode = packet[4];
    mode = msgMode;

    if (msgMode == MODE_SERVO) {
      pidMaxSpeed = 100;
      pidGoal = (double)getCurrentPosition();
    }
  } else if (packet[3] == CMD_SET_POS) {
    int param = packet[4] + packet[5] * 256;
    pidMaxSpeed = floor(100.0 * packet[6] / 255.0);
    double pos = param / 65535.0 * TAU;
    pidGoal = formatAngle(pos);
  } else if (packet[3] == CMD_RESET_POS) {
    ems22aT.setEqualTo(0.0);
    ems22a.setEqualTo(0.0);
    
    pidGoal = 0;
    pidOut = 0;
    pidPos = 0;
    pid.clear();

    char cfg[64];
    int fmc = 0;
    if (flipMotorPins == true) fmc = 1;
    int p = floor(ems22a.getAngleRadians() / TAU * 65535.0);
    sprintf(cfg, "%d,%d,%d,;", fmc, p, ems22aT.getOffset());
    esp8266.flagActuatorConfig = true;
    esp8266.actuatorCfg = cfg;
  } else if (packet[3] == CMD_FLIP_MOTOR) {
    int fmc = 0;
    if (flipMotorPins == true) {
      fmc = 0;
      motor = Motor(0, MotorP1, MotorP2, MotorP3);
      flipMotorPins = false;
    } else {
      fmc = 1;
      motor = Motor(0, MotorP1, MotorP3, MotorP2);
      flipMotorPins = true;
    }
    
    char cfg[64];
    int p = floor(ems22a.getAngleRadians() / TAU * 65535.0);
    sprintf(cfg, "%d,%i,%d,;", fmc, p, ems22aT.getOffset());
    esp8266.flagActuatorConfig = true;
    esp8266.actuatorCfg = cfg;
    lastCfgSent = millis();
  } else if (packet[3] == CMD_ROTATE) {
    int offsetBinary = 128;
    int motorStep = packet[4] - offsetBinary;
    int motorDuration = packet[5] + packet[6] * 256;

    if (motorDuration > 1000) {
      motorDuration = 1000;
    }

    motor.prepareCommand(motorStep, motorDuration);
  } else if (packet[3] == CMD_STOP_RELEASE) {
    stopEmergency = false;
  } else if (packet[3] == CMD_STOP_EMERGENCY) {
    stopEmergency = true;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting...");

  Serial.print("Actuator ID: ");
  Serial.println(ACTUATOR_ID);

  motor = Motor(0, MotorP1, MotorP2, MotorP3);
  motor.stop();
  delay(50);
  
  ems22a = Ems22a(0, EmsP1, 0, EmsP2, 0, EmsP3);
  ems22aT = Ems22a(0, EmsTP1, 0, EmsTP2, 0, EmsTP3);

  ems22a.setUp();
  ems22a.setMaxLap(3);

  ems22aT.setUp();
  ems22aT.setMaxLap(1);

  motor.setUp();
  
  pid.SetMode(AUTOMATIC);
  pid.SetOutputLimits(-1, 1);

  esp8266.ssid = TR2_AN_SSID;
  esp8266.pass = TR2_AN_PASS;
  //esp8266.setDebugSerial(&Serial);
  esp8266.setTimeout(600);
  esp8266.begin();
  esp8266.configure();
  
  delay(2000);
    
  esp8266.getConfig(ACTUATOR_ID);
  parseConfig(esp8266.getLastCommand());
  prevLap = ems22a.getLap();
  pidGoal = (double)ems22a.getAngleRadians();
}

void modeBackdrive() {
  int e_trq = floor(ems22aT.getAngleRadians() / TAU * ems22aT.getMaxResolution());
  if (e_trq > 510) e_trq = e_trq - 1020;
  float t_dif = e_trq * 0.0980;
  if (t_dif < 0) t_dif = e_trq * 0.0925;
  
  int m = t_dif * 100.0;

  if (m > 100) {
    m = 100;
  } else if (m < -100) {
    m = -100;
  }

  //m = m * -1.0;
  
  motor.step(m);
}

void modeServo() {
  double pos = (double)getCurrentPosition();
  pidPos = formatAngle(pos);

  // ignore if within threshold of goal -- good 'nuff
  if (abs(pidPos - pidGoal) >= pidThreshold) {
    double d = formatAngle(pidPos) - formatAngle(pidGoal);
    if (d > PI) {
      pidGoal = formatAngle(pidGoal);
      pidGoal += TAU;
    } else if (d < -PI) {
      pidPos = formatAngle(pidPos);
      pidPos += TAU;
    } else {
      pidGoal = formatAngle(pidGoal);
      pidPos = formatAngle(pidPos);
    }

    pid.Compute();
    int speed = pidOut * 100.0;
    if (speed > pidMaxSpeed) {
      speed = pidMaxSpeed;
    } else if (speed < -pidMaxSpeed) {
      speed = -pidMaxSpeed;
    }
    motor.step(speed);
  } else {
    //pid.clear();
  }
}

void modeRotate() {
  if (motor.isFlagged()) {
    motor.executePreparedCommand();
  } else {
    motor.stop();
  }
}

void parseConfig(char* cfg) {
  int bufIdx = 0;
  char buf[16];
  uint16_t packet[16];
  int packetIdx = 0;

  // no config
  if (strstr(cfg, "nc;")) {
    return;
  }

  bool hasComma = strstr(cfg, ",");
  bool hasSemiColon = strstr(cfg, ";");
  
  if (hasComma == false || hasSemiColon == false) {
    Serial.println("No response from server. Trying again in 5 sec...");
    delay(2000);
    esp8266.getConfig(ACTUATOR_ID);
    parseConfig(esp8266.getLastCommand());
    return;
  }
  
  for (int i = 0; i < strlen(cfg); i++) {
    char c = cfg[i];
    if (c != ',') {
      buf[bufIdx] = c;
      bufIdx++;
    } else {
      buf[bufIdx] = '\0';
      bufIdx = 0;
      packet[packetIdx] = atoi(buf);
      packetIdx++;
    }
  }

  if (packet[0] == 1) {
    flipMotorPins = true;
    motor = Motor(0, MotorP1, MotorP3, MotorP2);
  } else {
    flipMotorPins = false;
    motor = Motor(0, MotorP1, MotorP2, MotorP3);
  }

  float pos = packet[1] / 65535.0 * TAU;
  ems22a.setEqualTo(pos);

  ems22aT.setOffset(packet[2]);

  Serial.print("CFG: ");
  Serial.print(packet[0]);
  Serial.print(",");
  Serial.print(packet[1]);
  Serial.print(",");
  Serial.println(ems22aT.getOffset());
  
}

void parseCommand(char* cmd) { 
  int bufIdx = 0;
  char buf[16];
  uint8_t packet[16];
  int packetIdx = 0;

  // no command
  if (strstr(cmd, "nc;") || strlen(cmd) == 0) {
    return;
  }

  for (int i = 0; i < strlen(cmd); i++) {
    char c = cmd[i];
    if (c != ',') {
      buf[bufIdx++] = c;
    } else {
      buf[bufIdx] = '\0';
      bufIdx = 0;
      packet[packetIdx++] = atoi(buf);
    }
    if (c == ';') {
      request(packet);
      packetIdx = 0;
      bufIdx = 0;
    }
  }
}

float getCurrentPosition () {
  float e_pos = ems22a.getAngleRadians();
  int e_trq = floor(ems22aT.getAngleRadians() / TAU * ems22aT.getMaxResolution());
  if (e_trq > 510) e_trq = e_trq - 1020;
  float t_dif = e_trq * 0.0980;
  if (t_dif < 0) t_dif = e_trq * 0.0925;
  return e_pos + (t_dif / 360.0 * PI * 2);
}

void loop() {
  ems22a.step();
  ems22aT.step();
  
  float pos = getCurrentPosition();
  pos = formatAngle(pos);

  if (millis() - lastCfgSent > 1000.0) {
    char cfg[64];
    int fmc = 0;
    if (flipMotorPins == true) fmc = 1;
    int p = floor(ems22a.getAngleRadians() / TAU * 65535.0);
    sprintf(cfg, "%d,%i,%d,;", fmc, p, ems22aT.getOffset());
    esp8266.flagActuatorConfig = true;
    esp8266.actuatorCfg = cfg;
    lastCfgSent = millis();
  }
  
  esp8266.step(ACTUATOR_ID, pos);
  parseCommand(esp8266.getLastCommand());
  esp8266.clearCmd();
  
  if (stopEmergency == true) {
    motor.stop();
    delay(50);
    return;
  }

  long dlr = millis() - esp8266.dataLastReceived;
  if (dlr > 3000) {
    motor.stop();
    delay(50);
    return;
  } else if (dlr > 1000) {
    motor.stop();
    delay(50);
    return;
  }

  if (mode == MODE_BACKDRIVE) {
    modeBackdrive();
  } else if (mode == MODE_SERVO) {
    modeServo();
  } else if (mode == MODE_ROTATE) {
    modeRotate();
  }

  prevLap = ems22a.getLap();
}
