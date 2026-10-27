# E-Reader — RP2040 ThinkInk + tri-color eInk + microSD

A battery-powered e-reader that reads plain-text `.txt` books off a microSD card
and displays them on a 4.2" tri-color eInk panel, with a 5-way button for
navigation. Built on an Adafruit RP2040 Feather ThinkInk.

Add books by dragging `.txt` files onto the SD card from any computer — no
reflashing needed.

---

## Hardware

| Part | ID | Notes |
|---|---|---|
| Adafruit RP2040 Feather ThinkInk (24-pin EPD) | PID 5727 | MCU + eInk driver + LiPo charger; eInk is on its **own** SPI bus (SPI1) |
| 4.2" 400×300 tri-color eInk panel (SSD1683) | PID 6382 | Bare panel, 24-pin FPC — plugs straight into the Feather |
| LiPo battery 3.7V | PID 328 | JST-PH |
| microSD breakout (SDIO/SPI, 3.3V-native) | pzsmocn | Pins: `CD D1 D2 D3 CLK CMD DO GND 3.3V` |
| microSD card | — | FAT32. **Must support SPI mode** — see Troubleshooting |
| 5-way nav button | — | Common → GND, 5 directions to GPIO |

### ⚠️ microSD card gotcha
The Mac/PC reads cards over **SDIO**; this breakout talks **SPI**. Some cards
(often generic/older ones) work fine in a computer but are **dead in SPI mode**.
If the card won't init, **try a different card (a name-brand SanDisk is reliable)**
before suspecting anything else. This bit us hard — one of our cards simply
didn't do SPI.

---

## Wiring

### microSD breakout → Feather (main SPI bus, SPI0)

| Breakout pin | SD function | → Feather pin | GPIO |
|---|---|---|---|
| `3.3V` | VDD | `3.3V` | — |
| `GND` | VSS | `GND` | — |
| `CLK` | SCK | `SCK` | 14 |
| `CMD` | MOSI (host→card) | `MO` | 15 |
| `DO`  | MISO (card→host) | `MI` | 8 |
| `D3`  | DAT3 / **CS** | `D5` | 5 |
| `D1`  | DAT1 (pull high) | `D24` | 24 |
| `D2`  | DAT2 (pull high) | `D25` | 25 |
| `CD`  | card-detect | — (unconnected) | — |

Notes:
- `CMD`→`MO` and `DO`→`MI` is the spot people swap. `DO` is the card's **output**
  (MISO), despite "DO" sounding like "data out of the host."
- `D1`/`D2` (DAT1/DAT2) are unused in SPI but **must be pulled high** or some
  cards won't enter SPI mode. The sketch sets GPIO24/25 as `INPUT_PULLUP`, so
  just wire `D1`→`D24` and `D2`→`D25`.
- The Feather has **two `3.3V` pins** right next to `GND`. Make sure `GND`
  actually lands on `GND`, not the second `3.3V` — a floating ground makes the
  card fail silently (and the breakout's power LED stays off). This was our
  single worst bug.

### 5-way button → Feather (pressed = LOW, internal pull-ups)

| Button | Feather pin | GPIO |
|---|---|---|
| COM (common) | `GND` | — |
| UP | `D11` | 11 |
| DOWN | `D10` | 10 |
| LEFT | `D6` | 6 |
| RIGHT | `D9` | 9 |
| CENTER (press) | `D12` | 12 |

### eInk panel
Slides directly into the Feather's 24-pin FPC connector — **no jumper wires.**
Gold contacts face the board; push fully in, latch down.

---

## Software

### Toolchain
- **Arduino IDE 2.x**
- **RP2040 board core** (Earle Philhower): add this Boards Manager URL, then
  install "Raspberry Pi Pico/RP2040":
  `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`
- **Board:** Tools → Board → **Adafruit Feather RP2040 ThinkInk**
- **Libraries** (Library Manager): `Adafruit EPD` (+ its deps: Adafruit GFX,
  Adafruit BusIO), `SD` and `SPI` ship with the core.

### Display driver
The 4.2" SSD1683 tri-color panel uses:
```cpp
ThinkInk_420_Tricolor_MFGNR display(EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY, EPD_SPI);
```
with `EPD_SPI = &SPI1` and `SRAM_CS = -1` (board pins `PIN_EPD_*`).

### Flashing
1. Open `ereader.ino`.
2. Upload (double-tap RESET to enter the bootloader if it doesn't auto-upload).
3. Press RESET. After a ~15s refresh you get the **LIBRARY** menu.

### Using it
- **Library menu:** UP/DOWN move the cursor, CENTER opens a book.
- **Reading:** RIGHT/LEFT = page, **UP/DOWN = previous/next chapter**, CENTER = back to library.
- Chapter jump scans each book on open for headings (`CHAPTER`/`PROLOGUE`/
  `EPILOGUE`/`LETTER `, case-insensitive). Books without those headings just
  won't jump — no harm.
- Books are any `.txt` files in the **root** of the SD card. Drag more on from a
  computer — no reflashing.

### Knobs
- `const int TS` (text size) near the top of the sketch: `1` = ~4× more text per
  page (smaller), `2` = larger/fewer words. Everything else (columns, rows,
  wrapping) recalculates automatically.

### Preparing book files
eInk font is plain ASCII. Convert smart quotes / em-dashes / accents to ASCII,
e.g.:
```bash
python3 -c 'import sys,unicodedata
m={"‘":"\x27","’":"\x27","“":"\x22","”":"\x22","—":"--","–":"-","…":"..."}
for p in sys.argv[1:]:
    t=open(p,encoding="utf-8",errors="ignore").read()
    for k,v in m.items(): t=t.replace(k,v)
    t=unicodedata.normalize("NFKD",t.replace("\r","")).encode("ascii","ignore").decode()
    open(p,"w").write(t)' book.txt
```
(EPUB? Convert to `.txt` first with Calibre or `pandoc book.epub -o book.txt`.)

---

## eInk refresh caveat
Tri-color (red/black/white) eInk does a **full ~15-second refresh that flashes**
on every page turn. That's the panel, not a bug — tri-color panels can't do fast
partial refreshes. A mono (black/white) 4.2" panel on the same 24-pin connector
would give ~1s flicker-free page turns if you ever want that (swap the panel +
change the driver class to a `ThinkInk_420_Mono_*`).

---

## Troubleshooting (hard-won)

`SD.begin` / CMD0 returns `0xFF` (card won't init). Check in this order:

1. **Ground.** Is `GND` really on `GND` (not the second `3.3V`)? The breakout's
   power LED should be lit. No ground = silent failure.
2. **Card.** Swap in a different card (name-brand). Many cards are fine in a PC
   but dead over SPI. Confirmed-good card is the fastest sanity check.
3. **`D1`/`D2` pulled high?** Floating DAT1/DAT2 makes some cards refuse SPI.
4. **`CMD`↔`DO` not swapped**, `CS` on `D3` (not `CD`), `D3`→Feather `5`.
5. **Card seated**, FAT32 formatted.

Low-level probe: a bare bit-banged CMD0 returning `0x01` means card + wiring are
good and it's a software/library issue; `0xFF` means the card isn't being reached
(power, ground, wiring, or card). A `MO`→`MI` loopback echoing `0xA5` proves the
Feather's SPI peripheral works.

---

## Files
- `ereader.ino` — the reader (SD + eInk + buttons)
- `data/` — sample books (if using the onboard-flash LittleFS variant instead of SD)

## Known-good config (as built)
- Board: Adafruit Feather RP2040 ThinkInk
- Panel driver: `ThinkInk_420_Tricolor_MFGNR`
- SD: SPI0 (SCK 14 / MOSI 15 / MISO 8 / CS 5), `D1`→24, `D2`→25 pulled high
- Buttons: UP 11, DOWN 10, LEFT 6, RIGHT 9, CENTER 12
- Card: use the SPI-working one (NOT the original flaky card)
