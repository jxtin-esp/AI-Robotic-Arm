// NEXUS arm v4 firmware - Arduino Uno R3
//
// Seven logical servos. Their pins can be changed from the PC (Q command).
//   servo 0        continuous   J1 base          (default D3)
//   servo 1 + 2    continuous   J2 shoulder pair (default D4 + D5, servo 2 mirrors servo 1)
//   servo 3        continuous   J3 elbow         (default D6)
//   servo 4,5,6    positional   J4 wrist, J5 roll, J6 gripper (default D7, D8, D9)
// FSR sensors: A0 and A1.
//
// Serial protocol (115200 baud, newline terminated)
//   M,j,d[,pct]  continuous joint j = 0..2, d = 1 right / -1 left / 0 stop,
//                pct = speed 5-100 (optional, default = V value).
//                Resend while moving. A joint stops by itself after 300 ms of silence.
//   S            stop all continuous joints
//   V,pct        default speed 5-100
//   E,1 / E,0    attach+hold / detach the positional servos
//   A,k,deg      positional servo k = 0..2 target angle 0-180 (moves smoothly)
//   Q,p0,...,p6  set the pins of all 7 logical servos at once.
//                Allowed pins: 2-13 and 16-19 (A2-A5). A0, A1 are the FSRs. All pins must differ.
// Output every 100 ms:  F,a0,a1
//
// Servo POWER must come from the buck converter. Buck GND must join an Uno GND pin.

#include <Servo.h>

const uint8_t NS = 7;
uint8_t servoPin[NS] = {3, 4, 5, 6, 7, 8, 9};
Servo sv[NS];

// ---------- continuous joints
const uint8_t NC = 3;
const uint8_t CA[NC] = {0, 1, 3};      // main logical servo of each joint
const int8_t  CB[NC] = {-1, 2, -1};    // mirror servo (shoulder pair)
const int STOP_US = 1500;
const int US_PER_PCT = 4;              // 100 % = 400 us either side of stop
const unsigned long HOLD_TIMEOUT_MS = 300;

int8_t dirn[NC];
int jPct[NC];
unsigned long lastCmd[NC];
int speedPct = 50;

// ---------- positional joints
const uint8_t NP = 3;
const uint8_t PS[NP] = {4, 5, 6};      // logical servo of each positional joint
bool pEnabled = false;
int pCur[NP];
int pTgt[NP];
const unsigned long SLEW_MS = 10;
const int SLEW_DEG = 3;                // about 300 deg/s
unsigned long lastSlew = 0;

// ---------- FSR sensors
const uint8_t FSR_PIN[2] = {A0, A1};

// ---------- reporting and parsing
const unsigned long REPORT_MS = 100;
unsigned long lastReport = 0;
char buf[64];
uint8_t len = 0;

bool pinAllowed(int p) {
  return (p >= 2 && p <= 13) || (p >= 16 && p <= 19);
}

int readFsr(uint8_t pin) {
  long sum = 0;
  for (uint8_t i = 0; i < 8; i++) sum += analogRead(pin);
  return sum / 8;
}

void contWrite(uint8_t i, int us) {
  if (!sv[i].attached()) sv[i].attach(servoPin[i], 500, 2500);
  sv[i].writeMicroseconds(us);
}

void drive(uint8_t j) {
  if (dirn[j] == 0) {
    sv[CA[j]].detach();
    if (CB[j] >= 0) sv[CB[j]].detach();
    return;
  }
  int delta = dirn[j] * jPct[j] * US_PER_PCT;
  contWrite(CA[j], STOP_US + delta);
  if (CB[j] >= 0) contWrite(CB[j], STOP_US - delta);   // mirror: opposite direction
}

void stopJoint(uint8_t j) {
  dirn[j] = 0;
  drive(j);
}

void posAttach(uint8_t k) {
  uint8_t i = PS[k];
  if (!sv[i].attached()) sv[i].attach(servoPin[i], 544, 2400);
  sv[i].write(pCur[k]);   // resume from the last commanded angle
}

void setPins(char *list) {
  int p[NS];
  uint8_t cnt = 0;
  char *t = strtok(list, ",");
  while (t && cnt < NS) {
    p[cnt++] = atoi(t);
    t = strtok(NULL, ",");
  }
  if (cnt != NS) { Serial.println("ERR need 7 pins"); return; }

  for (uint8_t i = 0; i < NS; i++) {
    if (!pinAllowed(p[i])) { Serial.println("ERR bad pin"); return; }
    for (uint8_t j = i + 1; j < NS; j++) {
      if (p[i] == p[j]) { Serial.println("ERR duplicate pin"); return; }
    }
  }

  for (uint8_t j = 0; j < NC; j++) stopJoint(j);
  for (uint8_t i = 0; i < NS; i++) {
    sv[i].detach();
    servoPin[i] = p[i];
  }
  if (pEnabled) for (uint8_t k = 0; k < NP; k++) posAttach(k);
  Serial.println("OK pins set");
}

void handleLine(char *line) {
  int a = 0, b = 0, c = 0;
  int n = (strlen(line) > 2) ? sscanf(line + 2, "%d,%d,%d", &a, &b, &c) : 0;
  char cmd = line[0];

  switch (cmd) {
    case 'M': {
      if (n < 2 || a < 0 || a >= NC) break;
      int d = constrain(b, -1, 1);
      if (d == 0) { stopJoint(a); break; }
      jPct[a] = (n >= 3) ? constrain(c, 5, 100) : speedPct;
      dirn[a] = d;
      lastCmd[a] = millis();
      drive(a);
      break;
    }
    case 'S':
      for (uint8_t j = 0; j < NC; j++) stopJoint(j);
      Serial.println("OK stopped");
      break;
    case 'V':
      if (n >= 1) {
        speedPct = constrain(a, 5, 100);
        Serial.print("OK speed ");
        Serial.println(speedPct);
      }
      break;
    case 'E':
      if (n >= 1) {
        bool on = (a != 0);
        for (uint8_t k = 0; k < NP; k++) {
          if (on) posAttach(k);
          else sv[PS[k]].detach();
        }
        pEnabled = on;
        Serial.println(on ? "OK positional on" : "OK positional off");
      }
      break;
    case 'A':
      if (n >= 2 && a >= 0 && a < NP) pTgt[a] = constrain(b, 0, 180);
      break;
    case 'Q':
      if (strlen(line) > 2) setPins(line + 2);
      break;
  }
}

void updateContinuous() {
  unsigned long now = millis();
  for (uint8_t j = 0; j < NC; j++) {
    if (dirn[j] != 0 && now - lastCmd[j] > HOLD_TIMEOUT_MS) stopJoint(j);
  }
}

void updatePositional() {
  unsigned long now = millis();
  if (!pEnabled || now - lastSlew < SLEW_MS) return;
  lastSlew = now;
  for (uint8_t k = 0; k < NP; k++) {
    if (pCur[k] == pTgt[k]) continue;
    pCur[k] += constrain(pTgt[k] - pCur[k], -SLEW_DEG, SLEW_DEG);
    sv[PS[k]].write(pCur[k]);
  }
}

void setup() {
  Serial.begin(115200);
  for (uint8_t j = 0; j < NC; j++) {
    dirn[j] = 0;
    jPct[j] = speedPct;
    lastCmd[j] = 0;
  }
  for (uint8_t k = 0; k < NP; k++) {
    pCur[k] = 90;
    pTgt[k] = 90;
  }
  lastSlew = millis();
  Serial.println("READY");
}

void loop() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (len > 0) {
        buf[len] = '\0';
        handleLine(buf);
        len = 0;
      }
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = ch;
    }
  }

  updateContinuous();
  updatePositional();

  if (millis() - lastReport >= REPORT_MS) {
    lastReport = millis();
    Serial.print("F,");
    Serial.print(readFsr(FSR_PIN[0]));
    Serial.print(',');
    Serial.println(readFsr(FSR_PIN[1]));
  }
}
