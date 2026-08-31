// SDL2 front end: window, keypad, beeper, and the timing loop.
//
//   ./chip8 roms/2-ibm-logo.ch8 [--speed 700] [--quirk name=on|off ...]
#include "chip8.hpp"

#include <SDL.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr int kScale = 12;  // pixels per CHIP-8 pixel
constexpr int kAudioHz = 44100;

// The COSMAC VIP's 4x4 hex keypad mapped onto the left of a QWERTY keyboard:
//   1 2 3 C        1 2 3 4
//   4 5 6 D   <-   Q W E R
//   7 8 9 E        A S D F
//   A 0 B F        Z X C V
int hexForScancode(SDL_Scancode code) {
    switch (code) {
        case SDL_SCANCODE_1: return 0x1;
        case SDL_SCANCODE_2: return 0x2;
        case SDL_SCANCODE_3: return 0x3;
        case SDL_SCANCODE_4: return 0xC;
        case SDL_SCANCODE_Q: return 0x4;
        case SDL_SCANCODE_W: return 0x5;
        case SDL_SCANCODE_E: return 0x6;
        case SDL_SCANCODE_R: return 0xD;
        case SDL_SCANCODE_A: return 0x7;
        case SDL_SCANCODE_S: return 0x8;
        case SDL_SCANCODE_D: return 0x9;
        case SDL_SCANCODE_F: return 0xE;
        case SDL_SCANCODE_Z: return 0xA;
        case SDL_SCANCODE_X: return 0x0;
        case SDL_SCANCODE_C: return 0xB;
        case SDL_SCANCODE_V: return 0xF;
        default: return -1;
    }
}

// A square wave, generated on the fly. SDL pulls from this whenever the sound
// timer is running; `playing` is flipped by the main loop.
struct Beeper {
    SDL_AudioDeviceID device = 0;
    double phase = 0.0;
    bool playing = false;

    static void callback(void* userdata, Uint8* stream, int len) {
        auto* self = static_cast<Beeper*>(userdata);
        auto* out = reinterpret_cast<float*>(stream);
        const int samples = len / static_cast<int>(sizeof(float));
        const double step = 440.0 / kAudioHz;
        for (int i = 0; i < samples; ++i) {
            if (!self->playing) {
                out[i] = 0.0f;
                continue;
            }
            out[i] = self->phase < 0.5 ? 0.05f : -0.05f;
            self->phase += step;
            if (self->phase >= 1.0) self->phase -= 1.0;
        }
    }

    bool open() {
        SDL_AudioSpec want{};
        want.freq = kAudioHz;
        want.format = AUDIO_F32SYS;
        want.channels = 1;
        want.samples = 512;
        want.callback = callback;
        want.userdata = this;
        device = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
        if (device == 0) return false;
        SDL_PauseAudioDevice(device, 0);
        return true;
    }
};

void applyQuirk(chip8::Quirks& q, const std::string& spec) {
    const auto eq = spec.find('=');
    const std::string name = spec.substr(0, eq);
    const bool on = eq == std::string::npos ||
                    spec.substr(eq + 1) == "on" || spec.substr(eq + 1) == "1";
    if (name == "shift") q.shiftUsesVY = on;
    else if (name == "jump") q.jumpWithVX = on;
    else if (name == "memory") q.memoryIncrementsI = on;
    else if (name == "index") q.indexOverflowFlag = on;
    else if (name == "clip") q.clipSprites = on;
    else if (name == "logic") q.logicResetsVF = on;
    else if (name == "vblank") q.displayWait = on;
    else std::fprintf(stderr, "unknown quirk '%s', ignored\n", name.c_str());
}

void usage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s <rom.ch8> [--speed HZ] [--quirk NAME=on|off]...\n\n"
        "  quirks: shift jump memory index clip logic vblank\n"
        "  keys:   1234/QWER/ASDF/ZXCV   space=pause  n=step  backspace=reset\n",
        argv0);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    std::string romPath;
    int instructionsPerSecond = 700;  // the guide's recommended default
    chip8::Quirks quirks;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--speed" && i + 1 < argc) instructionsPerSecond = std::atoi(argv[++i]);
        else if (arg == "--quirk" && i + 1 < argc) applyQuirk(quirks, argv[++i]);
        else if (arg == "--help" || arg == "-h") { usage(argv[0]); return 0; }
        else if (!arg.empty() && arg[0] == '-') { usage(argv[0]); return 1; }
        else romPath = arg;
    }
    if (romPath.empty() || instructionsPerSecond <= 0) {
        usage(argv[0]);
        return 1;
    }

    chip8::Chip8 chip(quirks);
    if (!chip.loadRomFile(romPath)) {
        std::fprintf(stderr, "could not load ROM: %s\n", romPath.c_str());
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        ("CHIP-8 - " + romPath).c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        chip8::kWidth * kScale, chip8::kHeight * kScale, SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!window || !renderer) {
        std::fprintf(stderr, "SDL window/renderer: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer, chip8::kWidth, chip8::kHeight);

    Beeper beeper;
    if (!beeper.open()) std::fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());

    using clock = std::chrono::steady_clock;
    auto lastTime = clock::now();
    double cpuAccumulator = 0.0;    // instructions owed
    double timerAccumulator = 0.0;  // milliseconds owed to the 60 Hz clock
    bool running = true;
    bool paused = false;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                case SDL_KEYUP: {
                    const bool down = event.type == SDL_KEYDOWN;
                    const SDL_Scancode code = event.key.keysym.scancode;
                    if (down && !event.key.repeat) {
                        if (code == SDL_SCANCODE_ESCAPE) running = false;
                        else if (code == SDL_SCANCODE_SPACE) paused = !paused;
                        else if (code == SDL_SCANCODE_BACKSPACE) chip.loadRomFile(romPath);
                        else if (code == SDL_SCANCODE_N) { paused = true; chip.grantFrame(); chip.cycle(); }
                    }
                    const int hexKey = hexForScancode(code);
                    if (hexKey >= 0) chip.setKey(static_cast<std::uint8_t>(hexKey), down);
                    break;
                }
                default:
                    break;
            }
        }

        const auto now = clock::now();
        double deltaMs = std::chrono::duration<double, std::milli>(now - lastTime).count();
        lastTime = now;
        if (deltaMs > 100.0) deltaMs = 100.0;  // clamp after a stall

        if (!paused && !chip.halted()) {
            // Timers first: a DXYN waiting on vblank then resumes at the start
            // of this batch instead of spinning through the whole budget.
            timerAccumulator += deltaMs;
            while (timerAccumulator >= 1000.0 / 60.0) {
                chip.tick60();
                timerAccumulator -= 1000.0 / 60.0;
            }

            cpuAccumulator += deltaMs * instructionsPerSecond / 1000.0;
            int steps = static_cast<int>(cpuAccumulator);
            if (steps > 2000) steps = 2000;
            cpuAccumulator -= steps;
            for (int i = 0; i < steps && !chip.halted(); ++i) chip.cycle();
        }

        beeper.playing = !paused && chip.beeping();

        if (chip.drawFlag) {
            SDL_SetRenderDrawColor(renderer, 0x0F, 0x12, 0x10, 0xFF);
            SDL_RenderClear(renderer);
            SDL_SetRenderDrawColor(renderer, 0x8E, 0xF7, 0xA8, 0xFF);
            const auto& pixels = chip.display();
            for (int y = 0; y < chip8::kHeight; ++y) {
                for (int x = 0; x < chip8::kWidth; ++x) {
                    if (pixels[y * chip8::kWidth + x]) SDL_RenderDrawPoint(renderer, x, y);
                }
            }
            SDL_RenderPresent(renderer);
            chip.drawFlag = false;
        }

        SDL_Delay(1);  // yield rather than spin
    }

    if (chip.halted()) std::fprintf(stderr, "halted: %s\n", chip.error().c_str());

    if (beeper.device) SDL_CloseAudioDevice(beeper.device);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
