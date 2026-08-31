// Headless checks for the interpreter core: `make test`.
#include "chip8.hpp"

#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(const std::string& name, bool ok, const std::string& detail = "") {
    if (ok) std::printf("  pass  %s\n", name.c_str());
    else {
        ++failures;
        std::printf("  FAIL  %s %s\n", name.c_str(), detail.c_str());
    }
}

std::vector<std::uint8_t> assemble(std::initializer_list<std::uint16_t> words) {
    std::vector<std::uint8_t> rom;
    rom.reserve(words.size() * 2);
    for (std::uint16_t w : words) {
        rom.push_back(static_cast<std::uint8_t>(w >> 8));
        rom.push_back(static_cast<std::uint8_t>(w & 0xFF));
    }
    return rom;
}

// Runs a short program. A frame is always available so the display-wait quirk
// never stalls these opcode tests; pacing gets its own section below.
chip8::Chip8 run(std::initializer_list<std::uint16_t> words, int cycles = -1) {
    const auto rom = assemble(words);
    chip8::Chip8 chip;
    chip.loadRom(rom);
    const int steps = cycles < 0 ? static_cast<int>(words.size()) : cycles;
    for (int i = 0; i < steps; ++i) {
        chip.grantFrame();
        chip.cycle();
    }
    return chip;
}

int litPixels(const chip8::Chip8& chip) {
    int count = 0;
    for (std::uint8_t p : chip.display()) count += p;
    return count;
}

}  // namespace

int main() {
    std::printf("opcodes\n");
    check("6XNN / 7XNN", run({0x600A, 0x7005}).v(0) == 15);
    {
        auto c = run({0x60FF, 0x7001});
        check("7XNN wraps without touching VF", c.v(0) == 0x00 && c.v(0xF) == 0);
    }
    {
        auto c = run({0x60FF, 0x6101, 0x8014});
        check("8XY4 sets carry", c.v(0) == 0x00 && c.v(0xF) == 1);
    }
    {
        auto c = run({0x6001, 0x6102, 0x8015});
        check("8XY5 clears VF on borrow", c.v(0) == 0xFF && c.v(0xF) == 0);
    }
    {
        auto c = run({0x6001, 0x6102, 0x8017});
        check("8XY7 reverse subtract", c.v(0) == 0x01 && c.v(0xF) == 1);
    }
    {
        auto c = run({0x6105, 0x8016});
        check("8XY6 shifts VY (VIP)", c.v(0) == 0x02 && c.v(0xF) == 1);
    }
    {
        chip8::Quirks modern;
        modern.shiftUsesVY = false;
        chip8::Chip8 c(modern);
        c.loadRom(assemble({0x6005, 0x6100, 0x8016}));
        for (int i = 0; i < 3; ++i) c.cycle();
        check("8XY6 shifts VX in place (CHIP-48)", c.v(0) == 0x02 && c.v(0xF) == 1);
    }
    check("3XNN skips", run({0x600A, 0x300A, 0x6001}).v(0) == 10);
    check("4XNN does not skip when equal", run({0x600A, 0x400A, 0x6001}).v(0) == 1);
    {
        auto c = run({0xA200, 0x6010, 0xF01E});
        check("ANNN / FX1E", c.index() == 0x210);
    }
    {
        auto c = run({0x609C, 0xA300, 0xF033});
        check("FX33 packs BCD",
              c.memory(0x300) == 1 && c.memory(0x301) == 5 && c.memory(0x302) == 6);
    }
    {
        auto c = run({0x6001, 0x6102, 0x6203, 0xA400, 0xF255, 0x6200, 0xF265});
        check("FX55/FX65 round-trip, I unchanged", c.v(2) == 3 && c.index() == 0x400);
    }
    {
        chip8::Quirks vip;
        vip.memoryIncrementsI = true;
        chip8::Chip8 c(vip);
        c.loadRom(assemble({0xA400, 0xF255}));
        c.cycle();
        c.cycle();
        check("FX55 advances I when the VIP quirk is on", c.index() == 0x403);
    }
    {
        auto c = run({0x2206, 0x1204, 0x1204, 0x6007, 0x00EE}, 5);
        check("2NNN/00EE call and return", c.v(0) == 7 && c.stackDepth() == 0);
    }
    check("BNNN adds V0", run({0x6004, 0xB202, 0x0000, 0x6009}, 3).v(0) == 9);
    {
        auto c = run({0x00EE}, 1);
        check("stack underflow halts cleanly", c.halted());
    }

    std::printf("display\n");
    {
        auto c = run({0x6000, 0x6100, 0xF029, 0xD015});  // font digit 0 at (0,0)
        bool topRow = c.display()[0] && c.display()[1] && c.display()[2] && c.display()[3];
        check("DXYN draws the \"0\" glyph", topRow);
        check("DXYN clears VF with no collision", c.v(0xF) == 0);
    }
    {
        auto c = run({0x6000, 0x6100, 0xF029, 0xD015, 0xD015});
        check("DXYN sets VF on collision", c.v(0xF) == 1);
        check("DXYN XOR erases on redraw", litPixels(c) == 0);
    }
    check("00E0 clears", litPixels(run({0x6000, 0xF029, 0xD015, 0x00E0})) == 0);

    std::printf("display wait\n");
    {
        const auto rom = assemble({0xF029, 0xD005, 0xD005});
        chip8::Chip8 chip;
        chip.loadRom(rom);
        chip.tick60();               // one frame available
        chip.cycle();                // FX29
        chip.cycle();                // first DXYN draws
        const std::uint16_t afterFirst = chip.pc();
        chip.cycle();                // second DXYN has no frame: stalls
        check("DXYN stalls until the next frame", chip.pc() == afterFirst);
        chip.tick60();
        chip.cycle();
        check("DXYN proceeds once the frame arrives", chip.pc() == afterFirst + 2);

        chip8::Quirks noWait;
        noWait.displayWait = false;
        chip8::Chip8 free(noWait);
        free.loadRom(rom);
        for (int i = 0; i < 3; ++i) free.cycle();
        check("displayWait off never stalls", free.pc() == 0x206);
    }

    std::printf("timers & input\n");
    {
        auto c = run({0x603C, 0xF015});
        c.tick60();
        c.tick60();
        check("delay timer counts down at 60 Hz", c.delayTimer() == 58);
    }
    {
        auto c = run({0x6005, 0xF018});
        check("sound timer drives the beeper", c.beeping());
    }
    {
        auto c = run({0xF00A}, 1);
        check("FX0A blocks", c.waitingForKey() && c.pc() == 0x202);
        c.setKey(0xA, true);
        c.cycle();
        check("FX0A still blocked while held", c.waitingForKey());
        c.setKey(0xA, false);
        c.cycle();
        check("FX0A releases with the key value", !c.waitingForKey() && c.v(0) == 0xA);
    }
    {
        // A tap entirely between two cycles must still register.
        auto c = run({0xF00A}, 1);
        c.setKey(0x7, true);
        c.setKey(0x7, false);
        c.cycle();
        check("FX0A catches a tap between cycles", !c.waitingForKey() && c.v(0) == 0x7);
    }
    {
        auto c = run({0x6005, 0xE0A1, 0x6001}, 1);
        c.setKey(5, true);
        c.cycle();
        c.cycle();
        check("EXA1 does not skip while key is held", c.v(0) == 1);
    }

    std::printf("roms\n");
    {
        chip8::Chip8 chip;
        if (!chip.loadRomFile("roms/splash.ch8")) {
            check("splash.ch8 loads", false, "(run make test from cpp/)");
        } else {
            // 700 Hz CPU against a 60 Hz frame clock: a tick every ~12 instructions.
            for (int i = 0; i < 3000 && !chip.halted(); ++i) {
                if (i % 12 == 0) chip.tick60();
                chip.cycle();
            }
            check("splash.ch8 runs clean", !chip.halted(), chip.error());
            check("splash.ch8 lit pixel count", litPixels(chip) > 200);

            std::printf("\nsplash.ch8 framebuffer:\n");
            for (int y = 0; y < chip8::kHeight; ++y) {
                std::string row;
                for (int x = 0; x < chip8::kWidth; ++x)
                    row += chip.display()[y * chip8::kWidth + x] ? '#' : '.';
                std::printf("%s\n", row.c_str());
            }
        }
    }

    std::printf("\n%s\n", failures == 0 ? "all checks passed"
                                        : (std::to_string(failures) + " check(s) failed").c_str());
    return failures == 0 ? 0 : 1;
}
