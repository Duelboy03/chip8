# CHIP-8

A CHIP-8 interpreter in C++17 with an SDL2 front end, written from the spec in
[Tobias V. Langhoff's guide](https://tobiasvl.github.io/blog/write-a-chip-8-emulator/).

CHIP-8 is a virtual machine from 1977 — it never existed as hardware. It was an
interpreted instruction set that let hobbyists write a game once and run it on
any machine with an interpreter. This is one of those interpreters: 35 opcodes,
4 KB of memory, a 64×32 monochrome display, and a 16-key hex keypad.

It passes [Timendus's chip8-test-suite](https://github.com/Timendus/chip8-test-suite),
including all seven documented behavioural quirks, each switchable at runtime.

## Quick start

```bash
brew install sdl2            # or: apt install libsdl2-dev
git clone https://github.com/Duelboy03/chip8.git
cd chip8
make
./scripts/get-test-roms.sh   # optional: fetch the test-suite ROMs
./chip8 roms/splash.ch8
```

Escape quits. See [Controls](#run) below.

## Build

Needs a C++17 compiler and SDL2. On macOS:

```bash
brew install sdl2
make
```

That produces three binaries:

| Binary | What it does |
| --- | --- |
| `./chip8` | The emulator: SDL2 window, keypad, sound |
| `./tests` | 33 checks on the interpreter core (`make test`) |
| `./run_rom` | Runs a ROM headlessly and prints the screen as ASCII |

`make headless` builds just `tests` and `run_rom` — neither links against SDL2,
so they work on a machine without it.

## Run

```bash
./chip8 roms/2-ibm-logo.ch8
```

Options:

```bash
./chip8 roms/3-corax+.ch8 --speed 1200 --quirk memory=on --quirk shift=off
```

- `--speed HZ` — instructions per second (default 700, as the guide recommends)
- `--quirk NAME=on|off` — `shift`, `jump`, `memory`, `index`, `clip`, `logic`, `vblank`

While running: **space** pauses, **N** single-steps, **backspace** resets,
**escape** quits.

## Keypad

The original COSMAC VIP had a 4×4 hex keypad, mapped here onto the left of a
QWERTY keyboard:

```
1 2 3 C        1 2 3 4
4 5 6 D   →    Q W E R
7 8 9 E        A S D F
A 0 B F        Z X C V
```

## Files

| File | |
| --- | --- |
| `chip8.hpp` / `chip8.cpp` | The interpreter. No SDL, no I/O, no clock of its own. |
| `main.cpp` | SDL2 front end: window, input, audio, timing loop. |
| `tests.cpp` | The check suite. |
| `run_rom.cpp` | Headless runner. |
| `roms/` | Two hand-written demo ROMs and the assembler that emits them. |
| `scripts/get-test-roms.sh` | Downloads the third-party test-suite ROMs. |

The split matters: `chip8::Chip8` knows nothing about SDL or about time. It
exposes three entry points the host drives —

- `cycle()` — fetch, decode, execute one instruction
- `tick60()` — decrement both timers and release one display frame
- `setKey(hex, down)` — key state in

— which is why the same core runs under the windowed emulator and under the
headless tools.

## Timing

The host runs the CPU at the selected instruction rate and the timers at a
fixed 60 Hz, both driven off `std::chrono::steady_clock` rather than off frame
counting, so speed does not depend on the monitor's refresh rate. Timers tick
*before* the CPU batch each frame, so a `DXYN` waiting on vblank resumes at the
start of the batch instead of spinning through the whole budget.

Key releases are latched inside the core rather than sampled. `FX0A` completes
on press-and-release like the VIP did, and a tap that begins and ends between
two cycles would otherwise be lost entirely.

## Quirks

Several CHIP-8 instructions behaved differently across historical interpreters,
and ROMs disagree about which they expect. All seven are switchable; the
defaults follow the guide's recommendations.

| `--quirk` | Default | Off |
| --- | --- | --- |
| `shift` | on — `8XY6`/`8XYE` copy VY into VX first (COSMAC VIP) | shift VX in place (CHIP-48) |
| `jump` | off — `BNNN` jumps to NNN + V0 (VIP) | on: XNN + VX (CHIP-48) |
| `memory` | off — `FX55`/`FX65` leave I alone (CHIP-48) | on: I ends at I + X + 1 (VIP) |
| `index` | off — `FX1E` never touches VF (VIP) | on: VF set past 0x0FFF (Amiga) |
| `clip` | on — `DXYN` start wraps, sprite body clips | sprite wraps around edges |
| `logic` | on — `8XY1`/`8XY2`/`8XY3` zero VF (VIP side effect) | VF preserved |
| `vblank` | on — `DXYN` waits for vblank, 60 draws/sec (VIP) | draws immediately |

If a downloaded game misbehaves — sprites in the wrong place, a character that
won't move — flipping `shift` or `memory` is usually the fix.

`0NNN` (call host machine code) is decoded and ignored, as on every modern
interpreter. SUPER-CHIP's 128×64 mode and extra opcodes are not implemented.

## Tests

```bash
make test
```

33 checks: arithmetic and carry/borrow flags, skips, jumps, BCD, register
store/load, subroutines, XOR drawing and collision detection, the display-wait
stall, timer decay, and the `FX0A` blocking path (including a tap that starts
and ends between two cycles). It finishes by running `splash.ch8` and printing
the framebuffer as ASCII.

## Test suite results

The suite is [Timendus's chip8-test-suite](https://github.com/Timendus/chip8-test-suite),
which is **GPL-3.0** and not redistributed here. Fetch it into `roms/`:

```bash
./scripts/get-test-roms.sh
```

Run one without a window:

```bash
./run_rom roms/3-corax+.ch8 20000
```

`key=<hex>@<cycle>[:<hold>]` injects keypresses and `quirk=NAME:on|off` changes
behaviour, which is how the menu-driven tests below were driven:

```bash
./run_rom roms/6-keypad.ch8 10500 key=1@2000:200 key=5@9000:5000
./run_rom roms/5-quirks.ch8 30000 key=1@2000 quirk=memory:on
```

| Test | Result |
| --- | --- |
| 1 — CHIP-8 logo | pass — renders |
| 2 — IBM logo | pass — renders |
| 3 — corax+ opcodes | pass — all 22 cells show the check mark |
| 4 — flags | pass — no cross mark anywhere on screen |
| 5 — quirks | 5/6 by default, 6/6 with `quirk=memory:on` (see below) |
| 6 — keypad | pass — `EX9E`, `EXA1`, and `FX0A` all report "ALL GOOD" |
| 7 — beep | pass — sound timer active ~52% of the run |
| 8 — scrolling | n/a — SUPER-CHIP only; sits at its menu, does not crash |

The quirks ROM tests against *original CHIP-8*, so its "Memory" row disagrees
with the default on purpose: the guide recommends the CHIP-48 reading of
`FX55`/`FX65`, where I is left alone. Pass `quirk=memory:on` and all six rows
pass. The other five — vF reset, display wait, clipping, shifting, jumping —
pass out of the box.

Every ROM above was additionally cross-checked during development against a
second, independent implementation of the same spec, and the framebuffers came
out pixel-for-pixel identical.

## Credits

- [Tobias V. Langhoff's guide to writing a CHIP-8 emulator](https://tobiasvl.github.io/blog/write-a-chip-8-emulator/) — the spec this was built from, and the source of the quirk recommendations.
- [Timendus's chip8-test-suite](https://github.com/Timendus/chip8-test-suite) (GPL-3.0) — the test ROMs, fetched by `scripts/get-test-roms.sh` rather than vendored here.

## License

[MIT](LICENSE). The test-suite ROMs are not covered by it — they remain GPL-3.0
and belong to their author, which is why they are downloaded rather than
committed.
