
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// ============================================================
// CONFIG — EDIT THESE SETTINGS
// ============================================================

const char* PRODUCT_NAME = "AI Robotic Arm";

// Add, remove, or rename presenters here.
const char* PRESENTER_NAMES[] = {
  "Jatin",
  "Sufyaan",
  "Gunamsh"
};

const uint8_t PRESENTER_COUNT =
  sizeof(PRESENTER_NAMES) / sizeof(PRESENTER_NAMES[0]);

const uint8_t SCREEN_ROTATION = 1;
const bool RESET_RATINGS_ON_BOOT = false;
const bool FORCE_RECALIBRATE = false;

// First-boot ratings: 1-star to 5-star
const uint32_t SEED_COUNTS[5] = {26, 4, 12, 38, 122};

// ============================================================
// COLOUR PALETTE — MIDNIGHT / CYAN / GOLD
// ============================================================

#define RGB_BG       12, 18, 32
#define RGB_CARD     24, 34, 53
#define RGB_CARD2    31, 45, 65
#define RGB_TRACK    47, 59, 76
#define RGB_WHITE    239, 244, 250
#define RGB_MUTED    151, 169, 190
#define RGB_CYAN     55, 210, 220
#define RGB_GOLD     246, 190, 91
#define RGB_DARK     12, 25, 35
#define RGB_EMPTY    87, 105, 126

// ============================================================
// LAYOUT — 320 x 240 LANDSCAPE
// ============================================================

const int BTN_X = 18;
const int BTN_W = 284;
const int HOME_BTN_Y = 192;
const int HOME_BTN_H = 34;

const int LOW_BTN_Y = 196;
const int LOW_BTN_H = 28;

const int STAR_CX[5] = {44, 102, 160, 218, 276};
const int STAR_CY = 118;
const int STAR_ROUT = 26;
const int STAR_RIN = 11;

// ============================================================
// GLOBALS
// ============================================================

TFT_eSPI tft = TFT_eSPI();
Preferences ratingPrefs;
Preferences calPrefs;

uint16_t colBg, colCard, colCard2, colTrack;
uint16_t colWhite, colMuted, colCyan, colGold;
uint16_t colDark, colEmpty;

enum Screen {
  SCREEN_HOME,
  SCREEN_RATE,
  SCREEN_THANKS
};

Screen currentScreen = SCREEN_HOME;

uint32_t starCount[6] = {0};
int selectedRating = 0;
int lastSubmittedRating = 0;

// ============================================================
// PALETTE
// ============================================================

void buildPalette() {
  colBg     = tft.color565(RGB_BG);
  colCard   = tft.color565(RGB_CARD);
  colCard2  = tft.color565(RGB_CARD2);
  colTrack  = tft.color565(RGB_TRACK);
  colWhite  = tft.color565(RGB_WHITE);
  colMuted  = tft.color565(RGB_MUTED);
  colCyan   = tft.color565(RGB_CYAN);
  colGold   = tft.color565(RGB_GOLD);
  colDark   = tft.color565(RGB_DARK);
  colEmpty  = tft.color565(RGB_EMPTY);
}

// ============================================================
// RATING DATA
// ============================================================

uint32_t totalRatings() {
  uint32_t total = 0;
  for (int i = 1; i <= 5; i++) total += starCount[i];
  return total;
}

float averageRating() {
  uint32_t total = totalRatings();
  if (!total) return 0;

  uint32_t sum = 0;
  for (int i = 1; i <= 5; i++) {
    sum += i * starCount[i];
  }

  return (float)sum / total;
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

  // Send each individual review to the laptop over USB serial.
  Serial.print("REVIEW,");
  Serial.print(stars);
  Serial.print(",");
  Serial.print(totalRatings());
  Serial.print(",");
  Serial.println(averageRating(), 2);
}

// ============================================================
// TOUCH CALIBRATION
// ============================================================

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
    tft.setTextColor(colWhite, colBg);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Touch calibration", 160, 35, GFXFF);

    tft.calibrateTouch(calData, TFT_CYAN, colBg, 15);

    calPrefs.putBytes("cal", calData, sizeof(calData));
    tft.setTouch(calData);
  }
}

// ============================================================
// DRAWING HELPERS
// ============================================================

void drawTitle() {
  tft.setFreeFont(&FreeSerifBold18pt7b);
  tft.setTextColor(colWhite, colBg);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(PRODUCT_NAME, 160, 9, GFXFF);
}

void drawStar(int cx, int cy, int outer, int inner,
              uint16_t color, bool filled) {
  int16_t px[10], py[10];

  for (int i = 0; i < 10; i++) {
    float angle = -HALF_PI + i * PI / 5.0f;
    int radius = (i % 2 == 0) ? outer : inner;

    px[i] = cx + (int)roundf(radius * cosf(angle));
    py[i] = cy + (int)roundf(radius * sinf(angle));
  }

  for (int i = 0; i < 10; i++) {
    int j = (i + 1) % 10;

    if (filled) {
      tft.fillTriangle(cx, cy, px[i], py[i], px[j], py[j], color);
    } else {
      tft.drawLine(px[i], py[i], px[j], py[j], color);
    }
  }
}

void drawSummaryStar(int cx, int cy, float amount) {
  if (amount >= 0.99f) {
    drawStar(cx, cy, 10, 5, colGold, true);
  } else if (amount >= 0.49f) {
    drawStar(cx, cy, 10, 5, colGold, true);
    tft.fillRect(cx, cy - 12, 13, 24, colCard);
  }

  drawStar(cx, cy, 10, 5, colEmpty, false);
}

void drawButton(int x, int y, int w, int h,
                const char* label, uint16_t fill,
                uint16_t text) {
  tft.fillRoundRect(x, y, w, h, 10, fill);
  tft.drawRoundRect(x, y, w, h, 10, colCyan);

  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextColor(text, fill);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, x + w / 2, y + h / 2 + 1, GFXFF);
}

bool inRect(int tx, int ty, int x, int y, int w, int h) {
  return tx >= x && tx <= x + w &&
         ty >= y && ty <= y + h;
}

// ============================================================
// HOME SCREEN — NEW DESIGN
// ============================================================

void drawHome() {
  tft.fillScreen(colBg);

  // Header
  drawTitle();

  // Small cyan accent line
  tft.fillRoundRect(137, 39, 46, 3, 2, colCyan);

  // Presenter panel
  tft.fillRoundRect(14, 50, 292, 34, 9, colCard);
  tft.drawRoundRect(14, 50, 292, 34, 9, colTrack);

  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(colMuted, colCard);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("PRESENTED BY", 160, 53, GFXFF);

  // Build editable presenter name line
  char names[100] = "";

  for (int i = 0; i < PRESENTER_COUNT; i++) {
    if (i > 0) {
      strncat(names, "  |  ", sizeof(names) - strlen(names) - 1);
    }

    strncat(names, PRESENTER_NAMES[i],
            sizeof(names) - strlen(names) - 1);
  }

  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(colWhite, colCard);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(names, 160, 73, GFXFF);

  // Rating summary panel
  tft.fillRoundRect(14, 94, 292, 82, 12, colCard);
  tft.drawRoundRect(14, 94, 292, 82, 12, colTrack);

  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(colMuted, colCard);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("OVERALL RATING", 27, 102, GFXFF);

  // Large average number
  char avgText[10];
  snprintf(avgText, sizeof(avgText), "%.1f", averageRating());

  tft.setFreeFont(&FreeSansBold24pt7b);
  tft.setTextColor(colWhite, colCard);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(avgText, 28, 143, GFXFF);

  // Stars
  float rounded = roundf(averageRating() * 2.0f) / 2.0f;
  int full = (int)rounded;
  bool half = (rounded - full) >= 0.49f;

  for (int i = 0; i < 5; i++) {
    float amount = (i < full) ? 1.0f :
                   ((i == full && half) ? 0.5f : 0.0f);

    drawSummaryStar(139 + i * 29, 132, amount);
  }

  // Total ratings
  char countText[32];
  snprintf(countText, sizeof(countText), "%lu reviews",
           (unsigned long)totalRatings());

  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(colMuted, colCard);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(countText, 141, 151, GFXFF);

  // Rate button
  drawButton(BTN_X, HOME_BTN_Y, BTN_W, HOME_BTN_H,
             "RATE THIS PRODUCT", colCyan, colDark);
}

// ============================================================
// RATE SCREEN
// ============================================================

void drawRateStars() {
  tft.fillRect(0, 90, 320, 65, colBg);

  for (int i = 0; i < 5; i++) {
    if (i < selectedRating) {
      drawStar(STAR_CX[i], STAR_CY, STAR_ROUT,
               STAR_RIN, colGold, true);
    } else {
      drawStar(STAR_CX[i], STAR_CY, STAR_ROUT,
               STAR_RIN, colEmpty, false);
    }
  }
}

void drawRateHint() {
  tft.fillRect(0, 54, 320, 28, colBg);

  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(colMuted, colBg);

  if (selectedRating == 0) {
    tft.drawString("Tap a star to rate", 160, 60, GFXFF);
  } else {
    char text[40];
    snprintf(text, sizeof(text), "You selected %d out of 5",
             selectedRating);
    tft.drawString(text, 160, 60, GFXFF);
  }
}

void drawRateSubmit() {
  if (selectedRating > 0) {
    drawButton(18, LOW_BTN_Y, 284, LOW_BTN_H,
               "SUBMIT RATING", colCyan, colDark);
  } else {
    tft.fillRoundRect(18, LOW_BTN_Y, 284, LOW_BTN_H,
                      10, colTrack);
    tft.setFreeFont(&FreeSansBold12pt7b);
    tft.setTextColor(colMuted, colTrack);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("SUBMIT RATING", 160, LOW_BTN_Y + 15, GFXFF);
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
    if (inRect(tx, ty, STAR_CX[i] - 29,
               STAR_CY - 35, 58, 70)) {
      selectedRating = i + 1;
      drawRateHint();
      drawRateStars();
      drawRateSubmit();
      return;
    }
  }

  if (selectedRating > 0 &&
      inRect(tx, ty, 18, LOW_BTN_Y, 284, LOW_BTN_H)) {
    addRating(selectedRating);
    lastSubmittedRating = selectedRating;
    currentScreen = SCREEN_THANKS;
    drawThanks();
  }
}

// ============================================================
// THANK YOU SCREEN
// ============================================================

void drawThanks() {
  tft.fillScreen(colBg);

  tft.fillCircle(160, 76, 31, colCyan);
  tft.drawLine(145, 76, 155, 87, colDark);
  tft.drawLine(155, 87, 177, 62, colDark);
  tft.drawLine(146, 76, 156, 87, colDark);
  tft.drawLine(156, 87, 178, 62, colDark);

  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextColor(colWhite, colBg);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("THANK YOU!", 160, 122, GFXFF);

  char text[40];
  snprintf(text, sizeof(text), "You gave %d out of 5",
           lastSubmittedRating);

  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(colMuted, colBg);
  tft.drawString(text, 160, 150, GFXFF);

  drawButton(18, LOW_BTN_Y, 284, LOW_BTN_H,
             "BACK TO HOME", colCyan, colDark);
}

void drawThanks();

void handleThanksTouch(int tx, int ty) {
  if (inRect(tx, ty, 18, LOW_BTN_Y, 284, LOW_BTN_H)) {
    currentScreen = SCREEN_HOME;
    drawHome();
  }
}

// ============================================================
// SETUP / LOOP
// ============================================================

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
        if (inRect(tx, ty, BTN_X, HOME_BTN_Y, BTN_W, HOME_BTN_H)) {
          selectedRating = 0;
          currentScreen = SCREEN_RATE;
          drawRate();
        }
        break;

      case SCREEN_RATE:
        handleRateTouch(tx, ty);
        break;

      case SCREEN_THANKS:
        handleThanksTouch(tx, ty);
        break;
    }

    while (tft.getTouch(&tx, &ty)) delay(10);
    delay(150);
  }
}
