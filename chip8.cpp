#include "chip8.hpp"

#include <cstdio>
#include <fstream>
#include <random>

namespace chip8 {
namespace {

// Hexadecimal digits 0-F, 4x5 pixels each, one byte per row (high nibble used).
constexpr std::uint8_t kFont[80] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0,  // 0
    0x20, 0x60, 0x20, 0x20, 0x70,  // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0,  // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0,  // 3
    0x90, 0x90, 0xF0, 0x10, 0x10,  // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0,  // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0,  // 6
    0xF0, 0x10, 0x20, 0x40, 0x40,  // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0,  // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0,  // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90,  // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0,  // B
    0xF0, 0x80, 0x80, 0x80, 0xF0,  // C
    0xE0, 0x90, 0x90, 0x90, 0xE0,  // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0,  // E
    0xF0, 0x80, 0xF0, 0x80, 0x80,  // F
};

std::uint8_t randomByte() {
    static std::mt19937 engine{std::random_device{}()};
    static std::uniform_int_distribution<int> dist(0, 255);
    return static_cast<std::uint8_t>(dist(engine));
}

std::string hex(unsigned value, int width) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%0*X", width, value);
    return buf;
}

}  // namespace

Chip8::Chip8(Quirks q) : quirks(q) { reset(); }

void Chip8::reset() {
    memory_.fill(0);
    for (std::size_t i = 0; i < sizeof(kFont); ++i) memory_[kFontStart + i] = kFont[i];
    v_.fill(0);
    display_.fill(0);
    keys_.fill(0);
    stack_.clear();
    stack_.reserve(kStackLimit);
    i_ = 0;
    pc_ = kProgStart;
    delayTimer_ = soundTimer_ = 0;
    releasedKey_ = -1;
    waitingForKey_ = -1;
    vblankReady_ = false;
    halted_ = false;
    error_.clear();
    drawFlag = true;
}

bool Chip8::loadRom(const std::vector<std::uint8_t>& rom) {
    if (rom.size() > kMemorySize - kProgStart) return false;
    reset();
    for (std::size_t i = 0; i < rom.size(); ++i) memory_[kProgStart + i] = rom[i];
    return true;
}

bool Chip8::loadRomFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
    return loadRom(bytes);
}

void Chip8::setKey(std::uint8_t hexKey, bool down) {
    hexKey &= 0xF;
    if (!down && keys_[hexKey]) releasedKey_ = hexKey;
    keys_[hexKey] = down ? 1 : 0;
}

void Chip8::tick60() {
    vblankReady_ = true;
    if (delayTimer_ > 0) --delayTimer_;
    if (soundTimer_ > 0) --soundTimer_;
}

void Chip8::skip() { pc_ = (pc_ + 2) & 0xFFF; }

void Chip8::fault(const std::string& message) {
    error_ = message + " at PC " + hex((pc_ - 2) & 0xFFF, 3);
    halted_ = true;
}

// FX0A completes on press *and release*, as on the COSMAC VIP.
void Chip8::serviceKeyWait() {
    if (releasedKey_ < 0) return;
    v_[waitingForKey_] = static_cast<std::uint8_t>(releasedKey_);
    releasedKey_ = -1;
    waitingForKey_ = -1;
}

void Chip8::cycle() {
    if (halted_) return;
    if (waitingForKey_ >= 0) {
        serviceKeyWait();
        return;
    }

    // Fetch, and advance PC immediately so jumps and skips act on the next slot.
    const std::uint16_t opcode =
        static_cast<std::uint16_t>(memory_[pc_] << 8 | memory_[(pc_ + 1) & 0xFFF]);
    pc_ = (pc_ + 2) & 0xFFF;

    // Decode operands up front.
    const std::uint8_t x = (opcode & 0x0F00) >> 8;
    const std::uint8_t y = (opcode & 0x00F0) >> 4;
    const std::uint8_t n = opcode & 0x000F;
    const std::uint8_t nn = opcode & 0x00FF;
    const std::uint16_t nnn = opcode & 0x0FFF;

    switch (opcode & 0xF000) {
        case 0x0000:
            if (opcode == 0x00E0) {  // clear screen
                display_.fill(0);
                drawFlag = true;
            } else if (opcode == 0x00EE) {  // return from subroutine
                if (stack_.empty()) return fault("stack underflow");
                pc_ = stack_.back();
                stack_.pop_back();
            }
            // 0NNN called host machine code; no modern interpreter emulates it.
            break;

        case 0x1000:  // 1NNN: jump
            pc_ = nnn;
            break;

        case 0x2000:  // 2NNN: call
            if (stack_.size() >= kStackLimit) return fault("stack overflow");
            stack_.push_back(pc_);
            pc_ = nnn;
            break;

        case 0x3000: if (v_[x] == nn) skip(); break;                 // 3XNN
        case 0x4000: if (v_[x] != nn) skip(); break;                 // 4XNN
        case 0x5000: if (n == 0 && v_[x] == v_[y]) skip(); break;    // 5XY0
        case 0x9000: if (n == 0 && v_[x] != v_[y]) skip(); break;    // 9XY0

        case 0x6000: v_[x] = nn; break;                              // 6XNN
        case 0x7000: v_[x] += nn; break;                             // 7XNN, no carry

        case 0x8000: arithmetic(x, y, n); break;

        case 0xA000: i_ = nnn; break;                                // ANNN

        case 0xB000:  // BNNN: jump with offset
            pc_ = (nnn + (quirks.jumpWithVX ? v_[x] : v_[0])) & 0xFFF;
            break;

        case 0xC000: v_[x] = randomByte() & nn; break;               // CXNN

        case 0xD000:  // DXYN
            // The VIP drew during the vertical blank, capping sprites at 60 per
            // second. Stall by rewinding PC until the next frame arrives.
            if (quirks.displayWait && !vblankReady_) {
                pc_ = (pc_ - 2) & 0xFFF;
                return;
            }
            vblankReady_ = false;
            draw(v_[x], v_[y], n);
            break;

        case 0xE000:
            if (nn == 0x9E && keys_[v_[x] & 0xF]) skip();
            else if (nn == 0xA1 && !keys_[v_[x] & 0xF]) skip();
            break;

        case 0xF000: misc(x, nn); break;

        default:
            fault("unknown opcode " + hex(opcode, 4));
            break;
    }
}

void Chip8::arithmetic(std::uint8_t x, std::uint8_t y, std::uint8_t n) {
    switch (n) {
        case 0x0: v_[x] = v_[y]; break;
        // The VIP zeroed VF as a side effect of the logic ops; test ROMs check it.
        case 0x1: v_[x] |= v_[y]; if (quirks.logicResetsVF) v_[0xF] = 0; break;
        case 0x2: v_[x] &= v_[y]; if (quirks.logicResetsVF) v_[0xF] = 0; break;
        case 0x3: v_[x] ^= v_[y]; if (quirks.logicResetsVF) v_[0xF] = 0; break;
        case 0x4: {  // add with carry
            const unsigned sum = v_[x] + v_[y];
            v_[x] = static_cast<std::uint8_t>(sum);
            v_[0xF] = sum > 0xFF ? 1 : 0;
            break;
        }
        case 0x5: {  // VX -= VY
            const std::uint8_t noBorrow = v_[x] >= v_[y] ? 1 : 0;
            v_[x] -= v_[y];
            v_[0xF] = noBorrow;
            break;
        }
        case 0x7: {  // VX = VY - VX
            const std::uint8_t noBorrow = v_[y] >= v_[x] ? 1 : 0;
            v_[x] = v_[y] - v_[x];
            v_[0xF] = noBorrow;
            break;
        }
        case 0x6: {  // shift right
            const std::uint8_t src = quirks.shiftUsesVY ? v_[y] : v_[x];
            v_[x] = src >> 1;
            v_[0xF] = src & 1;
            break;
        }
        case 0xE: {  // shift left
            const std::uint8_t src = quirks.shiftUsesVY ? v_[y] : v_[x];
            v_[x] = static_cast<std::uint8_t>(src << 1);
            v_[0xF] = (src >> 7) & 1;
            break;
        }
        default:
            fault("unknown 8XY" + hex(n, 1));
            break;
    }
}

// Sprites are XORed in, so drawing the same sprite twice erases it. The start
// coordinate wraps; the sprite body clips at the right and bottom edges.
void Chip8::draw(std::uint8_t vx, std::uint8_t vy, std::uint8_t rows) {
    const int startX = vx % kWidth;
    const int startY = vy % kHeight;
    v_[0xF] = 0;

    for (int row = 0; row < rows; ++row) {
        const std::uint8_t spriteByte = memory_[(i_ + row) & 0xFFF];
        const int py = quirks.clipSprites ? startY + row : (startY + row) % kHeight;
        if (py >= kHeight) break;

        for (int bit = 0; bit < 8; ++bit) {
            if (!(spriteByte & (0x80 >> bit))) continue;
            const int px = quirks.clipSprites ? startX + bit : (startX + bit) % kWidth;
            if (px >= kWidth) break;

            std::uint8_t& pixel = display_[py * kWidth + px];
            if (pixel) v_[0xF] = 1;  // a pixel switched off means a collision
            pixel ^= 1;
        }
    }
    drawFlag = true;
}

void Chip8::misc(std::uint8_t x, std::uint8_t nn) {
    switch (nn) {
        case 0x07: v_[x] = delayTimer_; break;
        case 0x15: delayTimer_ = v_[x]; break;
        case 0x18: soundTimer_ = v_[x]; break;

        case 0x1E:  // FX1E: I += VX
            i_ += v_[x];
            if (quirks.indexOverflowFlag && i_ > 0x0FFF) v_[0xF] = 1;
            break;

        case 0x0A:  // FX0A: block until a key is pressed and released
            waitingForKey_ = x;
            releasedKey_ = -1;  // only releases from here on count
            break;

        case 0x29:  // FX29: point I at the font sprite for the low nibble of VX
            i_ = kFontStart + (v_[x] & 0xF) * 5;
            break;

        case 0x33:  // FX33: binary-coded decimal
            memory_[i_ & 0xFFF] = v_[x] / 100;
            memory_[(i_ + 1) & 0xFFF] = (v_[x] / 10) % 10;
            memory_[(i_ + 2) & 0xFFF] = v_[x] % 10;
            break;

        case 0x55:  // FX55: store V0..VX at I
            for (int reg = 0; reg <= x; ++reg) memory_[(i_ + reg) & 0xFFF] = v_[reg];
            if (quirks.memoryIncrementsI) i_ = (i_ + x + 1) & 0xFFF;
            break;

        case 0x65:  // FX65: load V0..VX from I
            for (int reg = 0; reg <= x; ++reg) v_[reg] = memory_[(i_ + reg) & 0xFFF];
            if (quirks.memoryIncrementsI) i_ = (i_ + x + 1) & 0xFFF;
            break;

        default:
            fault("unknown FX" + hex(nn, 2));
            break;
    }
}

}  // namespace chip8
