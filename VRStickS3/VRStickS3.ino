#include <M5Unified.h>
#include <HijelHID_BLEKeyboard.h>
#include <math.h>

HijelHID_BLEKeyboard keyboard("VRStickS3", "M5Stack", 100);

static bool lastConnected = false;
static bool lastPaired    = false;
static uint32_t lastSendMs = 0;
static const uint32_t SEND_COOLDOWN_MS = 250;

// Опрос батареи раз в 10 минут
static const uint32_t BATTERY_READ_INTERVAL_MS = 10UL * 60UL * 1000UL;

static String note = "BOOT";
static uint32_t noteUntil = 0;

static int batteryLevel = 0;
static uint32_t lastBatteryRead = 0;

// LED: обычно выключен, при нажатии синий на 1 секунду
static uint32_t ledBlueUntil = 0;
static const uint32_t LED_PULSE_MS = 1000;

// ---------- colors ----------
static const uint16_t BG          = 0xC618;
static const uint16_t BLACK_C     = 0x0000;
static const uint16_t WHITE_C     = 0xFFFF;
static const uint16_t HEADER_C    = 0x5AEB;
static const uint16_t GREEN_C     = 0x6D4D;
static const uint16_t BLUE_NEXT   = 0x0AF9;
static const uint16_t BLUE_NEXT_D = 0x08D7;
static const uint16_t BTN_GRAY    = 0x528A;
static const uint16_t BTN_GRAY2   = 0x4208;

// Winamp-style equalizer colors
static const uint16_t EQ_BG       = 0x0000;
static const uint16_t EQ_GRID     = 0x2104;
static const uint16_t EQ_GREEN_D  = 0x03A0;
static const uint16_t EQ_GREEN    = 0x07E0;
static const uint16_t EQ_LIME     = 0xAFE0;
static const uint16_t EQ_YELLOW   = 0xFFE0;
static const uint16_t EQ_ORANGE   = 0xFD20;
static const uint16_t EQ_PEAK     = 0xC618;

static const int IR_TX_PIN = 46;
static const int IR_RX_PIN = 42;

// ---------- equalizer ----------
static constexpr size_t EQ_N = 240;
static constexpr int EQ_BARS = 18;
static constexpr int EQ_SR = 18000;

static constexpr int EQ_GAP = 2;
static constexpr int EQ_SEG_H = 4;
static constexpr int EQ_SEG_GAP = 2;

static constexpr uint32_t EQ_FRAME_INTERVAL_MS = 35;
static constexpr uint32_t EQ_PEAK_DROP_MS = 35;

static const int EQ_AREA_Y = 74;
static const int EQ_AREA_H = 72;
static const int EQ_INNER_TOP_PAD = 5;
static const int EQ_INNER_BOTTOM_PAD = 5;

static int16_t eqSamples[EQ_N];
static int eqSmooth[EQ_BARS] = {0};
static int eqPeakHold[EQ_BARS] = {0};

static uint32_t lastEqFrame = 0;
static uint32_t lastEqPeakDrop = 0;

// ---------- power / peripherals ----------
void disableIR() {
  digitalWrite(IR_TX_PIN, LOW);
  pinMode(IR_TX_PIN, OUTPUT);

  pinMode(IR_RX_PIN, INPUT);

  M5.Power.setExtOutput(false);
}

// ---------- status LED ----------
void statusLedOff() {
  M5.Power.setLed(0);

  if (M5.Led.isEnabled() && M5.Led.getCount() > 0) {
    M5.Led.setBrightness(0);
    M5.Led.setColor(0, 0, 0, 0);
    M5.Led.display();
  }
}

void statusLedBlue(uint32_t ms = LED_PULSE_MS) {
  M5.Power.setLed(0);

  if (M5.Led.isEnabled() && M5.Led.getCount() > 0) {
    M5.Led.setBrightness(25);
    M5.Led.setColor(0, 0, 0, 255);
    M5.Led.display();
  }

  ledBlueUntil = millis() + ms;
}

void updateStatusLed() {
  if (ledBlueUntil != 0 && (int32_t)(millis() - ledBlueUntil) >= 0) {
    ledBlueUntil = 0;
    statusLedOff();
  }
}

// ---------- helpers ----------
void setNote(const char* text, uint32_t ms = 1200) {
  note = text;
  noteUntil = millis() + ms;
}

bool hasActiveNote() {
  return millis() < noteUntil;
}

bool cooldownOk() {
  uint32_t now = millis();

  if (now - lastSendMs < SEND_COOLDOWN_MS) {
    return false;
  }

  lastSendMs = now;
  return true;
}

// Читает батарею редко.
// Возвращает true, если значение реально обновилось.
bool updateBatteryInfo(bool force = false) {
  uint32_t now = millis();

  if (!force && now - lastBatteryRead < BATTERY_READ_INTERVAL_MS) {
    return false;
  }

  lastBatteryRead = now;

  batteryLevel = M5.Power.getBatteryLevel();

  if (batteryLevel < 0) batteryLevel = 0;
  if (batteryLevel > 100) batteryLevel = 100;

  return true;
}

// ---------- header battery ----------
void drawHeaderBatteryIcon(int x, int y, int w, int h, int level) {
  auto& d = M5.Display;

  d.drawRect(x, y, w, h, WHITE_C);
  d.drawRect(x + 1, y + 1, w - 2, h - 2, WHITE_C);

  int capW = 3;
  int capH = h / 2;

  d.fillRect(x + w, y + (h - capH) / 2, capW, capH, WHITE_C);

  int innerX = x + 3;
  int innerY = y + 3;
  int innerW = w - 6;
  int innerH = h - 6;

  int fillW = (innerW * level) / 100;

  if (fillW < 0) fillW = 0;
  if (fillW > innerW) fillW = innerW;

  d.fillRect(innerX, innerY, fillW, innerH, WHITE_C);
}

void drawHeader() {
  auto& d = M5.Display;
  int w = d.width();

  d.fillRoundRect(8, 8, w - 16, 22, 6, HEADER_C);

  if (hasActiveNote()) {
    d.setTextDatum(middle_center);
    d.setTextColor(WHITE_C, HEADER_C);
    d.setTextSize(1);
    d.drawString(note.c_str(), w / 2, 19);
    return;
  }

  char pct[10];
  snprintf(pct, sizeof(pct), "%d%%", batteryLevel);

  int iconW = 22;
  int iconH = 12;
  int textW = 48;
  int totalW = iconW + 8 + textW;

  int startX = (w - totalW) / 2;

  drawHeaderBatteryIcon(startX, 13, iconW, iconH, batteryLevel);

  d.setTextDatum(middle_left);
  d.setTextColor(WHITE_C, HEADER_C);
  d.setTextSize(2);
  d.drawString(pct, startX + iconW + 8, 19);
}

// ---------- UI ----------
void drawPill(int x, int y, int w, int h, const char* text, bool active) {
  auto& d = M5.Display;
  uint16_t fill = active ? GREEN_C : HEADER_C;

  d.fillRoundRect(x, y, w, h, 8, fill);
  d.setTextColor(WHITE_C, fill);
  d.setTextDatum(middle_center);
  d.setTextSize(2);
  d.drawString(text, x + w / 2, y + h / 2);
}

uint16_t eqSegmentColor(int seg, int maxSeg) {
  if (maxSeg <= 1) return EQ_GREEN;

  int t = (seg * 100) / (maxSeg - 1);

  if (t < 35) return EQ_GREEN_D;
  if (t < 65) return EQ_GREEN;
  if (t < 82) return EQ_LIME;
  if (t < 94) return EQ_YELLOW;
  return EQ_ORANGE;
}

void drawEqualizerWinamp() {
  auto& d = M5.Display;

  int screenW = d.width();

  // Эквалайзер на всю ширину экрана
  int areaX = 0;
  int areaY = EQ_AREA_Y;
  int areaW = screenW;
  int areaH = EQ_AREA_H;

  d.startWrite();

  d.fillRect(areaX, areaY, areaW, areaH, EQ_BG);

  // Серая точечная сетка, как у Winamp
  for (int yy = areaY + 3; yy < areaY + areaH - 2; yy += 4) {
    for (int xx = areaX + 2; xx < areaX + areaW - 2; xx += 4) {
      d.drawPixel(xx, yy, EQ_GRID);
    }
  }

  // Минимальные внутренние отступы
  int innerX = areaX + 2;
  int innerY = areaY + EQ_INNER_TOP_PAD;
  int innerW = areaW - 4;
  int innerH = areaH - EQ_INNER_TOP_PAD - EQ_INNER_BOTTOM_PAD;

  int barW = (innerW - (EQ_BARS - 1) * EQ_GAP) / EQ_BARS;
  if (barW < 3) barW = 3;

  int totalBarsW = EQ_BARS * barW + (EQ_BARS - 1) * EQ_GAP;
  int startX = innerX + (innerW - totalBarsW) / 2;

  int maxSeg = (innerH + EQ_SEG_GAP) / (EQ_SEG_H + EQ_SEG_GAP);
  if (maxSeg < 1) maxSeg = 1;

  for (int b = 0; b < EQ_BARS; b++) {
    int bx = startX + b * (barW + EQ_GAP);

    int h = eqSmooth[b];

    if (h < 0) h = 0;
    if (h > innerH) h = innerH;

    int filledSeg = (h * maxSeg + innerH - 1) / innerH;

    if (filledSeg < 0) filledSeg = 0;
    if (filledSeg > maxSeg) filledSeg = maxSeg;

    for (int s = 0; s < filledSeg; s++) {
      int sy = innerY + innerH - EQ_SEG_H - s * (EQ_SEG_H + EQ_SEG_GAP);

      if (sy < innerY) sy = innerY;

      uint16_t c = eqSegmentColor(s, maxSeg);
      d.fillRect(bx, sy, barW, EQ_SEG_H, c);
    }

    int ph = eqPeakHold[b];

    if (ph > 1) {
      if (ph > innerH) ph = innerH;

      int py = innerY + innerH - ph;

      if (py < innerY) py = innerY;
      if (py > innerY + innerH - 2) py = innerY + innerH - 2;

      d.fillRect(bx, py, barW, 2, EQ_PEAK);
    }
  }

  d.endWrite();
}

void updateEqualizer() {
  uint32_t now = millis();

  if (now - lastEqFrame < EQ_FRAME_INTERVAL_MS) {
    return;
  }

  lastEqFrame = now;

  if (!M5.Mic.record(eqSamples, EQ_N, EQ_SR, false)) {
    return;
  }

  int innerH = EQ_AREA_H - EQ_INNER_TOP_PAD - EQ_INNER_BOTTOM_PAD;
  int chunk = EQ_N / EQ_BARS;

  for (int b = 0; b < EQ_BARS; b++) {
    int32_t peak = 0;

    int start = b * chunk;
    int end = start + chunk;

    for (int i = start; i < end; i++) {
      int32_t v = eqSamples[i];

      if (v < 0) v = -v;
      if (v > peak) peak = v;
    }

    int32_t shaped = (int32_t)sqrt((double)peak) * 181;

    int h = (int)((shaped * innerH) / 32767.0f);

    if (h < 0) h = 0;
    if (h > innerH) h = innerH;

    eqSmooth[b] = (eqSmooth[b] * 5 + h * 3) / 8;

    if (eqSmooth[b] > eqPeakHold[b]) {
      eqPeakHold[b] = eqSmooth[b];
    }
  }

  if (now - lastEqPeakDrop > EQ_PEAK_DROP_MS) {
    for (int b = 0; b < EQ_BARS; b++) {
      if (eqPeakHold[b] > 0) {
        eqPeakHold[b]--;
      }
    }

    lastEqPeakDrop = now;
  }

  drawEqualizerWinamp();
}

void drawPlayPauseButton(int x, int y, int w, int h) {
  auto& d = M5.Display;

  int realW = w + 22;

  d.fillRoundRect(x, y, realW, h, 10, BTN_GRAY);
  d.fillRoundRect(x + 2, y + 2, realW - 4, h - 4, 9, BTN_GRAY2);

  d.setTextDatum(middle_center);
  d.setTextColor(WHITE_C, BTN_GRAY2);
  d.setTextSize(2);

  d.drawString("Play/Pause", x + (w / 2) + 7, y + h / 2);
}

void drawNextButton(int x, int y, int w, int visibleH) {
  auto& d = M5.Display;

  int fullH = visibleH + 12;

  d.fillRoundRect(x, y, w, fullH, 12, BLUE_NEXT);
  d.fillRoundRect(x + 2, y + 2, w - 4, fullH - 4, 10, BLUE_NEXT_D);

  d.setTextDatum(middle_center);
  d.setTextColor(WHITE_C, BLUE_NEXT_D);
  d.setTextSize(3);

  int centerY = y + visibleH / 2 + 2;
  d.drawString("NEXT", x + w / 2, centerY);
}

void drawUI() {
  auto& d = M5.Display;

  int w = d.width();
  int h = d.height();

  d.fillScreen(BG);

  drawHeader();

  bool bt  = keyboard.isConnected();
  bool hid = keyboard.isPaired();

  drawPill(10, 42, (w - 30) / 2, 20, "BT", bt);
  drawPill(20 + (w - 30) / 2, 42, (w - 30) / 2, 20, "HID", hid);

  drawEqualizerWinamp();

  drawPlayPauseButton(8, 156, w - 20, 34);
  drawNextButton(10, 202, w - 20, h - 202);
}

// ---------- media controls ----------
void sendNext() {
  if (!cooldownOk()) return;

  if (!keyboard.isPaired()) {
    setNote("PAIR DEVICE", 1600);
    drawUI();
    return;
  }

  statusLedBlue();

  keyboard.tap(MEDIA_NEXT_TRACK);

  setNote("NEXT send", 900);
  drawUI();
}

void sendPlayPause() {
  if (!cooldownOk()) return;

  if (!keyboard.isPaired()) {
    setNote("PAIR DEVICE", 1600);
    drawUI();
    return;
  }

  statusLedBlue();

  keyboard.tap(MEDIA_PLAY_PAUSE);

  setNote("Pause/Resume send", 900);
  drawUI();
}

// ---------- setup / loop ----------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Led.begin();
  statusLedOff();

  disableIR();

  M5.Display.setRotation(0);
  M5.Display.setBrightness(60);

  // Микрофон для эквалайзера.
  // Speaker выключаем, чтобы не конфликтовал с Mic на Stick S3.
  M5.Speaker.end();
  M5.Mic.begin();

  M5.update();

  bool clearPairing = M5.BtnB.isPressed();

  keyboard.setLogLevel(HIDLogLevel::Off);

  // Чем меньше значение, тем меньше мощность BLE.
  // Если связь станет нестабильной — попробуй 3 или 4.
  keyboard.setTxPower(2);

  keyboard.setTapDelay(60);
  keyboard.setKeyGap(60);

  if (clearPairing) {
    keyboard.begin();
    delay(300);
    keyboard.clearBonds();

    updateBatteryInfo(true);

    setNote("BONDS CLEARED", 1500);
    drawUI();

    delay(1500);
    ESP.restart();
  }

  keyboard.begin();

  updateBatteryInfo(true);

  setNote("READY", 1000);
  drawUI();

  lastConnected = keyboard.isConnected();
  lastPaired = keyboard.isPaired();

  statusLedOff();
}

void loop() {
  M5.update();

  if (ledBlueUntil == 0) {
    M5.Power.setLed(0);
  }

  if (M5.BtnA.wasPressed()) {
    sendNext();
  }

  if (M5.BtnB.wasPressed()) {
    sendPlayPause();
  }

  bool c = keyboard.isConnected();
  bool p = keyboard.isPaired();

  if (c != lastConnected || p != lastPaired) {
    lastConnected = c;
    lastPaired = p;

    if (p) {
      setNote("HID READY", 1000);
    } else if (c) {
      setNote("BT CONNECTED", 1000);
    } else {
      setNote("ADVERTISING", 1000);
    }

    drawUI();
  }

  // Реальное состояние аккумулятора читается редко — раз в 10 минут.
  if (updateBatteryInfo(false)) {
    drawUI();
  }

  static bool redrawAfterNote = false;

  if (hasActiveNote()) {
    redrawAfterNote = true;
  } else if (redrawAfterNote) {
    redrawAfterNote = false;
    drawUI();
  }

  updateEqualizer();
  updateStatusLed();

  delay(10);
}