// Headless ROM runner: prints the framebuffer as ASCII after N cycles.
//
//   ./run_rom roms/3-corax+.ch8 [cycles] [key=<hex>@<cycle>[:<hold>]] [quirk=NAME:on|off]...
//   ./run_rom roms/6-keypad.ch8 10500 key=1@2000:200 key=5@9000:5000
//   ./run_rom roms/5-quirks.ch8 30000 key=1@2000 quirk=memory:on
#include "chip8.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

// A scheduled keypress: pressed at `at`, released `hold` cycles later. The
// default hold is long because ROMs that poll each key in turn need to see it.
struct Tap {
    std::uint8_t hex;
    int at;
    int hold;
};

Tap parseTap(const std::string& spec) {
    const auto at = spec.find('@');
    const auto colon = spec.find(':', at == std::string::npos ? 0 : at);
    Tap tap{};
    tap.hex = static_cast<std::uint8_t>(std::stoi(spec.substr(4, at - 4), nullptr, 16));
    tap.at = std::stoi(spec.substr(at + 1, colon == std::string::npos
                                              ? std::string::npos : colon - at - 1));
    tap.hold = colon == std::string::npos ? 300 : std::stoi(spec.substr(colon + 1));
    return tap;
}

// quirk=NAME:on|off, matching the front end's --quirk flag.
void applyQuirk(chip8::Quirks& q, const std::string& spec) {
    const auto colon = spec.find(':', 6);
    const std::string name = spec.substr(6, colon - 6);
    const bool on = colon == std::string::npos || spec.substr(colon + 1) == "on";
    if (name == "shift") q.shiftUsesVY = on;
    else if (name == "jump") q.jumpWithVX = on;
    else if (name == "memory") q.memoryIncrementsI = on;
    else if (name == "index") q.indexOverflowFlag = on;
    else if (name == "clip") q.clipSprites = on;
    else if (name == "logic") q.logicResetsVF = on;
    else if (name == "vblank") q.displayWait = on;
    else std::fprintf(stderr, "unknown quirk '%s', ignored\n", name.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
            "usage: %s <rom.ch8> [cycles] [key=<hex>@<cycle>[:<hold>]] "
            "[quirk=NAME:on|off]...\n", argv[0]);
        return 1;
    }

    const std::string romPath = argv[1];
    int cycles = 2000;
    std::vector<Tap> taps;
    chip8::Quirks quirks;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("key=", 0) == 0) taps.push_back(parseTap(arg));
        else if (arg.rfind("quirk=", 0) == 0) applyQuirk(quirks, arg);
        else cycles = std::atoi(arg.c_str());
    }

    chip8::Chip8 chip(quirks);
    if (!chip.loadRomFile(romPath)) {
        std::fprintf(stderr, "could not load ROM: %s\n", romPath.c_str());
        return 1;
    }

    int beepCycles = 0;
    for (int i = 0; i < cycles && !chip.halted(); ++i) {
        for (const Tap& tap : taps) {
            if (i == tap.at) chip.setKey(tap.hex, true);
            if (i == tap.at + tap.hold) chip.setKey(tap.hex, false);
        }
        if (i % 12 == 0) chip.tick60();  // ~60 Hz relative to a 700 Hz CPU
        chip.cycle();
        if (chip.beeping()) ++beepCycles;
    }

    std::printf("=== %s (%d cycles) ===\n", romPath.c_str(), cycles);
    for (int y = 0; y < chip8::kHeight; ++y) {
        std::string row;
        for (int x = 0; x < chip8::kWidth; ++x)
            row += chip.display()[y * chip8::kWidth + x] ? '#' : ' ';
        while (!row.empty() && row.back() == ' ') row.pop_back();
        std::printf("%s\n", row.c_str());
    }
    std::printf("PC %03X | beeped %d cycles%s%s\n", chip.pc(), beepCycles,
                chip.halted() ? " | HALTED: " : "",
                chip.halted() ? chip.error().c_str() : "");
    return 0;
}
