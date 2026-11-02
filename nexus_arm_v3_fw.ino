// NEXUS arm v3 firmware - Arduino Uno R3
//
// Continuous joints (hold to move, no position tracking, no limits):
//   C1 = D3              MG996R
//   C2 = D4 + D5         shoulder pair, D5 always mirrors D4 (opposite direction, same speed)
//   C3 = D6              MG996R
// Positional joints (true angle, 0-180, no extra limits):
//   P1 = D7, P2 = D8, P3 = D9     MG90S
// FSR sensors:
//   FSR 1 on A0, FSR 2 on A1, raw 0-1023, averaged over 8 reads
//
// Serial protocol (115200 baud, newline terminated)
//   M,j,d     j = 0..2, d = 1 right, -1 left, 0 stop
//             Resend while the button is held. A joint stops by itself after 300 ms of silence.
//   S         stop all continuous joints
//   V,pct     speed 5-100 %
//   E,1       attach and hold the positional servos
//   E,0       detach the positional servos (they go limp)
//   A,k,deg   target angle for positional servo k = 0..2 (0-180), moves smoothly
// Output every 100 ms:  F,a0,a1
//
// Servo POWER must come from the buck converter. Buck GND must join an Uno GND pin.

#include <Servo.h>

Servo sv[10];   // indexed by pin number, only 3-9 are used

// ---------- continuous joints
const uint8_t NC = 3;
const uint8_t CA[NC] = {3, 4, 6};     // main pin of each joint
const int8_t  CB[NC] = {-1, 5, -1};   // D5 mirrors D4 on the shoulder
const int STOP_US = 1500;
const int US_PER_PCT = 4;             // 100 % = 400 us either side of stop
const unsigned long HOLD_TIMEOUT_MS = 300;

int8_t dirn[NC];
unsigned long lastCmd[NC];
int speedPct = 30;

// ---------- positional joints
const uint8_t NP = 3;
const uint8_t PP[NP] = {7, 8, 9};
bool pEnabled = false;
int pCur[NP];
int pTgt[NP];
const unsigned long SLEW_MS = 10;
const int SLEW_DEG = 3;               // about 300 deg/s
unsigned long lastSlew = 0;

// ---------- FSR sensors
const uint8_t FSR_PIN[2] = {A0, A1};

// ---------- reporting and parsing
const unsigned long REPORT_MS = 100;
unsigned long lastReport = 0;
char buf[48];
uint8_t len = 0;

int readFsr(uint8_t pin) {
  long sum = 0;
  for (uint8_t i = 0; i < 8; i++) sum += analogRead(pin);
  return sum / 8;
}

void contWrite(uint8_t pin, int us) {
  if (!sv[pin].attached()) sv[pin].attach(pin, 500, 2500);
  sv[pin].writeMicroseconds(us);
}

void drive(uint8_t j) {
  if (dirn[j] == 0) {
    sv[CA[j]].detach();
    if (CB[j] >= 0) sv[CB[j]].detach();
    return;
  }
  int delta = dirn[j] * speedPct * US_PER_PCT;
  contWrite(CA[j], STOP_US + delta);
  if (CB[j] >= 0) contWrite(CB[j], STOP_US - delta);   // mirror: opposite direction
}

void stopJoint(uint8_t j) {
  dirn[j] = 0;
  drive(j);
}

void posAttach(uint8_t k) {
  if (!sv[PP[k]].attached()) sv[PP[k]].attach(PP[k], 544, 2400);
  sv[PP[k]].write(pCur[k]);   // resume from the last commanded angle
}

void handleLine(char *line) {
  int a = 0, b = 0, c = 0;
  int n = (strlen(line) > 2) ? sscanf(line + 2, "%d,%d,%d", &a, &b, &c) : 0;

  switch (line[0]) {
    case 'M': {
      if (n < 2 || a < 0 || a >= NC) break;
      int d = constrain(b, -1, 1);
      if (d == 0) { stopJoint(a); break; }
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
        for (uint8_t j = 0; j < NC; j++) if (dirn[j] != 0) drive(j);
      }
      break;
    case 'E':
      if (n >= 1) {
        bool on = (a != 0);
        for (uint8_t k = 0; k < NP; k++) {
          if (on) posAttach(k);
          else sv[PP[k]].detach();
        }
        pEnabled = on;
        Serial.println(on ? "OK positional on" : "OK positional off");
      }
      break;
    case 'A':
      if (n >= 2 && a >= 0 && a < NP) pTgt[a] = constrain(b, 0, 180);
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
    sv[PP[k]].write(pCur[k]);
  }
}

void setup() {
  Serial.begin(115200);
  for (uint8_t j = 0; j < NC; j++) {
    dirn[j] = 0;
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
