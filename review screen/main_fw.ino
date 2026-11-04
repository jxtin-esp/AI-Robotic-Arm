
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef GFXFF
#define GFXFF 1
#endif

// ============================================================
// CONFIG - EDIT THESE
// ============================================================

const char* TXT_PRODUCT_NAME = "AI Robotic Arm";

const char* PRESENTER_NAMES[] = {
  "Jatin",
  "Sufyaan",
  "Sushant"
};

const uint8_t PRESENTER_COUNT =
  sizeof(PRESENTER_NAMES) / sizeof(PRESENTER_NAMES[0]);

const char* TXT_RATINGS_LABEL = "ratings";
const char* TXT_BTN_RATE_NOW = "RATE NOW";
const char* TXT_HINT_EMPTY = "Tap a star to rate";
const char* TXT_HINT_PICKED = "You rated it %d out of 5";
const char* TXT_BTN_SUBMIT = "SUBMIT";
const char* TXT_THANKS = "Thank you for rating!";
const char* TXT_THANKS_SUB = "You gave %d out of 5";
const char* TXT_BTN_BACK_HOME = "BACK TO HOME";
const char* TXT_CALIBRATE = "Tap each crosshair";

const uint32_t SEED_COUNTS[5] = {26, 4, 12, 38, 122};

const uint8_t SCREEN_ROTATION = 1;
const bool RESET_RATINGS_ON_BOOT = false;
const bool FORCE_RECALIBRATE = false;

// Midnight blue / cyan / gold
#define RGB_BG          13, 20, 34
#define RGB_CARD        25, 37, 56
#define RGB_TRACK       48, 63, 82
#define RGB_TEXT        239, 245, 250
#define RGB_TEXT_DIM    155, 174, 192
#define RGB_ACCENT      48, 205, 220
#define RGB_ACCENT_DEEP 22, 130, 153
#define RGB_ON_ACCENT   8, 25, 35
#define RGB_STAR_EMPTY  88, 108, 128
#define RGB_GOLD        255, 190, 78

// Layout
const int BTN_X = 20, BTN_W = 280;
const int HOME_BTN_Y = 204, HOME_BTN_H = 30;
const int LOW_BTN_Y = 196, LOW_BTN_H = 30;

const int STAR_CX[5] = {44, 102, 160, 218, 276};
const int STAR_CY = 118, STAR_ROUT = 26, STAR_RIN = 11;

// ============================================================
// END CONFIG
// ============================================================

TFT_eSPI tft = TFT_eSPI();
Preferences ratingPrefs;
Preferences calPrefs;

uint16_t colBg, colCard, colTrack, colText, colTextDim;
uint16_t colAccent, colAccentDeep, colOnAccent;
uint16_t colStarEmpty, colGold;

enum Screen {
  SCREEN_HOME,
  SCREEN_RATE,
  SCREEN_THANKS
};

Screen currentScreen = SCREEN_HOME;

uint32_t starCount[6] = {0};
int selectedRating = 0;
int lastSubmittedRating = 0;

// Forward declarations
void drawHome();
void drawRate();
void drawThanks();
void handleHomeTouch(int tx, int ty);
void handleRateTouch(int tx, int ty);
void handleThanksTouch(int tx, int ty);

// ---------------- Palette ----------------

void buildPalette() {
  colBg = tft.color565(RGB_BG);
  colCard = tft.color565(RGB_CARD);
  colTrack = tft.color565(RGB_TRACK);
  colText = tft.color565(RGB_TEXT);
  colTextDim = tft.color565(RGB_TEXT_DIM);
  colAccent = tft.color565(RGB_ACCENT);
  colAccentDeep = tft.color565(RGB_ACCENT_DEEP);
  colOnAccent = tft.color565(RGB_ON_ACCENT);
  colStarEmpty = tft.color565(RGB_STAR_EMPTY);
  colGold = tft.color565(RGB_GOLD);
}

// ---------------- Data ----------------

uint32_t totalRatings() {
  uint32_t total = 0;
  for (int i = 1; i <= 5; i++) {
    total += starCount[i];
  }
  return total;
}

float averageRating() {
  uint32_t total = totalRatings();
  if (total == 0) return 0.0f;

  uint32_t sum = 0;
  for (int i = 1; i <= 5; i++) {
    sum += (uint32_t)i * starCount[i];
  }

  return (float)sum / (float)total;
}

void saveAllCounts() {
  for (int i = 1; i <= 5; i++) {
    char key[3] = {'s', char('0' + i), '\0'};
    ratingPrefs.putUInt(key, starCount[i]);
  }
}

void loadRatingData() {
  if (RESET_RATINGS_ON_BOOT || !ratingPrefs.isKey("s5")) {
    for (int i = 1; i <= 5; i++) {
      starCount[i] = SEED_COUNTS[i - 1];
    }
    saveAllCounts();
  } else {
    for (int i = 1; i <= 5; i++) {
      char key[3] = {'s', char('0' + i), '\0'};
      starCount[i] = ratingPrefs.getUInt(key, 0);
    }
  }
}

void addRating(int stars) {
  if (stars < 1 || stars > 5) return;

  starCount[stars]++;

  char key[3] = {'s', char('0' + stars), '\0'};
  ratingPrefs.putUInt(key, starCount[stars]);

  // Send each review to the laptop over USB Serial.
  Serial.printf(
    "REVIEW,%d,%lu,%.2f\n",
    stars,
    (unsigned long)totalRatings(),
    averageRating()
  );
}

// ---------------- Touch calibration ----------------

void doTouchCalibration() {
  uint16_t calData[5];

  bool haveSaved =
    !FORCE_RECALIBRATE &&
    calPrefs.isKey("cal") &&
    calPrefs.getBytesLength("cal") == sizeof(calData);

  if (haveSaved) {
    calPrefs.getBytes("cal", calData, sizeof(calData));
    tft.setTouch(calData);
  } else {
    tft.fillScreen(colBg);
    tft.setFreeFont(&FreeSansBold12pt7b);
    tft.setTextColor(colText, colBg);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(TXT_CALIBRATE, tft.width() / 2, 28, GFXFF);

    tft.calibrateTouch(calData, TFT_YELLOW, colBg, 15);
    calPrefs.putBytes("cal", calData, sizeof(calData));
    tft.setTouch(calData);
  }
}

// ---------------- Star drawing ----------------

void getStarPts(
  int cx, int cy, int rOut, int rIn,
  int16_t* px, int16_t* py
) {
  for (int i = 0; i < 10; i++) {
    float a = -HALF_PI + i * (PI / 5.0f);
    int r = (i % 2 == 0) ? rOut : rIn;

    px[i] = cx + (int)roundf(r * cosf(a));
    py[i] = cy + (int)roundf(r * sinf(a));
  }
}

void drawStarFilled(
  int cx, int cy, int rOut, int rIn, uint16_t color
) {
  int16_t px[10], py[10];
  getStarPts(cx, cy, rOut, rIn, px, py);

  for (int i = 0; i < 10; i++) {
    int j = (i + 1) % 10;
    tft.fillTriangle(
      cx, cy, px[i], py[i], px[j], py[j], color
    );
  }
}

void drawStarOutline(
  int cx, int cy, int rOut, int rIn, uint16_t color
) {
  int16_t px[10], py[10];
  getStarPts(cx, cy, rOut, rIn, px, py);

  for (int i = 0; i < 10; i++) {
    int j = (i + 1) % 10;
    tft.drawLine(px[i], py[i], px[j], py[j], color);
  }
}

void drawSummaryStar(
  int cx, int cy, int rOut, int rIn, float amount
) {
  if (amount >= 0.99f) {
    drawStarFilled(cx, cy, rOut, rIn, colGold);
  } else if (amount >= 0.49f) {
    drawStarFilled(cx, cy, rOut, rIn, colGold);
    tft.fillRect(
      cx, cy - rOut - 2,
      rOut + 3, rOut * 2 + 4, colCard
    );
  }

  drawStarOutline(cx, cy, rOut, rIn, colStarEmpty);
}

// ---------------- UI helpers ----------------

void drawThickLine(
  float x0, float y0, float x1, float y1,
  int thickness, uint16_t color
) {
  float dx = x1 - x0;
  float dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);

  if (len < 0.5f) return;

  float nx = -dy / len;
  float ny = dx / len;

  for (int t = -thickness / 2; t <= thickness / 2; t++) {
    tft.drawLine(
      (int)(x0 + nx * t),
      (int)(y0 + ny * t),
      (int)(x1 + nx * t),
      (int)(y1 + ny * t),
      color
    );
  }
}

void drawCheckBadge(int cx, int cy, int r) {
  tft.fillCircle(cx, cy, r, colAccent);

  float x1 = cx - r * 0.48f;
  float y1 = cy + r * 0.02f;
  float x2 = cx - r * 0.12f;
  float y2 = cy + r * 0.36f;
  float x3 = cx + r * 0.52f;
  float y3 = cy - r * 0.34f;

  drawThickLine(x1, y1, x2, y2, 5, colOnAccent);
  drawThickLine(x2, y2, x3, y3, 5, colOnAccent);
}


void drawButton(
  int x, int y, int w, int h,
  const char* label,
  uint16_t fillColor,
  uint16_t borderColor,
  uint16_t textColor
) {
  // Draw a clean pill without a separate rectangular outline
  tft.fillRoundRect(x, y, w, h, h / 2, fillColor);

  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextColor(textColor, fillColor);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, x + w / 2, y + h / 2 + 1, GFXFF);
}


bool inRect(int tx, int ty, int x, int y, int w, int h) {
  return tx >= x && tx <= x + w &&
         ty >= y && ty <= y + h;
}

void drawTitle() {
  tft.setFreeFont(&FreeSerifBold18pt7b);
  tft.setTextColor(colText, colBg);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(TXT_PRODUCT_NAME, 160, 5, GFXFF);
}

// ---------------- HOME SCREEN ----------------


void drawHome() {
  tft.fillScreen(colBg);
  drawTitle();

  // Presenter card
  tft.fillRoundRect(12, 48, 296, 112, 14, colCard);
  tft.drawRoundRect(12, 48, 296, 112, 14, colAccentDeep);

  // Heading
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(colAccent, colCard);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("PRESENTED BY", 160, 68, GFXFF);

  // Vertical presenter list
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextColor(colText, colCard);
  tft.setTextDatum(MC_DATUM);

  int nameY = 94;
  int spacing = 22;

  for (int i = 0; i < PRESENTER_COUNT; i++) {
    tft.drawString(PRESENTER_NAMES[i], 160, nameY + (i * spacing), GFXFF);
  }

  // Rate button
  drawButton(
    BTN_X, HOME_BTN_Y, BTN_W, HOME_BTN_H,
    TXT_BTN_RATE_NOW,
    colAccent, colAccentDeep, colOnAccent
  );
}


void handleHomeTouch(int tx, int ty) {
  if (inRect(tx, ty, BTN_X, HOME_BTN_Y, BTN_W, HOME_BTN_H)) {
    selectedRating = 0;
    currentScreen = SCREEN_RATE;
    drawRate();
  }
}

// ---------------- RATE SCREEN ----------------

void drawRateStars() {
  tft.fillRect(
    0, STAR_CY - STAR_ROUT - 6,
    320, (STAR_ROUT + 6) * 2,
    colBg
  );

  for (int i = 0; i < 5; i++) {
    if (i < selectedRating) {
      drawStarFilled(
        STAR_CX[i], STAR_CY, STAR_ROUT, STAR_RIN, colGold
      );
      drawStarOutline(
        STAR_CX[i], STAR_CY, STAR_ROUT, STAR_RIN, colAccentDeep
      );
    } else {
      drawStarOutline(
        STAR_CX[i], STAR_CY, STAR_ROUT, STAR_RIN, colStarEmpty
      );
    }
  }
}

void drawRateHint() {
  tft.fillRect(0, 58, 320, 26, colBg);

  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(colTextDim, colBg);

  if (selectedRating == 0) {
    tft.drawString(TXT_HINT_EMPTY, 160, 64, GFXFF);
  } else {
    char buf[48];
    snprintf(buf, sizeof(buf), TXT_HINT_PICKED, selectedRating);
    tft.drawString(buf, 160, 64, GFXFF);
  }
}

void drawRateSubmit() {
  if (selectedRating > 0) {
    drawButton(
      BTN_X, LOW_BTN_Y, BTN_W, LOW_BTN_H,
      TXT_BTN_SUBMIT,
      colAccent, colAccentDeep, colOnAccent
    );
  } else {
    drawButton(
      BTN_X, LOW_BTN_Y, BTN_W, LOW_BTN_H,
      TXT_BTN_SUBMIT,
      colTrack, colTrack, colTextDim
    );
  }
}

void drawRate() {
  tft.fillScreen(colBg);
  drawTitle();
  drawRateHint();
  drawRateStars();
  drawRateSubmit();
}

void handleRateTouch(int tx, int ty) {
  for (int i = 0; i < 5; i++) {
    if (inRect(
      tx, ty,
      STAR_CX[i] - 29, STAR_CY - 40,
      58, 80
    )) {
      selectedRating = i + 1;
      drawRateHint();
      drawRateStars();
      drawRateSubmit();
      return;
    }
  }

  if (
    selectedRating > 0 &&
    inRect(tx, ty, BTN_X, LOW_BTN_Y, BTN_W, LOW_BTN_H)
  ) {
    addRating(selectedRating);
    lastSubmittedRating = selectedRating;
    currentScreen = SCREEN_THANKS;
    drawThanks();
  }
}

// ---------------- THANKS SCREEN ----------------

void drawThanks() {
  tft.fillScreen(colBg);
  drawCheckBadge(160, 76, 32);

  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextColor(colText, colBg);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(TXT_THANKS, 160, 122, GFXFF);

  char buf[48];
  snprintf(buf, sizeof(buf), TXT_THANKS_SUB, lastSubmittedRating);

  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(colTextDim, colBg);
  tft.drawString(buf, 160, 150, GFXFF);

  drawButton(
    BTN_X, LOW_BTN_Y, BTN_W, LOW_BTN_H,
    TXT_BTN_BACK_HOME,
    colAccent, colAccentDeep, colOnAccent
  );
}

void handleThanksTouch(int tx, int ty) {
  if (inRect(tx, ty, BTN_X, LOW_BTN_Y, BTN_W, LOW_BTN_H)) {
    currentScreen = SCREEN_HOME;
    drawHome();
  }
}

// ---------------- SETUP / LOOP ----------------

void setup() {
  Serial.begin(115200);
  delay(300);

  tft.init();
  tft.setRotation(SCREEN_ROTATION);
  buildPalette();

  ratingPrefs.begin("kiosk", false);
  calPrefs.begin("touchcal", false);

  doTouchCalibration();
  loadRatingData();
  drawHome();

  Serial.println("KIOSK_READY");
}

void loop() {
  uint16_t tx, ty;

  if (tft.getTouch(&tx, &ty)) {
    switch (currentScreen) {
      case SCREEN_HOME:
        handleHomeTouch(tx, ty);
        break;

      case SCREEN_RATE:
        handleRateTouch(tx, ty);
        break;

      case SCREEN_THANKS:
        handleThanksTouch(tx, ty);
        break;
    }

    while (tft.getTouch(&tx, &ty)) {
      delay(10);
    }

    delay(150);
  }
}
