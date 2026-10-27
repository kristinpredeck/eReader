/*
 * SD-card e-reader for Adafruit RP2040 Feather ThinkInk
 * + 4.2" 400x300 tri-color SSD1683 panel (PID 6382)
 * + 3.3V microSD breakout on SPI
 * + 5-way nav button (common -> GND)
 *
 * Books: plain-text .txt files in the ROOT of a FAT32 microSD card.
 * Drag new books onto the card from any computer - no reflashing.
 *
 * Reading controls: RIGHT/LEFT = page, UP/DOWN = chapter, CENTER = library.
 * NOTE: tri-color eInk does a full ~15s refresh on every page.
 */

#include <SPI.h>
#include <SD.h>
#include <vector>
#include "Adafruit_ThinkInk.h"
#include <Fonts/FreeSerif9pt7b.h>   // body font - swap for FreeSans9pt7b for sans-serif

// ---------- display ----------
#define EPD_DC    PIN_EPD_DC
#define EPD_CS    PIN_EPD_CS
#define EPD_BUSY  PIN_EPD_BUSY
#define SRAM_CS   -1
#define EPD_RESET PIN_EPD_RESET
#define EPD_SPI   &SPI1
ThinkInk_420_Tricolor_MFGNR display(EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY, EPD_SPI);

// ---------- SD card (main SPI bus) ----------
#define SD_CS   5     // D5 -> breakout D3 (CS). SCK=14, MOSI=15(->CMD), MISO=8(->DO), D1->24, D2->25

// ---------- 5-way button (pressed = LOW, common to GND) ----------
#define BTN_UP    11    // D11 - previous chapter
#define BTN_DOWN  10    // D10 - next chapter
#define BTN_LEFT  6     // D6  - previous page
#define BTN_RIGHT 9     // D9  - next page
#define BTN_SEL   12    // D12 - open / back

// ---------- body text (real font, the readable middle size) ----------
const GFXfont *BODY_FONT = &FreeSerif9pt7b;
const int LINE_H  = 18;   // line spacing in px: smaller = MORE lines/page (try 16-22)
const int MX = 4, MY = 4; // margins

// ---------- menu (built-in font, size 2) ----------
const int MTS = 2;
const int MCW = 6 * MTS, MCH = 8 * MTS;

enum Mode { MENU, READING, SLEEP };
Mode mode = MENU;

std::vector<String> books;
int sel = 0;

File book;
long curStart = 0;
long nextStart = -1;
std::vector<long> history;    // page-start offsets for "previous page"
std::vector<long> chapters;   // byte offsets of chapter headings

// Returns 1=up 2=down 3=left 4=right 5=center(tap) 6=center(hold ~1s) 0=none
int readButton() {
  const int pins[5]  = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_SEL};
  const int codes[5] = {1, 2, 3, 4, 5};
  for (int i = 0; i < 5; i++) {
    if (digitalRead(pins[i]) == LOW) {
      delay(25);
      if (digitalRead(pins[i]) == LOW) {
        unsigned long t0 = millis();
        while (digitalRead(pins[i]) == LOW) delay(5);   // wait for release
        unsigned long held = millis() - t0;
        delay(25);
        if (pins[i] == BTN_SEL && held > 1000) return 6; // long-press center = power toggle
        return codes[i];
      }
    }
  }
  return 0;
}

// True pen advance (not ink width) for a string in BODY_FONT. Prevents
// letters/words from crowding and overlapping.
int advance(const String &s) {
  int total = 0;
  for (unsigned i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (c < BODY_FONT->first || c > BODY_FONT->last) continue;
    total += BODY_FONT->glyph[c - BODY_FONT->first].xAdvance;
  }
  return total;
}
int wordWidth(const String &w) { return advance(w); }
int bodySpaceWidth() { return advance(" "); }

// Lay out one page from byte `start` using the proportional body font.
// Returns next page's byte offset, or -1 at EOF.
long layoutPage(File &f, long start, bool draw) {
  f.seek(start);
  if (draw) {
    display.clearBuffer();
    display.setFont(BODY_FONT);
    display.setTextSize(1);        // custom font at native size (menu leaves it at 2)
    display.setTextColor(EPD_BLACK);
  }
  const int usableW = display.width() - 2 * MX;
  const int maxRows = (display.height() - 2 * MY) / LINE_H;
  const int sp = bodySpaceWidth();
  int x = 0, row = 0;
  bool lineHasContent = false;

  while (true) {
    long p = f.position();
    int c = f.read();
    if (c < 0) return -1;
    if (c == '\r') continue;
    if (c == '\n') {
      row++; x = 0; lineHasContent = false;
      if (row >= maxRows) return f.position();
      continue;
    }
    if (c == ' ' || c == '\t') {
      if (lineHasContent) x += sp;
      continue;
    }
    long wordStart = p;
    String w; w += (char)c;
    while (true) {
      long q = f.position();
      int d = f.read();
      if (d < 0) break;
      if (d == '\r') continue;
      if (d == '\n' || d == ' ' || d == '\t') { f.seek(q); break; }
      w += (char)d;
    }
    int ww = wordWidth(w);
    if (x + ww > usableW) {                 // wrap to next line
      row++; x = 0; lineHasContent = false;
      if (row >= maxRows) return wordStart;
    }
    if (draw) {
      int baseline = MY + (row + 1) * LINE_H - 4;   // custom-font cursor = baseline
      display.setCursor(MX + x, baseline);
      display.print(w);
    }
    x += ww;
    lineHasContent = true;
  }
}

void scanChapters(File &f) {
  chapters.clear();
  f.seek(0);
  long lineStart = 0;
  String line;
  while (true) {
    int c = f.read();
    bool eof = (c < 0);
    if (c == '\n' || eof) {
      String u = line; u.trim(); u.toUpperCase();
      if (u.startsWith("CHAPTER") || u.startsWith("PROLOGUE") ||
          u.startsWith("EPILOGUE") || u.startsWith("LETTER ")) {
        if (chapters.size() < 400) chapters.push_back(lineStart);
      }
      line = "";
      lineStart = f.position();
      if (eof) break;
    } else if (c != '\r' && line.length() < 48) {
      line += (char)c;
    }
  }
}

void renderPage() {
  nextStart = layoutPage(book, curStart, true);
  display.display();
}

void clearScreen() {        // blank to white (storage-safe) and go idle
  display.clearBuffer();
  display.display();
}

void jumpChapter(int dir) {
  if (chapters.empty()) return;
  long target = -1;
  if (dir > 0) {
    for (long off : chapters) if (off > curStart) { target = off; break; }
  } else {
    for (int i = (int)chapters.size() - 1; i >= 0; i--)
      if (chapters[i] < curStart) { target = chapters[i]; break; }
  }
  if (target >= 0) { history.clear(); curStart = target; renderPage(); }
}

void drawMenu() {
  display.clearBuffer();
  display.setFont(NULL);          // built-in font for the menu
  display.setTextSize(MTS);
  display.setTextColor(EPD_RED);
  display.setCursor(MX, MY);
  display.print("== LIBRARY ==");
  display.setTextColor(EPD_BLACK);
  if (books.empty()) {
    display.setCursor(MX, MY + 2 * MCH);
    display.print("No .txt files on card.");
  }
  for (int i = 0; i < (int)books.size(); i++) {
    display.setCursor(MX, MY + (i + 2) * MCH);
    display.print(i == sel ? "> " : "  ");
    display.print(books[i]);
  }
  display.setTextColor(EPD_RED);
  display.setCursor(MX, MY + 13 * MCH);
  display.print("UP/DN pick  SEL open");
  display.display();
}

void openBook(int i) {
  book = SD.open(("/" + books[i]).c_str());
  scanChapters(book);
  history.clear();
  curStart = 0;
  mode = READING;
  renderPage();
}

void showError(const char *l1, const char *l2) {
  display.clearBuffer();
  display.setFont(NULL);
  display.setTextSize(MTS);
  display.setTextColor(EPD_RED);
  display.setCursor(MX, MY);         display.print(l1);
  display.setTextColor(EPD_BLACK);
  display.setCursor(MX, MY + 2*MCH); display.print(l2);
  display.display();
}

void loadFileList() {
  books.clear();
  File root = SD.open("/");
  while (true) {
    File e = root.openNextFile();
    if (!e) break;
    if (!e.isDirectory()) {
      String n = e.name();
      int slash = n.lastIndexOf('/');
      if (slash >= 0) n = n.substring(slash + 1);
      String low = n; low.toLowerCase();
      if (!n.startsWith(".") && low.endsWith(".txt")) books.push_back(n);
    }
    e.close();
  }
  root.close();
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_SEL, INPUT_PULLUP);
  pinMode(24, INPUT_PULLUP);   // D1 (DAT1) pull-up
  pinMode(25, INPUT_PULLUP);   // D2 (DAT2) pull-up

  display.begin(THINKINK_TRICOLOR);

  SPI.setSCK(14); SPI.setTX(15); SPI.setRX(8); SPI.begin();
  if (!SD.begin(SD_CS, SD_SCK_MHZ(4))) {
    showError("SD init failed.", "Check wiring / card.");
    return;
  }
  loadFileList();
  drawMenu();
}

void loop() {
  int b = readButton();
  if (!b) { delay(15); return; }

  if (b == 6) {                 // hold CENTER anywhere = "off": blank screen, go idle
    if (mode == READING) book.close();
    clearScreen();
    mode = SLEEP;
    return;
  }
  if (mode == SLEEP) {          // any press = "on": back to the library
    mode = MENU;
    drawMenu();
    return;
  }

  if (mode == MENU) {
    if (books.empty()) return;
    if (b == 1) { sel = (sel - 1 + books.size()) % books.size(); drawMenu(); }
    else if (b == 2) { sel = (sel + 1) % books.size(); drawMenu(); }
    else if (b == 5) { openBook(sel); }
  } else { // READING
    if (b == 4 && nextStart >= 0) {            // RIGHT: next page
      history.push_back(curStart);
      curStart = nextStart;
      renderPage();
    } else if (b == 3 && !history.empty()) {   // LEFT: previous page
      curStart = history.back();
      history.pop_back();
      renderPage();
    } else if (b == 1) {                       // UP: previous chapter
      jumpChapter(-1);
    } else if (b == 2) {                       // DOWN: next chapter
      jumpChapter(+1);
    } else if (b == 5) {                       // CENTER: back to library
      book.close();
      mode = MENU;
      drawMenu();
    }
  }
}
