# E-Reader (RP2040 ThinkInk + tri-color eInk + microSD)

A battery-powered e-reader that reads plain-text `.txt` books off a microSD card
and shows them on a 4.2" tri-color eInk panel. A 5-way button handles
navigation. Built on an Adafruit RP2040 Feather ThinkInk.

To add books, drop `.txt` files onto the SD card from any computer. No reflashing.

## Hardware

| Part | ID | Notes |
|---|---|---|
| Adafruit RP2040 Feather ThinkInk (24-pin EPD) | PID 5727 | MCU, eInk driver, LiPo charger. The eInk sits on its own SPI bus (SPI1). |
| 4.2" 400x300 tri-color eInk panel (SSD1683) | PID 6382 | Bare panel, 24-pin FPC, plugs straight into the Feather. |
| LiPo battery 3.7V | PID 328 | JST-PH. |
| microSD breakout (SDIO/SPI, 3.3V) | pzsmocn | Pins: `CD D1 D2 D3 CLK CMD DO GND 3.3V`. |
| microSD card | | FAT32. Must support SPI mode (see Troubleshooting). |
| 5-way nav button | | Common to GND, 5 directions to GPIO. |

### microSD card warning
Your computer reads cards over SDIO, but this breakout talks SPI. Some cards
(often generic or older ones) read fine in a computer and are completely dead
over SPI. If a card won't init, try a different one before suspecting anything
else. A name-brand SanDisk is a reliable choice. One of the cards used here
simply didn't do SPI, which cost a lot of debugging time.

## Wiring

### microSD breakout to Feather (main SPI bus, SPI0)

| Breakout pin | SD function | Feather pin | GPIO |
|---|---|---|---|
| `3.3V` | VDD | `3.3V` | |
| `GND` | VSS | `GND` | |
| `CLK` | SCK | `SCK` | 14 |
| `CMD` | MOSI (host to card) | `MO` | 15 |
| `DO`  | MISO (card to host) | `MI` | 8 |
| `D3`  | DAT3 / CS | `D5` | 5 |
| `D1`  | DAT1 (pull high) | `D24` | 24 |
| `D2`  | DAT2 (pull high) | `D25` | 25 |
| `CD`  | card-detect | unconnected | |

Notes:
- `CMD` goes to `MO` and `DO` goes to `MI`. This is the easy one to get
  backwards. `DO` is the card's output (MISO), even though "DO" sounds like it
  should be the host's output.
- `D1` and `D2` (DAT1/DAT2) aren't used in SPI, but some cards won't enter SPI
  mode unless those lines are pulled high. The sketch sets GPIO24/25 as
  `INPUT_PULLUP`, so just wire `D1` to `D24` and `D2` to `D25`.
- The Feather has two `3.3V` pins right next to `GND`. Make sure `GND` lands on
  `GND` and not the second `3.3V`. A floating ground makes the card fail
  silently (and the breakout's power LED stays dark). This was the worst bug in
  the whole build.

### 5-way button to Feather (pressed = LOW, internal pull-ups)

| Button | Feather pin | GPIO |
|---|---|---|
| COM (common) | `GND` | |
| UP | `D11` | 11 |
| DOWN | `D10` | 10 |
| LEFT | `D6` | 6 |
| RIGHT | `D9` | 9 |
| CENTER (press) | `D12` | 12 |

### eInk panel
Slides straight into the Feather's 24-pin FPC connector. No jumper wires. Gold
contacts face the board, push it fully in, latch it down.

## Software

### Toolchain
- Arduino IDE 2.x
- RP2040 board core by Earle Philhower. Add this Boards Manager URL, then install
  "Raspberry Pi Pico/RP2040":
  `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`
- Board: Tools > Board > Adafruit Feather RP2040 ThinkInk
- Libraries (Library Manager): `Adafruit EPD` plus its dependencies (Adafruit
  GFX, Adafruit BusIO). `SD` and `SPI` ship with the core. The body font
  `FreeSerif9pt7b` is built into Adafruit GFX.

### Display driver
The 4.2" SSD1683 tri-color panel uses:
```cpp
ThinkInk_420_Tricolor_MFGNR display(EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY, EPD_SPI);
```
with `EPD_SPI = &SPI1` and `SRAM_CS = -1` (board pins `PIN_EPD_*`).

### Flashing
1. Open `ereader.ino`.
2. Upload. Double-tap RESET to enter the bootloader if it doesn't auto-upload.
3. Press RESET. After a ~15s refresh you get the LIBRARY menu.

### Controls
- Library menu: UP/DOWN move the cursor, CENTER opens a book.
- Reading: RIGHT/LEFT turn pages, UP/DOWN jump to the previous/next chapter,
  CENTER returns to the library.
- Hold CENTER for about a second to blank the screen (idle). Any press wakes it
  back to the library.
- Books are `.txt` files in the root of the SD card. Chapter jump works on books
  whose chapters start with `CHAPTER`, `PROLOGUE`, `EPILOGUE`, or `LETTER `.
  Books without those headings just won't jump, which is harmless.

### Knobs (top of the sketch)
- `LINE_H` sets line spacing. Lower packs more lines per page, higher is airier.
- `BODY_FONT` is the reading font. Swap the `FreeSerif9pt7b` include and constant
  for `FreeSans9pt7b` if you want sans-serif, or a larger point size for bigger
  text.

### Preparing book files
The font is plain ASCII, so convert smart quotes, em-dashes, and accents to ASCII
first. For a plain `.txt`:
```bash
python3 -c 'import sys,unicodedata
m={"‘":"\x27","’":"\x27","“":"\x22","”":"\x22","—":"--","–":"-","…":"..."}
for p in sys.argv[1:]:
    t=open(p,encoding="utf-8",errors="ignore").read()
    for k,v in m.items(): t=t.replace(k,v)
    t=unicodedata.normalize("NFKD",t.replace("\r","")).encode("ascii","ignore").decode()
    open(p,"w").write(t)' book.txt
```
For HTML or EPUB, convert to text first (Calibre, or `pandoc book.epub -o book.txt`),
then run the cleanup above. Text that still has non-ASCII characters shows up as
garbage in the serif font, since that font only covers plain ASCII.

## About the slow refresh
Tri-color (red/black/white) eInk does a full refresh that flashes and takes about
15 seconds on every page turn. That's the panel, not the code. Tri-color panels
can't do fast partial refreshes. The chip is also busy during the refresh, so
button presses made while it's flashing are ignored. Press once, wait for the
flash to finish, then press again. A mono black-and-white 4.2" panel on the same
connector would give roughly 1-second flicker-free page turns if that matters
more than color.

## Troubleshooting
`SD.begin` or a CMD0 probe returns `0xFF` (card won't init). Check in this order:

1. Ground. Is `GND` really on `GND` and not the second `3.3V`? The breakout's
   power LED should be lit. No ground means a silent failure.
2. Card. Swap in a different card, ideally a name-brand one. Plenty of cards
   work in a computer but are dead over SPI.
3. `D1`/`D2` pulled high? Floating DAT1/DAT2 makes some cards refuse SPI.
4. `CMD` and `DO` not swapped, CS on `D3` (not `CD`), `D3` wired to Feather `5`.
5. Card actually seated, formatted FAT32.

Useful probes:
- A bare bit-banged CMD0 that returns `0x01` means the card and wiring are good
  and the problem is software. `0xFF` means the card isn't being reached (power,
  ground, wiring, or the card itself).
- A `MO`-to-`MI` loopback that echoes `0xA5` proves the Feather's SPI works. Note
  this does not prove the `SCK` pin clocks externally, since a loopback shifts
  data internally.

## Files
- `ereader.ino` is the reader (SD, eInk, buttons).
- `data/` holds sample public-domain books for the onboard-flash (LittleFS)
  variant, if you use that instead of SD.

## Known-good setup (as built)
- Board: Adafruit Feather RP2040 ThinkInk
- Panel driver: `ThinkInk_420_Tricolor_MFGNR`
- SD on SPI0: SCK 14, MOSI 15, MISO 8, CS 5, with `D1` to 24 and `D2` to 25
  pulled high
- Buttons: UP 11, DOWN 10, LEFT 6, RIGHT 9, CENTER 12
- Use a card that actually works over SPI
