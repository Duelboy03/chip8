// CHIP-8 interpreter core.
//
// Pure logic: no windowing, no rendering, no clock of its own. The host owns
// timing and calls cycle() at the CPU rate and tick60() at 60 Hz.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace chip8 {

inline constexpr int kWidth = 64;
inline constexpr int kHeight = 32;
inline constexpr std::uint16_t kFontStart = 0x050;
inline constexpr std::uint16_t kProgStart = 0x200;
inline constexpr std::size_t kMemorySize = 4096;
inline constexpr std::size_t kStackLimit = 16;

// Instructions whose behaviour differed between historical interpreters. ROMs
// disagree about which they expect, so every one is switchable at runtime.
// Defaults follow Tobias V. Langhoff's recommendations: original COSMAC VIP
// for the shifts and BNNN, CHIP-48 for FX55/FX65.
struct Quirks {
    bool shiftUsesVY = true;        // 8XY6/8XYE copy VY into VX before shifting
    bool jumpWithVX = false;        // BNNN: false = NNN+V0, true = XNN+VX
    bool memoryIncrementsI = false; // FX55/FX65 leave I untouched
    bool indexOverflowFlag = false; // FX1E does not set VF past 0x0FFF
    bool clipSprites = true;        // DXYN start wraps, sprite body clips
    bool logicResetsVF = true;      // 8XY1/2/3 zero VF as a side effect
    bool displayWait = true;        // DXYN waits for vblank (60 draws/second)
};

class Chip8 {
public:
    explicit Chip8(Quirks quirks = {});

    // Clears all state and reloads the font. Does not touch the quirk settings.
    void reset();

    // Resets, then copies the ROM to 0x200. False if it cannot fit.
    bool loadRom(const std::vector<std::uint8_t>& rom);
    bool loadRomFile(const std::string& path);

    // Fetch, decode, and execute one instruction.
    void cycle();

    // 60 Hz housekeeping: counts the timers down and releases one display frame.
    void tick60();

    // Key state in. Releases are latched rather than sampled, so a tap that
    // begins and ends between two cycles still satisfies a pending FX0A.
    void setKey(std::uint8_t hexKey, bool down);

    bool beeping() const { return soundTimer_ > 0; }
    bool halted() const { return halted_; }
    const std::string& error() const { return error_; }
    bool waitingForKey() const { return waitingForKey_ >= 0; }

    // One byte per pixel, 0 or 1, row-major.
    const std::array<std::uint8_t, kWidth * kHeight>& display() const { return display_; }

    // Set by anything that changes the screen; the host clears it after drawing.
    bool drawFlag = true;

    Quirks quirks;

    // Exposed for tests and for a debugger view in the host.
    std::uint16_t pc() const { return pc_; }
    std::uint16_t index() const { return i_; }
    std::uint8_t v(std::size_t n) const { return v_[n]; }
    std::uint8_t delayTimer() const { return delayTimer_; }
    std::uint8_t memory(std::uint16_t addr) const { return memory_[addr & 0xFFF]; }
    std::size_t stackDepth() const { return stack_.size(); }
    // Lets a test grant a display frame without also advancing the timers.
    void grantFrame() { vblankReady_ = true; }

private:
    void skip();
    void fault(const std::string& message);
    void arithmetic(std::uint8_t x, std::uint8_t y, std::uint8_t n);
    void draw(std::uint8_t vx, std::uint8_t vy, std::uint8_t rows);
    void misc(std::uint8_t x, std::uint8_t nn);
    void serviceKeyWait();

    std::array<std::uint8_t, kMemorySize> memory_{};
    std::array<std::uint8_t, 16> v_{};
    std::array<std::uint8_t, kWidth * kHeight> display_{};
    std::array<std::uint8_t, 16> keys_{};
    std::vector<std::uint16_t> stack_;

    std::uint16_t i_ = 0;
    std::uint16_t pc_ = kProgStart;
    std::uint8_t delayTimer_ = 0;
    std::uint8_t soundTimer_ = 0;
    int releasedKey_ = -1;    // latched key-up, consumed by FX0A
    int waitingForKey_ = -1;  // register awaiting FX0A, or -1
    bool vblankReady_ = false;
    bool halted_ = false;
    std::string error_;
};

}  // namespace chip8
