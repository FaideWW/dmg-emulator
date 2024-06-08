#ifndef EMULATOR_H_
#define EMULATOR_H_

#undef assert

#include "log.h"
#include <cassert>
#include <stddef.h>
#include <stdint.h>
#include <string>

#define EMULATOR_SCREEN_WIDTH 160
#define EMULATOR_SCREEN_HEIGHT 144

#define kilobytes(x) (1024LL * (x))
#define megabytes(x) (1024LL * kilobytes(x))
#define gigabytes(x) (1024LL * megabytes(x))

#define ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))

// platform bridge functions

struct read_file_result {
  uint32_t contentSize;
  void *content;
};

typedef read_file_result platform_readEntireFile(std::string fp);
typedef void platform_freeFileMemory(read_file_result file);

// debug logging via ImGui

extern EmulatorLog debugLog;

// emulator constants

#define REGID_B 0
#define REGID_C 1
#define REGID_D 2
#define REGID_E 3
#define REGID_H 4
#define REGID_L 5
#define REGID_A 7
#define REGID_F 8

#define CLOCKSPEED_HZ 4194304
#define CPU_CYCLES_PER_FRAME 70224
#define CPU_CYCLES_PER_DIVTICK 256

#define COLORDATA_WHITE 0xEB
#define COLORDATA_LIGHTGREY 0xC4
#define COLORDATA_DARKGREY 0x60
#define COLORDATA_BLACK 0x00

#define CART_ROM_BANK_SIZE kilobytes(16)
#define CART_RAM_BANK_SIZE kilobytes(8)

#define SCANLINE_CYCLES 456
#define DMA_TRANSFER_DOTS 640

// memory mapping shortcuts

// interrupt handlers
#define MEM_INTERRUPT_VBLANK 0x0040
#define MEM_INTERRUPT_STAT 0x0048
#define MEM_INTERRUPT_TIMER 0x0050
#define MEM_INTERRUPT_SERIAL 0x0058
#define MEM_INTERRUPT_JOYPAD 0x0060

// cartridge header data
#define MEM_CART_TYPE 0x0147
#define MEM_CART_ROM_SIZE 0x0148
#define MEM_CART_RAM_SIZE 0x0149

// rom/ram sectors
#define MEM_ROM_BANK_N 0x4000
#define MEM_VRAM 0x8000
#define MEM_VRAM_SIZE 0x2000
#define MEM_WRAM 0xC000
#define MEM_WRAM_BANK_N 0xD000
#define MEM_ECHO_RAM 0xE000
#define ECHO_RAM_SIZE 0x1F00

// object attribute mapping
#define MEM_OAM 0xFE00
#define MEM_OAM_SIZE 0xA0

// --- IO

#define MEM_JOYPAD 0xFF00
#define MEM_SERIAL_BUS 0xFF01
#define MEM_SERIAL_CONTROL 0xFF02

// timer registers
#define MEM_DIV 0xFF04
#define MEM_TIMA 0xFF05
#define MEM_TMA 0xFF06
#define MEM_TAC 0xFF07

// sound registers (TODO)

// lcd/graphics
#define MEM_LCDC 0xFF40
#define MEM_STAT 0xFF41

#define STAT_SELECT_LYC 6
#define STAT_SELECT_MODE2 5
#define STAT_SELECT_MODE1 4
#define STAT_SELECT_MODE0 3
#define STAT_LY_LYC 2

#define MEM_SCY 0xFF42
#define MEM_SCX 0xFF43
#define MEM_LY 0xFF44
#define MEM_LYC 0xFF45
#define MEM_DMA 0xFF46
#define MEM_BGP 0xFF47
#define MEM_OBP0 0xFF48
#define MEM_OBP1 0xFF49
#define MEM_WY 0xFF4A
#define MEM_WX 0xFF4B
#define MEM_BOOTROM_DISABLE 0xFF50
// vram DMA (unused; CGB only)
#define MEM_VRAM_VBK 0xFF4F
#define MEM_VRAM_HDMA1 0xFF51
#define MEM_VRAM_HDMA2 0xFF52
#define MEM_VRAM_HDMA3 0xFF53
#define MEM_VRAM_HDMA4 0xFF54
#define MEM_VRAM_HDMA5 0xFF55
// color palettes (unused; CGB only)
#define MEM_BCPS 0xFF68
#define MEM_BCPD 0xFF69
#define MEM_OCPS 0xFF6A
#define MEM_OCPD 0xFF6B

#define MEM_HRAM 0xFF80
#define MEM_HRAM_SIZE 0x7F

// interrupts
#define MEM_IF 0xFF0F
#define MEM_IE 0xFFFF

// CPU flags

#define FLAG_Z 7
#define FLAG_N 6
#define FLAG_H 5
#define FLAG_C 4

#define INTERRUPT_JOYPAD 4
#define INTERRUPT_SERIAL 3
#define INTERRUPT_TIMER 2
#define INTERRUPT_LCD 1
#define INTERRUPT_VBLANK 0

inline void setBit(uint8_t *f, int bitIndex) { *f = (*f | (1 << bitIndex)); }
inline void clearBit(uint8_t *f, int bitIndex) { *f = (*f & ~(1 << bitIndex)); }
inline void toggleBit(uint8_t *f, int bitIndex) {
  *f = *f ^ ((uint8_t)1 << bitIndex);
}
inline bool checkBit(uint8_t f, int bitIndex) { return (f >> bitIndex) & 1; }

inline bool checkBit16(uint16_t f, int bitIndex) { return (f >> bitIndex) & 1; }

// emulator structs

struct cart_hardware {
  short mbc;
  bool supportedMBCType;
  bool ramEnabled;

  // unused fields
  bool batteryEnabled;
  bool timerEnabled;
  bool sensorEnabled;
  bool rumbleEnabled;
};

struct dmg_cpu_registers {
  // General purpose registers
  uint8_t a; // accumulator
  uint8_t b;
  uint8_t c;
  uint8_t d;
  uint8_t e;
  uint8_t h;
  uint8_t l;
  uint8_t f; // flags register
};

struct dmg_cpu {
  dmg_cpu_registers reg;

  uint16_t pc; // program counter
  uint16_t sp; // stack pointer
  bool ime;    // interrupt master enable

  bool isHalted;
  bool isStopped;
};

struct dmg_memory {
  uint8_t mainMemory[0x10000];

  // cartridges can have 1 or more active ROM banks of 0x4000 bytes each
  uint8_t activeROMBank;

  // cartridges can have up to 4 RAM banks of 8KB (0x2000) each (in MBC1; MBC5
  // can support up to 128KB RAM)
  uint8_t cartridgeRAMBanks[0x8000];
  uint8_t activeRAMBank;

  // whether RAM write access is enabled
  bool enableRAM;
  // banking mode 0 is ROM banking; banking mode 1 is RAM banking
  bool bankingMode;
};

#define BG_PIXEL 0
#define OBJ_PIXEL 1

struct fifo_pixel {
  uint8_t pixelColor;
  bool palette;
  bool pixelSrc;
  bool bgPriority;
};

struct oam_sprite {
  uint8_t y;
  uint8_t x;
  uint8_t tileNum;
  uint8_t flags;
};

struct dmg_ppu {
  // because the CPU determines how many cycles elapse in a step,
  // there is a chance that the number of cycles will not align nicely with a
  // whole PPU instruction. in case this happens, we will execute the
  // instruction as if we had enough cycles, and then "remember" how many cycles
  // we skipped so that we can count them in the next step.
  int cycleOverrun;

  // which "dot" on the horizontal scan we're on
  // (https://gbdev.io/pandocs/Rendering.html)
  int currentDot;

  // during DMA transfers, only HRAM is accessible to the CPU. PPU access to
  // VRAM during DMA results in rendering glitches
  // (https://gbdev.io/pandocs/OAM_DMA_Transfer.html)
  bool inDMA;
  int dmaDotsRemaining;

  // oamSpriteBuffer holds the sprites to be drawn on the current scanline (as
  // calculated by the OAM scan). These are sorted by their X position (and
  // secondarily, by their position in memory)
  //
  // As sprites are fed into the FIFO, oamSpriteHead tracks the "next" sprite to
  // be drawn.
  uint8_t oamSpriteHead;
  oam_sprite oamSpriteBuffer[10];
  uint8_t spritesInBuffer;
  bool oamScanned;

  // the background FIFO holds up to 8 pixels to be pushed to the screen (in
  // first-in-first-out order). this FIFO will try to push pixels to the LCD as
  // long as it has pixels, and will mix pixels from the sprite FIFO (if there
  // are any) before pushing. if there are no pixels in the bg FIFO it will
  // pause until it's filled by the next fetch.
  fifo_pixel bgFifo[8];
  uint8_t pixelsInBgFifo;

  // which pixel on the current scanline will the FIFO push to next
  uint8_t nextLCDPixel;

  // there are three fetch types that can happen - a background fetch, a window
  // fetch, and an OAM/sprite fetch
  //
  // the background fetcher is technically always running to retrieve the next
  // group of pixels, but it can only push them into the FIFO when there is
  // room. so we buffer the pixels the fetcher has retrieved, and then "pause"
  // until we can push them into the FIFO
  //
  // when we reach a window pixel (the FIFO X == WX, we reset the fetcher, flush
  // the FIFO and initiate a window pixel fetch. (I believe this persists until
  // the end of the scanline, since the window is always the same size as the
  // viewport and can never start BEFORE X=0)
  //
  // both bg and window fetches push pixels into the bg FIFO.
  fifo_pixel bgFetcherBuffer[8];
  bool bufferFull;
  uint8_t bgFetchPendingCyclesRemaining;

  // when we reach a sprite/object (there is an object in the oam buffer whose X
  // == FIFO X), initiate a sprite pixel fetch and push into the OAM FIFO.
  // during a sprite fetch the BG FIFO is also paused, so that we don't advance
  // beyond where the sprite should be displayed.
  fifo_pixel oamFifo[8];
  uint8_t pixelsInOamFifo;
  uint8_t oamFetchPendingCyclesRemaining;

  // this value tracks the x-position of the FIFO/fetcher. it's incremented
  // after each pixel fetch (8 pixels at a time), which corresponds to the next
  // tile on the scanline
  uint8_t internalXCounter;

  // the window has its own internal line counter that increments with LY
  // whenever the window is being fetched
  bool isFetchingWindow;
  uint8_t windowLineCounter;

  // *Only* a 0 to 1 transition of the STAT interrupt conditions triggers an
  // IRQ. All conditions are OR'd together, so if multiple conditions are
  // met/have overlap, only the first condition to be met will trigger an
  // interrupt.
  bool lastStatResult;
};

struct emulator_state {
  dmg_cpu cpu;
  dmg_memory memory;
  dmg_ppu ppu;

  // contains dmg_boot. when `emulator_state.isBooting` is true, memory region
  // [0x0000-0x0100] is mapped into this block
  uint8_t bootROM[0x100];

  // contains the loaded cartridge's ROM data
  uint8_t cartridgeROM[megabytes(8)];
  size_t cartridgeROMSize;

  // contains the loaded cartridge's RAM
  uint8_t cartridgeRAM[kilobytes(128)];
  size_t cartridgeRAMSize;
  cart_hardware cartridgeHardware;
  bool isCartLoaded;

  // when true, we map memory $0000-$0100 to boot ROM, until control is handed
  // back to the game ROM
  // https://dmgdev.io/pandocs/Power_Up_Sequence.html
  bool isBooting;

  // false until we hit a STOP/HALT instruction
  bool isHalted;
  bool isRunning;
  int cyclesDelta;

  // debug controls
  bool debug_breakpointEnabled;
  uint16_t debug_breakpoint;

  // timer tracking

  int dividerCycles; // cycles since last divider tick
  int timaCycles;    // cycles since last tima tick
};

struct emulator_bridge {
  bool isInitialized;

  emulator_state *emulatorState;
  uint32_t *graphicsBuffer;

  platform_readEntireFile *platformReadEntireFile;
  platform_freeFileMemory *platformFreeFileMemory;
};

void initEmulator(emulator_bridge *emulator, std::string cartridgeROMPath);
void resetEmulator(emulator_bridge *emulator);
int stepInstruction(emulator_bridge *emulator);
void stepFrame(emulator_bridge *emulator);
int stepEmulator(emulator_bridge *emulator, int cycles);
int stepCPU(emulator_state *state);
int serviceInterrupts(emulator_state *state);
void requestInterrupt(emulator_state *state, int interruptBitIndex);
void updateStat(emulator_state *state);
void drawVramToBuffer(emulator_state *state, uint8_t *buffer);
const char *printRegisterName(int regId);

// -------- ppu ---------

void advancePPU(emulator_state *state, int cyclesToAdvance,
                uint32_t *graphicsBuffer);
void resetPPUState(emulator_state *state);
uint8_t readPPUMode(emulator_state *state);
void setPPUMode(emulator_state *state, uint8_t mode);

// -------- timers --------
int getTimaFrequency(uint8_t tac);
void advanceTimers(emulator_state *state, int cycles);

// -------- memory --------
int readMemory8(emulator_state *state, uint16_t addr, uint8_t *dest);
int readMemory8(emulator_state *state, uint16_t addr, int8_t *dest);
int readMemory16(emulator_state *state, uint16_t addr, uint16_t *dest);
int readMemory16(emulator_state *state, uint16_t addr, int16_t *dest);
int readMemoryRegion(emulator_state *state, void *buffer, uint16_t start,
                     uint16_t len);

void internal_readMemory8(emulator_state *state, uint16_t addr, uint8_t *dest);
void internal_readMemory16(emulator_state *state, uint16_t addr,
                           uint16_t *dest);
void internal_readMemory32(emulator_state *state, uint16_t addr,
                           uint32_t *dest);

int writeMemory8(emulator_state *state, uint16_t addr, uint8_t value);
int writeMemory16(emulator_state *state, uint16_t addr, uint16_t value);
int writeMemoryRegion(emulator_state *state, void *buffer, size_t bufLen,
                      uint16_t addr);

void internal_writeMemory8(emulator_state *state, uint16_t addr, uint8_t value);
void internal_writeMemoryRegion(emulator_state *state, void *buffer,
                                size_t bufLen, uint16_t addr);

#endif
