// =====================================================================
//  Win-style OS for M5Stack Cardputer ADV
//  Libraries: M5Cardputer, M5Unified, M5GFX (+ built-in SPI, SD, Preferences)
//  Controls (Fn layer): Fn+; UP  Fn+, LEFT  Fn+. DOWN  Fn+/ RIGHT
//                       Enter = select,  Fn+` = back / close app
// =====================================================================

#include <M5Cardputer.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// ---------------------- TYPES (must be before any function) ----------------------
enum KeyKind { KK_NONE, KK_UP, KK_DOWN, KK_LEFT, KK_RIGHT, KK_ENTER, KK_BACK, KK_DEL, KK_CHAR };
struct KeyEvent { KeyKind kind; char ch; };
enum AppKind { APP_NONE, APP_CALC, APP_SNAKE, APP_TETRIS, APP_FILES, APP_SETTINGS, APP_SLEEP };

// ---------------------- SCREEN / LAYOUT ----------------------
static const int SCR_W = 240;
static const int SCR_H = 135;
static const int TASKBAR_H = 14;
static const int CONTENT_T = 13;
static const int CONTENT_B = 121;
static const int CONTENT_H = CONTENT_B - CONTENT_T;

static M5Canvas *fb = nullptr;     // back buffer (no flicker)

// ---------------------- COLORS ----------------------
static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static const uint16_t COL_WHITE = rgb565(255, 255, 255);
static const uint16_t COL_BLACK = rgb565(0, 0, 0);
static const uint16_t COL_GRAY  = rgb565(192, 192, 192);
static const uint16_t COL_DGRAY = rgb565(128, 128, 128);
static const uint16_t COL_NAVY  = rgb565(0, 0, 128);
static const uint16_t COL_GREEN = rgb565(0, 200, 80);
static const uint16_t COL_RED   = rgb565(230, 40, 40);

static uint16_t tetColor(int t) {
  static const uint8_t rgbT[7][3] = {
    {0, 230, 230}, {240, 220, 0}, {160, 0, 220}, {0, 200, 0},
    {230, 0, 0}, {0, 60, 230}, {255, 140, 0}
  };
  return rgb565(rgbT[t][0], rgbT[t][1], rgbT[t][2]);
}

// ---------------------- INPUT ----------------------
static KeyEvent keyEvents[8];
static int keyCount = 0;

static void pollKeys() {
  keyCount = 0;
  M5Cardputer.update();
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
  auto ks = M5Cardputer.Keyboard.keysState();

  for (char c : ks.word) {
    if (keyCount >= 8) break;
    KeyKind k = KK_NONE;
    if (ks.fn) {
      switch (c) {
        case ';': k = KK_UP; break;
        case ',': k = KK_LEFT; break;
        case '.': k = KK_DOWN; break;
        case '/': k = KK_RIGHT; break;
        case '`': k = KK_BACK; break;
        default: break;
      }
    } else {
      k = KK_CHAR;
    }
    if (k != KK_NONE) { keyEvents[keyCount].kind = k; keyEvents[keyCount].ch = c; keyCount++; }
  }
  if (ks.enter && keyCount < 8) { keyEvents[keyCount].kind = KK_ENTER; keyEvents[keyCount].ch = 0; keyCount++; }
  if (ks.del   && keyCount < 8) { keyEvents[keyCount].kind = KK_DEL;   keyEvents[keyCount].ch = 0; keyCount++; }
}

// ---------------------- APP STATE ----------------------
static AppKind openApp = APP_NONE;

static const int ICON_COUNT = 6;
static const AppKind iconApp[ICON_COUNT] = {APP_CALC, APP_SNAKE, APP_TETRIS, APP_FILES, APP_SETTINGS, APP_SLEEP};
static const char* iconLabel[ICON_COUNT] = {"Calc", "Snake", "Tetris", "Files", "Settings", "Sleep"};
static int deskSel = 0;
static float cursorX = 120, cursorY = 60, targetX = 0, targetY = 0;

static const char* appTitle(AppKind a) {
  switch (a) {
    case APP_CALC: return "Calc";
    case APP_SNAKE: return "Snake";
    case APP_TETRIS: return "Tetris";
    case APP_FILES: return "Files";
    case APP_SETTINGS: return "Settings";
    case APP_SLEEP: return "Sleep";
    default: return "";
  }
}

// ---------------------- DRAW HELPERS ----------------------
static void drawText(int x, int y, const char* s, uint16_t col) {
  fb->setTextColor(col);
  fb->setCursor(x, y);
  fb->print(s);
}

static void drawBevel(int x, int y, int w, int h, bool raised) {
  fb->fillRect(x, y, w, h, COL_GRAY);
  uint16_t hi = raised ? COL_WHITE : COL_DGRAY;
  uint16_t lo = raised ? COL_DGRAY : COL_WHITE;
  fb->drawFastHLine(x, y, w, hi);
  fb->drawFastVLine(x, y, h, hi);
  fb->drawFastHLine(x, y + h - 1, w, lo);
  fb->drawFastVLine(x + w - 1, y, h, lo);
}

static void drawWindowFrame(const char* title) {
  fb->fillRect(0, 0, SCR_W, SCR_H - TASKBAR_H, COL_GRAY);
  fb->fillRect(2, 1, SCR_W - 4, 11, COL_NAVY);
  drawText(5, 3, title, COL_WHITE);
  drawBevel(SCR_W - 14, 2, 11, 9, true);
  drawText(SCR_W - 11, 3, "x", COL_BLACK);
}

// ---------------------- DESKTOP: WALLPAPER (code-drawn) ----------------------
static void drawWallpaper() {
  for (int y = 0; y < SCR_H; y++) {
    int t = y * 255 / SCR_H;
    uint8_t r = (40 * t) / 255;
    uint8_t g = (48 * (255 - t) + 150 * t) / 255;
    uint8_t b = (130 * (255 - t) + 170 * t) / 255;
    fb->drawFastHLine(0, y, SCR_W, rgb565(r, g, b));
  }
  uint16_t gridCol = rgb565(40, 110, 160);
  for (int x = 0; x < SCR_W; x += 16) fb->drawFastVLine(x, 0, SCR_H - TASKBAR_H, gridCol);
  for (int y = 0; y < SCR_H - TASKBAR_H; y += 16) fb->drawFastHLine(0, y, SCR_W, gridCol);
}

// ---------------------- DESKTOP: ICONS (code-drawn) ----------------------
static void iconPosition(int i, int &x, int &y) {
  x = 22 + (i % 3) * 72;
  y = 16 + (i / 3) * 46;
}

static void drawIcon(int idx, int x, int y) {
  switch (idx) {
    case 0: { // Calculator
      fb->fillRoundRect(x + 4, y + 2, 24, 28, 3, rgb565(210, 210, 210));
      fb->drawRoundRect(x + 4, y + 2, 24, 28, 3, rgb565(60, 60, 60));
      fb->fillRect(x + 7, y + 5, 18, 7, rgb565(120, 200, 120));
      fb->drawRect(x + 7, y + 5, 18, 7, rgb565(40, 60, 40));
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++)
          fb->fillRect(x + 7 + c * 5, y + 14 + r * 5, 4, 3, rgb565(70, 70, 90));
      break;
    }
    case 1: { // Snake
      const int px[5] = {5, 9, 13, 17, 21};
      const int py[5] = {24, 20, 20, 16, 16};
      for (int i = 0; i < 5; i++) fb->fillCircle(x + px[i], y + py[i], 4, COL_GREEN);
      fb->fillCircle(x + 25, y + 12, 5, rgb565(0, 150, 50));
      fb->fillRect(x + 27, y + 10, 2, 2, COL_BLACK);
      fb->fillCircle(x + 8, y + 8, 3, COL_RED);
      break;
    }
    case 2: { // Tetris blocks
      struct Cell { int cx, cy; uint8_t t; };
      const Cell cells[8] = {
        {4, 4, 0}, {10, 4, 0}, {10, 10, 0}, {16, 10, 0},
        {4, 18, 1}, {10, 18, 1}, {4, 24, 1}, {10, 24, 1}
      };
      for (int i = 0; i < 8; i++) {
        fb->fillRect(x + cells[i].cx, y + cells[i].cy, 5, 5, tetColor(cells[i].t));
        fb->drawRect(x + cells[i].cx, y + cells[i].cy, 5, 5, COL_BLACK);
      }
      fb->fillRect(x + 16, y + 18, 5, 5, tetColor(2));
      fb->fillRect(x + 16, y + 24, 5, 5, tetColor(6));
      break;
    }
    case 3: { // Folder
      fb->fillRect(x + 2, y + 6, 11, 4, rgb565(230, 180, 40));
      fb->fillRect(x + 2, y + 9, 28, 18, rgb565(255, 210, 60));
      fb->drawRect(x + 2, y + 9, 28, 18, rgb565(150, 110, 0));
      break;
    }
    case 4: { // Gear
      uint16_t gearCol = rgb565(150, 150, 165);
      fb->fillCircle(x + 16, y + 16, 11, gearCol);
      fb->fillRect(x + 14, y + 2, 4, 6, gearCol);
      fb->fillRect(x + 14, y + 24, 4, 6, gearCol);
      fb->fillRect(x + 2, y + 14, 6, 4, gearCol);
      fb->fillRect(x + 24, y + 14, 6, 4, gearCol);
      fb->fillCircle(x + 16, y + 16, 4, rgb565(60, 60, 80));
      break;
    }
    case 5: { // Moon (sleep)
      fb->fillCircle(x + 16, y + 16, 12, rgb565(250, 230, 120));
      fb->fillCircle(x + 22, y + 12, 10, rgb565(40, 60, 110));  // bite out the crescent
      fb->fillRect(x + 4, y + 26, 3, 3, COL_WHITE);             // little stars
      fb->fillRect(x + 26, y + 4, 2, 2, COL_WHITE);
      break;
    }
  }
}

static void drawDesktop() {
  drawWallpaper();
  for (int i = 0; i < ICON_COUNT; i++) {
    int x, y;
    iconPosition(i, x, y);
    if (i == deskSel) fb->drawRect(x - 3, y - 3, 38, 38, COL_WHITE);
    drawIcon(i, x, y);
    int tw = fb->textWidth(iconLabel[i]);
    int lx = x + 16 - tw / 2;
    int ly = y + 35;
    if (i == deskSel) fb->fillRect(lx - 2, ly - 1, tw + 4, 10, COL_NAVY);
    drawText(lx + 1, ly + 1, iconLabel[i], COL_BLACK);
    drawText(lx, ly, iconLabel[i], COL_WHITE);
  }
}

// ---------------------- MOUSE CURSOR (smooth) ----------------------
static void drawCursor() {
  int x = (int)cursorX, y = (int)cursorY;
  fb->fillTriangle(x - 1, y - 1, x - 1, y + 13, x + 4, y + 9, COL_BLACK);
  fb->fillTriangle(x, y, x, y + 11, x + 3, y + 8, COL_WHITE);
  fb->fillRect(x + 2, y + 8, 2, 5, COL_WHITE);
}

// ---------------------- BATTERY (official Cardputer API) ----------------------
static int batteryPct = 0;
static int batteryMv = 0;
static uint32_t batteryTime = 0;
static bool batteryInit = false;

static void refreshBattery() {
  if (batteryInit && millis() - batteryTime < 2000) return;
  batteryInit = true;
  batteryTime = millis();
  batteryPct = M5Cardputer.Power.getBatteryLevel();   // 0..100 %, M5Unified estimate
  batteryMv  = M5Cardputer.Power.getBatteryVoltage(); // mV, for debugging
  if (batteryPct < 0) batteryPct = 0;
  if (batteryPct > 100) batteryPct = 100;
}

// ---------------------- TASKBAR ----------------------
static void drawTaskbar() {
  int y = SCR_H - TASKBAR_H;
  fb->fillRect(0, y, SCR_W, TASKBAR_H, COL_GRAY);
  fb->drawFastHLine(0, y, SCR_W, COL_WHITE);

  // Start button with 4-color logo
  drawBevel(2, y + 2, 46, TASKBAR_H - 4, true);
  fb->fillRect(7, y + 5, 3, 3, rgb565(255, 60, 60));
  fb->fillRect(11, y + 5, 3, 3, rgb565(60, 200, 60));
  fb->fillRect(7, y + 9, 3, 3, rgb565(60, 120, 255));
  fb->fillRect(11, y + 9, 3, 3, rgb565(255, 200, 40));
  drawText(17, y + 4, "Start", COL_BLACK);

  // Open app button
  if (openApp != APP_NONE) {
    drawBevel(52, y + 2, 70, TASKBAR_H - 4, false);
    drawText(56, y + 4, appTitle(openApp), COL_BLACK);
  }

  // Tray: battery icon + percent
  refreshBattery();
  drawBevel(SCR_W - 62, y + 2, 60, TASKBAR_H - 4, false);
  fb->drawRect(SCR_W - 56, y + 5, 22, 7, COL_BLACK);
  fb->fillRect(SCR_W - 33, y + 7, 2, 3, COL_BLACK);
  int bw = batteryPct * 18 / 100;
  fb->fillRect(SCR_W - 54, y + 7, bw, 3, batteryPct > 20 ? COL_GREEN : COL_RED);
  char pb[8];
  snprintf(pb, sizeof pb, "%d%%", batteryPct);
  drawText(SCR_W - 28, y + 4, pb, COL_BLACK);
}

// ---------------------- CALCULATOR ----------------------
static const char calcMap[16] = {'7','8','9','/','4','5','6','*','1','2','3','-','C','0','=','+'};
static char calcBuf[24] = "";
static int calcLen = 0;
static int calcSel = 0;
static bool calcResult = false;
static bool calcErr = false;

// Tiny recursive-descent parser: + - * / ( ) and decimals
static const char* parsePos;
static bool parseErr;
static double parseExpr();

static bool isDigitChar(char c) { return c >= '0' && c <= '9'; }

static double parseFactor() {
  bool neg = false;
  if (*parsePos == '-') { neg = true; parsePos++; }
  if (*parsePos == '(') {
    parsePos++;
    double v = parseExpr();
    if (*parsePos == ')') parsePos++; else parseErr = true;
    return neg ? -v : v;
  }
  if (!(isDigitChar(*parsePos) || *parsePos == '.')) { parseErr = true; return 0; }
  double v = 0;
  while (isDigitChar(*parsePos)) { v = v * 10 + (*parsePos - '0'); parsePos++; }
  if (*parsePos == '.') {
    parsePos++;
    double f = 0.1;
    while (isDigitChar(*parsePos)) { v += (*parsePos - '0') * f; f *= 0.1; parsePos++; }
  }
  return neg ? -v : v;
}

static double parseTerm() {
  double v = parseFactor();
  while (*parsePos == '*' || *parsePos == '/') {
    char op = *parsePos++;
    double r = parseFactor();
    if (op == '*') v *= r;
    else { if (r == 0) { parseErr = true; return 0; } v /= r; }
  }
  return v;
}

static double parseExpr() {
  double v = parseTerm();
  while (*parsePos == '+' || *parsePos == '-') {
    char op = *parsePos++;
    double r = parseTerm();
    v = (op == '+') ? v + r : v - r;
  }
  return v;
}

static void calcEvaluate() {
  parsePos = calcBuf;
  parseErr = false;
  double v = parseExpr();
  if (*parsePos != 0) parseErr = true;
  if (parseErr || !isfinite(v)) {
    strcpy(calcBuf, "Error");
    calcLen = 5;
    calcErr = true;
  } else {
    if (fabs(v - round(v)) < 1e-9 && fabs(v) < 1e15)
      snprintf(calcBuf, sizeof calcBuf, "%lld", (long long)llround(v));
    else
      snprintf(calcBuf, sizeof calcBuf, "%.6g", v);
    calcLen = strlen(calcBuf);
  }
  calcResult = true;
}

static void calcPress(char c) {
  if (c == 'C' || c == 'c') {
    calcLen = 0; calcBuf[0] = 0; calcResult = false; calcErr = false;
    return;
  }
  if (c == '=') { calcEvaluate(); return; }
  bool isNum = isDigitChar(c) || c == '.';
  if (calcErr || (calcResult && isNum)) { calcLen = 0; calcBuf[0] = 0; calcErr = false; }
  calcResult = false;
  if (calcLen < 22) { calcBuf[calcLen++] = c; calcBuf[calcLen] = 0; }
}

static void calcKey(const KeyEvent& e) {
  switch (e.kind) {
    case KK_UP:    if (calcSel >= 4) calcSel -= 4; break;
    case KK_DOWN:  if (calcSel < 12) calcSel += 4; break;
    case KK_LEFT:  if (calcSel % 4) calcSel--; break;
    case KK_RIGHT: if (calcSel % 4 < 3) calcSel++; break;
    case KK_ENTER: calcPress(calcMap[calcSel]); break;
    case KK_DEL:
      if (calcErr) { calcLen = 0; calcBuf[0] = 0; calcErr = false; }
      else if (calcLen > 0) { calcBuf[--calcLen] = 0; }
      break;
    case KK_CHAR:
      if (strchr("0123456789.+-*/=cC", e.ch)) calcPress(e.ch);
      break;
    default: break;
  }
}

static void drawCalc() {
  drawWindowFrame("Calc");
  // display
  fb->fillRect(6, CONTENT_T + 3, SCR_W - 12, 18, COL_WHITE);
  fb->drawRect(6, CONTENT_T + 3, SCR_W - 12, 18, COL_DGRAY);
  fb->setTextSize(2);
  const char* shown = calcLen ? calcBuf : "0";
  int tw = fb->textWidth(shown);
  drawText(SCR_W - 10 - tw, CONTENT_T + 4, shown, COL_BLACK);
  fb->setTextSize(1);
  // keypad
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      int idx = r * 4 + c;
      int x = 6 + c * 58;
      int y = CONTENT_T + 25 + r * 19;
      bool sel = (idx == calcSel);
      if (sel) fb->fillRect(x, y, 55, 18, COL_NAVY);
      else drawBevel(x, y, 55, 18, true);
      char lbl[2] = {calcMap[idx], 0};
      int lw = fb->textWidth(lbl);
      drawText(x + (55 - lw) / 2, y + 5, lbl, sel ? COL_WHITE : COL_BLACK);
    }
  }
}

// ---------------------- SNAKE ----------------------
static const int SNAKE_CELL = 6;
static const int GRID_W = SCR_W / SNAKE_CELL;     // 40
static const int GRID_H = CONTENT_H / SNAKE_CELL; // 18
static int snakeXs[GRID_W * GRID_H], snakeYs[GRID_W * GRID_H];
static int snakeLen, snakeDir, snakeNext, foodX, foodY, snakeScore;
static bool snakeDead;
static uint32_t snakeTick;

static void placeFood() {
  bool ok;
  do {
    foodX = random(GRID_W);
    foodY = random(GRID_H);
    ok = true;
    for (int i = 0; i < snakeLen; i++)
      if (snakeXs[i] == foodX && snakeYs[i] == foodY) { ok = false; break; }
  } while (!ok);
}

static void snakeReset() {
  snakeLen = 3;
  snakeDir = snakeNext = 3;
  for (int i = 0; i < snakeLen; i++) { snakeXs[i] = 10 - i; snakeYs[i] = GRID_H / 2; }
  snakeScore = 0;
  snakeDead = false;
  snakeTick = millis();
  placeFood();
}

static void snakeUpdate() {
  if (snakeDead) return;
  if (millis() - snakeTick < 110) return;
  snakeTick = millis();
  snakeDir = snakeNext;
  int nx = snakeXs[0], ny = snakeYs[0];
  if (snakeDir == 0) ny--;
  else if (snakeDir == 1) ny++;
  else if (snakeDir == 2) nx--;
  else nx++;

  if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) { snakeDead = true; return; }
  for (int i = 0; i < snakeLen; i++)
    if (snakeXs[i] == nx && snakeYs[i] == ny) { snakeDead = true; return; }

  bool eat = (nx == foodX && ny == foodY);
  if (eat && snakeLen < GRID_W * GRID_H - 1) snakeLen++;
  for (int i = snakeLen - 1; i > 0; i--) { snakeXs[i] = snakeXs[i - 1]; snakeYs[i] = snakeYs[i - 1]; }
  snakeXs[0] = nx; snakeYs[0] = ny;
  if (eat) { snakeScore++; placeFood(); }
}

static void snakeKey(const KeyEvent& e) {
  switch (e.kind) {
    case KK_UP:    if (snakeDir != 1) snakeNext = 0; break;
    case KK_DOWN:  if (snakeDir != 0) snakeNext = 1; break;
    case KK_LEFT:  if (snakeDir != 3) snakeNext = 2; break;
    case KK_RIGHT: if (snakeDir != 2) snakeNext = 3; break;
    case KK_ENTER: if (snakeDead) snakeReset(); break;
    default: break;
  }
}

static void drawSnake() {
  char t[32];
  snprintf(t, sizeof t, "Snake   Score: %d", snakeScore);
  drawWindowFrame(t);
  fb->fillRect(0, CONTENT_T, SCR_W, CONTENT_H, COL_BLACK);
  fb->fillRect(foodX * SNAKE_CELL, CONTENT_T + foodY * SNAKE_CELL, SNAKE_CELL - 1, SNAKE_CELL - 1, COL_RED);
  for (int i = 0; i < snakeLen; i++) {
    uint16_t col = (i == 0) ? rgb565(180, 255, 120) : COL_GREEN;
    fb->fillRect(snakeXs[i] * SNAKE_CELL, CONTENT_T + snakeYs[i] * SNAKE_CELL, SNAKE_CELL - 1, SNAKE_CELL - 1, col);
  }
  if (snakeDead) {
    fb->fillRect(50, CONTENT_T + 30, 140, 40, COL_WHITE);
    fb->drawRect(50, CONTENT_T + 30, 140, 40, COL_BLACK);
    drawText(88, CONTENT_T + 38, "GAME OVER", COL_RED);
    drawText(70, CONTENT_T + 54, "Enter = retry", COL_BLACK);
  }
}

// ---------------------- TETRIS ----------------------
static const int TET_W = 10, TET_H = 20, TET_CELL = 5;
static uint8_t tetBoard[TET_H][TET_W];
// 4x4 bitmasks, bit index = y*4 + x
static const uint16_t tetShapes[7] = {0x00F0, 0x0660, 0x0072, 0x0036, 0x0063, 0x0071, 0x0074};
static uint16_t tetMask;
static int tetType, tetNextType, tetX, tetY;
static int tetScore, tetLines, tetLevel;
static bool tetOver;
static uint32_t tetTick;

static bool tetFits(uint16_t m, int x, int y) {
  for (int i = 0; i < 16; i++) {
    if (!((m >> i) & 1)) continue;
    int nx = x + (i & 3), ny = y + (i >> 2);
    if (nx < 0 || nx >= TET_W || ny >= TET_H) return false;
    if (ny >= 0 && tetBoard[ny][nx]) return false;
  }
  return true;
}

static uint16_t tetRotate(uint16_t m) {
  uint16_t n = 0;
  for (int i = 0; i < 16; i++) {
    if (!((m >> i) & 1)) continue;
    int cx = i & 3, cy = i >> 2;
    int nx = 3 - cy, ny = cx;
    n |= (1 << (ny * 4 + nx));
  }
  return n;
}

static void tetSpawn() {
  tetType = tetNextType;
  tetNextType = random(7);
  tetMask = tetShapes[tetType];
  tetX = 3;
  tetY = 0;
  if (!tetFits(tetMask, tetX, tetY)) tetOver = true;
}

static void tetLock() {
  for (int i = 0; i < 16; i++) {
    if (!((tetMask >> i) & 1)) continue;
    int ny = tetY + (i >> 2), nx = tetX + (i & 3);
    if (ny >= 0 && ny < TET_H && nx >= 0 && nx < TET_W) tetBoard[ny][nx] = tetType + 1;
  }
  int cleared = 0;
  for (int r = TET_H - 1; r >= 0; r--) {
    bool full = true;
    for (int c = 0; c < TET_W; c++) if (!tetBoard[r][c]) { full = false; break; }
    if (full) {
      cleared++;
      for (int rr = r; rr > 0; rr--) memcpy(tetBoard[rr], tetBoard[rr - 1], TET_W);
      memset(tetBoard[0], 0, TET_W);
      r++;  // re-check the same row index after shifting
    }
  }
  if (cleared) {
    static const int pts[5] = {0, 100, 300, 500, 800};
    tetScore += pts[cleared] * tetLevel;
    tetLines += cleared;
    tetLevel = 1 + tetLines / 10;
  }
  tetSpawn();
}

static void tetrisReset() {
  memset(tetBoard, 0, sizeof tetBoard);
  tetScore = tetLines = 0;
  tetLevel = 1;
  tetOver = false;
  tetNextType = random(7);
  tetSpawn();
  tetTick = millis();
}

static void tetrisUpdate() {
  if (tetOver) return;
  int interval = 520 - (tetLevel - 1) * 45;
  if (interval < 90) interval = 90;
  if (millis() - tetTick >= (uint32_t)interval) {
    tetTick = millis();
    if (tetFits(tetMask, tetX, tetY + 1)) tetY++;
    else tetLock();
  }
}

static void tetrisKey(const KeyEvent& e) {
  if (e.kind == KK_ENTER && tetOver) { tetrisReset(); return; }
  if (tetOver) return;
  switch (e.kind) {
    case KK_LEFT:  if (tetFits(tetMask, tetX - 1, tetY)) tetX--; break;
    case KK_RIGHT: if (tetFits(tetMask, tetX + 1, tetY)) tetX++; break;
    case KK_DOWN:
      if (tetFits(tetMask, tetX, tetY + 1)) { tetY++; tetScore += 1; tetTick = millis(); }
      break;
    case KK_UP: {
      uint16_t r = tetRotate(tetMask);
      int offs[3] = {0, -1, 1};
      for (int k = 0; k < 3; k++) {
        if (tetFits(r, tetX + offs[k], tetY)) { tetMask = r; tetX += offs[k]; break; }
      }
      break;
    }
    case KK_ENTER:  // hard drop
      while (tetFits(tetMask, tetX, tetY + 1)) { tetY++; tetScore += 2; }
      tetLock();
      break;
    default: break;
  }
}

static void drawTetris() {
  drawWindowFrame("Tetris");
  const int bx = 8, by = CONTENT_T + 4;
  fb->fillRect(bx - 1, by - 1, TET_W * TET_CELL + 2, TET_H * TET_CELL + 2, COL_DGRAY);
  fb->fillRect(bx, by, TET_W * TET_CELL, TET_H * TET_CELL, COL_BLACK);
  for (int r = 0; r < TET_H; r++)
    for (int c = 0; c < TET_W; c++)
      if (tetBoard[r][c])
        fb->fillRect(bx + c * TET_CELL, by + r * TET_CELL, TET_CELL - 1, TET_CELL - 1, tetColor(tetBoard[r][c] - 1));
  for (int i = 0; i < 16; i++) {
    if (!((tetMask >> i) & 1)) continue;
    int cx = tetX + (i & 3), cy = tetY + (i >> 2);
    if (cy >= 0)
      fb->fillRect(bx + cx * TET_CELL, by + cy * TET_CELL, TET_CELL - 1, TET_CELL - 1, tetColor(tetType));
  }

  // side panel
  char n[16];
  const int px = 72;
  drawText(px, CONTENT_T + 4, "Score", COL_BLACK);
  snprintf(n, sizeof n, "%d", tetScore); drawText(px, CONTENT_T + 14, n, COL_NAVY);
  drawText(px, CONTENT_T + 30, "Lines", COL_BLACK);
  snprintf(n, sizeof n, "%d", tetLines); drawText(px, CONTENT_T + 40, n, COL_NAVY);
  drawText(px, CONTENT_T + 56, "Level", COL_BLACK);
  snprintf(n, sizeof n, "%d", tetLevel); drawText(px, CONTENT_T + 66, n, COL_NAVY);
  drawText(px, CONTENT_T + 80, "Next", COL_BLACK);
  for (int i = 0; i < 16; i++) {
    if (!((tetShapes[tetNextType] >> i) & 1)) continue;
    fb->fillRect(px + (i & 3) * 5, CONTENT_T + 92 + (i >> 2) * 5, 4, 4, tetColor(tetNextType));
  }

  if (tetOver) {
    fb->fillRect(4, CONTENT_T + 36, 58, 30, COL_WHITE);
    fb->drawRect(4, CONTENT_T + 36, 58, 30, COL_BLACK);
    drawText(12, CONTENT_T + 40, "GAME OVER", COL_RED);
    drawText(12, CONTENT_T + 54, "Enter=retry", COL_BLACK);
  }
}

// ---------------------- FILES (SD) ----------------------
static const int FILES_VISIBLE = 8;
static char filesPath[64] = "/";
static char filesNames[40][24];
static bool filesIsDir[40];
static uint32_t filesSize[40];
static int filesCount = 0, filesSel = 0, filesScroll = 0;
static bool sdOk = false;

static void filesLoad() {
  filesCount = 0; filesSel = 0; filesScroll = 0;
  if (!sdOk) return;
  if (strcmp(filesPath, "/") != 0) {
    strcpy(filesNames[0], "..");
    filesIsDir[0] = true; filesSize[0] = 0; filesCount = 1;
  }
  File d = SD.open(filesPath);
  if (!d || !d.isDirectory()) { d.close(); return; }
  while (filesCount < 40) {
    File f = d.openNextFile();
    if (!f) break;
    String nm = String(f.name());
    int sl = nm.lastIndexOf('/');
    if (sl >= 0) nm = nm.substring(sl + 1);
    strncpy(filesNames[filesCount], nm.c_str(), 23);
    filesNames[filesCount][23] = 0;
    filesIsDir[filesCount] = f.isDirectory();
    filesSize[filesCount] = filesIsDir[filesCount] ? 0 : (uint32_t)f.size();
    filesCount++;
    f.close();
  }
  d.close();
}

static void filesUp() {
  char* sl = strrchr(filesPath, '/');
  if (!sl || sl == filesPath) strcpy(filesPath, "/");
  else *sl = 0;
  filesLoad();
}

static void filesKey(const KeyEvent& e) {
  if (e.kind == KK_UP && filesSel > 0) filesSel--;
  else if (e.kind == KK_DOWN && filesSel < filesCount - 1) filesSel++;
  else if (e.kind == KK_ENTER && filesCount > 0 && filesIsDir[filesSel]) {
    if (strcmp(filesNames[filesSel], "..") == 0) { filesUp(); return; }
    char tmp[64];
    if (strcmp(filesPath, "/") == 0) snprintf(tmp, sizeof tmp, "/%s", filesNames[filesSel]);
    else snprintf(tmp, sizeof tmp, "%s/%s", filesPath, filesNames[filesSel]);
    strcpy(filesPath, tmp);
    filesLoad();
    return;
  }
  if (filesSel < filesScroll) filesScroll = filesSel;
  if (filesSel >= filesScroll + FILES_VISIBLE) filesScroll = filesSel - FILES_VISIBLE + 1;
}

static void drawFiles() {
  drawWindowFrame("Files");
  fb->fillRect(4, CONTENT_T + 2, SCR_W - 8, CONTENT_H - 4, COL_WHITE);
  fb->drawRect(4, CONTENT_T + 2, SCR_W - 8, CONTENT_H - 4, COL_DGRAY);

  int plen = strlen(filesPath);
  const char* p = (plen > 34) ? filesPath + plen - 34 : filesPath;
  drawText(8, CONTENT_T + 4, p, COL_NAVY);

  if (!sdOk) {
    drawText(10, CONTENT_T + 22, "No SD card found", COL_RED);
  } else if (filesCount == 0) {
    drawText(10, CONTENT_T + 22, "(empty)", COL_DGRAY);
  } else {
    for (int r = 0; r < FILES_VISIBLE; r++) {
      int i = filesScroll + r;
      if (i >= filesCount) break;
      int y = CONTENT_T + 16 + r * 10;
      bool sel = (i == filesSel);
      if (sel) fb->fillRect(6, y - 1, SCR_W - 12, 10, COL_NAVY);
      if (filesIsDir[i]) fb->fillRect(9, y + 1, 6, 4, rgb565(255, 200, 60));
      char lbl[30];
      snprintf(lbl, sizeof lbl, "%s%s", filesNames[i], filesIsDir[i] ? "/" : "");
      drawText(18, y, lbl, sel ? COL_WHITE : COL_BLACK);
    }
  }

  char st[40];
  if (!sdOk) snprintf(st, sizeof st, "Insert SD and reopen");
  else if (filesCount == 0) snprintf(st, sizeof st, "Items: 0");
  else if (filesIsDir[filesSel]) snprintf(st, sizeof st, "Folder  Items: %d", filesCount);
  else snprintf(st, sizeof st, "File  %lu bytes", (unsigned long)filesSize[filesSel]);
  drawText(8, CONTENT_B - 10, st, COL_BLACK);
}

// ---------------------- SETTINGS ----------------------
static uint8_t brightness = 255;
static Preferences prefs;   // NVS storage (saved brightness)

static void setBrightness(int v) {
  if (v < 8) v = 8;
  if (v > 255) v = 255;
  brightness = (uint8_t)v;
  M5Cardputer.Display.setBrightness(brightness);
  prefs.putUChar("bright", brightness);   // remember for next boot
}

static void settingsKey(const KeyEvent& e) {
  if (e.kind == KK_LEFT) setBrightness(brightness - 16);
  else if (e.kind == KK_RIGHT) setBrightness(brightness + 16);
}

static void drawSettings() {
  drawWindowFrame("Settings");
  drawText(10, CONTENT_T + 6, "Brightness", COL_BLACK);
  fb->fillRect(10, CONTENT_T + 18, 220, 12, COL_WHITE);
  fb->drawRect(10, CONTENT_T + 18, 220, 12, COL_DGRAY);
  int fillw = (brightness * 216) / 255;
  fb->fillRect(12, CONTENT_T + 20, fillw, 8, COL_NAVY);

  char b[32];
  snprintf(b, sizeof b, "Value: %d / 255", brightness);
  drawText(10, CONTENT_T + 34, b, COL_BLACK);
  drawText(10, CONTENT_T + 48, "Left / Right : adjust", COL_DGRAY);

  snprintf(b, sizeof b, "Battery: %d%%", batteryPct);
  drawText(10, CONTENT_T + 66, b, COL_BLACK);
  snprintf(b, sizeof b, "Voltage: %d mV", batteryMv);
  drawText(10, CONTENT_T + 78, b, COL_BLACK);
}

// ---------------------- SLEEP (deep sleep) ----------------------
static void goToDeepSleep() {
  M5Cardputer.Display.setBrightness(0);
  M5Cardputer.Display.fillScreen(COL_BLACK);
  delay(200);
  // Wake on BOOT button (GPIO0), pressed = LOW
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
  esp_deep_sleep_start();   // device restarts on wake, setup() reloads brightness
}

static void drawSleep() {
  drawWindowFrame("Sleep");
  drawText(30, CONTENT_T + 20, "Deep sleep", COL_BLACK);
  drawText(30, CONTENT_T + 36, "Enter = sleep now", COL_NAVY);
  drawText(30, CONTENT_T + 52, "Wake: BOOT button", COL_DGRAY);
  drawText(30, CONTENT_T + 64, "Fn+` = back", COL_DGRAY);
}

static void sleepKey(const KeyEvent& e) {
  if (e.kind == KK_ENTER) goToDeepSleep();
}

// ---------------------- APP LIFECYCLE / INPUT ----------------------
static void openAppNow(AppKind a) {
  openApp = a;
  switch (a) {
    case APP_CALC:
      calcLen = 0; calcBuf[0] = 0; calcResult = false; calcErr = false;
      break;
    case APP_SNAKE:  snakeReset(); break;
    case APP_TETRIS: tetrisReset(); break;
    case APP_FILES:
      if (!sdOk) sdOk = SD.begin(12, SPI, 40000000);  // retry SD on open
      strcpy(filesPath, "/");
      filesLoad();
      break;
    default: break;
  }
}

static void moveDeskSel(int dx, int dy) {
  int col = deskSel % 3 + dx;
  int row = deskSel / 3 + dy;
  if (col < 0 || col > 2 || row < 0) return;
  int ni = row * 3 + col;
  if (ni >= ICON_COUNT) return;
  deskSel = ni;
}

static void handleKey(const KeyEvent& e) {
  if (e.kind == KK_BACK) {
    if (openApp == APP_FILES && strcmp(filesPath, "/") != 0) filesUp();
    else openApp = APP_NONE;
    return;
  }
  if (openApp == APP_NONE) {
    switch (e.kind) {
      case KK_LEFT:  moveDeskSel(-1, 0); break;
      case KK_RIGHT: moveDeskSel(1, 0); break;
      case KK_UP:    moveDeskSel(0, -1); break;
      case KK_DOWN:  moveDeskSel(0, 1); break;
      case KK_ENTER: openAppNow(iconApp[deskSel]); break;
      default: break;
    }
    return;
  }
  switch (openApp) {
    case APP_CALC:     calcKey(e); break;
    case APP_SNAKE:    snakeKey(e); break;
    case APP_TETRIS:   tetrisKey(e); break;
    case APP_FILES:    filesKey(e); break;
    case APP_SETTINGS: settingsKey(e); break;
    case APP_SLEEP:    sleepKey(e); break;
    default: break;
  }
}

static void updateApp() {
  if (openApp == APP_SNAKE) snakeUpdate();
  else if (openApp == APP_TETRIS) tetrisUpdate();

  if (openApp == APP_NONE) {
    int x, y;
    iconPosition(deskSel, x, y);
    targetX = x + 14;
    targetY = y + 14;
  }
  // smooth glide toward target
  cursorX += (targetX - cursorX) * 0.22f;
  cursorY += (targetY - cursorY) * 0.22f;
}

static void drawFrame() {
  switch (openApp) {
    case APP_NONE:     drawDesktop(); break;
    case APP_CALC:     drawCalc(); break;
    case APP_SNAKE:    drawSnake(); break;
    case APP_TETRIS:   drawTetris(); break;
    case APP_FILES:    drawFiles(); break;
    case APP_SETTINGS: drawSettings(); break;
    case APP_SLEEP:    drawSleep(); break;
    default: break;
  }
  drawTaskbar();
  if (openApp == APP_NONE) drawCursor();
}

// ---------------------- SETUP / LOOP ----------------------
void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(0);

  // load saved brightness (default 255 on first boot)
  prefs.begin("win", false);
  brightness = prefs.getUChar("bright", 255);
  if (brightness < 8) brightness = 8;
  M5Cardputer.Display.setBrightness(brightness);

  fb = new M5Canvas(&M5Cardputer.Display);
  fb->setColorDepth(16);
  if (!fb->createSprite(SCR_W, SCR_H)) {
    M5Cardputer.Display.fillScreen(COL_BLACK);
    M5Cardputer.Display.setCursor(10, 10);
    M5Cardputer.Display.print("RAM error: canvas");
    while (true) delay(1000);
  }
  fb->setTextSize(1);

  // SD card pins for Cardputer ADV
  SPI.begin(40, 39, 14, 12);
  sdOk = SD.begin(12, SPI, 40000000);

  randomSeed(micros());

  cursorX = SCR_W / 2;
  cursorY = SCR_H / 2;
}

void loop() {
  pollKeys();
  for (int i = 0; i < keyCount; i++) handleKey(keyEvents[i]);
  updateApp();
  drawFrame();
  fb->pushSprite(0, 0);
  delay(15);
}
