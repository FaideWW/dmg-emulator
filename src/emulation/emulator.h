#ifndef EMULATOR_H_
#define EMULATOR_H_

#undef assert

#include <cassert>
#include <stddef.h>
#include <stdint.h>
#include <string>

#define EMULATOR_SCREEN_WIDTH 160
#define EMULATOR_SCREEN_HEIGHT 144

#define kilobytes(x) (1024LL * (x))
#define megabytes(x) (1024LL * kilobytes(x))
#define gigabytes(x) (1024LL * megabytes(x))

// platform bridge functions

struct read_file_result {
  uint32_t contentSize;
  void *content;
};

typedef read_file_result platform_readEntireFile(std::string fp);
typedef void platform_freeFileMemory(read_file_result file);

// emulator constants

#define CLOCKSPEED_HZ 4194304
#define CPU_CYCLES_PER_FRAME 70224

#define COLORDATA_WHITE 0xEB
#define COLORDATA_DARKGREY 0x60
#define COLORDATA_LIGHTGREY 0xC4
#define COLORDATA_BLACK 0x00

#define CART_ROM_BANK_SIZE kilobytes(16)
#define CART_RAM_BANK_SIZE kilobytes(8)

#define SCANLINE_CYCLES = 456

// memory mapping shortcuts

#define MEM_BOOTROM 0x0000
#define MEM_PROGROM 0x0100

#define MEM_LCDC 0xFF40
#define MEM_STAT 0xFF41

#define CART_TYPE 0x0147
#define CART_ROM_SIZE 0x0148
#define CART_RAM_SIZE 0x0149

// CPU flags

#define CHECK_BIT(var, pos) ((var) & (1<<(pos))
#define FLAG_Z 7
#define FLAG_N 6
#define FLAG_H 5
#define FLAG_C 5

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

struct dmg_ppu {
  // because the CPU determines how many cycles elapse in a step,
  // there is a chance that the number of cycles will not align nicely with a
  // whole PPU instruction. in case this happens, we will execute the
  // instruction as if we had enough cycles, and then "remember" how many cycles
  // we skipped so that we can count them in the next step.
  int cycleOverhang;
};

struct emulator_state {
  dmg_cpu cpu;
  dmg_memory memory;

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

  // when true, we run the boot ROM until control is handed back to the game ROM
  // https://dmgdev.io/pandocs/Power_Up_Sequence.html
  bool isBooting;

  // false until we hit a STOP/HALT instruction
  bool isHalted;

  int cyclesDelta;
};

struct emulator_bridge {
  bool isInitialized;

  emulator_state *emulatorState;
  uint8_t *graphicsBuffer;

  platform_readEntireFile *platformReadEntireFile;
  platform_freeFileMemory *platformFreeFileMemory;
};

void initEmulator(emulator_bridge *emulator, std::string cartridgeROMPath);
void stepEmulator(emulator_bridge *emulator);
int stepCPU(emulator_state *state);
void advancePPU(emulator_state *state, int cyclesToAdvance);
void drawVramToBuffer(emulator_state *state, uint8_t *buffer);

// -------- memory --------
uint8_t readMemory8(emulator_state *state, uint16_t addr);
uint16_t readMemory16(emulator_state *state, uint16_t addr);
void readMemoryRegion(emulator_state *state, void *buffer, uint16_t start,
                      uint16_t len);

uint8_t unsafe_readMemory8(emulator_state *state, uint16_t addr);
uint8_t unsafe_readMemory16(emulator_state *state, uint16_t addr);
void unsafe_readMemoryRegion(emulator_state *state, void *buffer,
                             uint16_t start, size_t len);

void writeMemory8(emulator_state *state, uint16_t addr, uint8_t value);
void writeMemory16(emulator_state *state, uint16_t addr, uint16_t value);
void writeMemoryRegion(emulator_state *state, void *buffer, size_t bufLen,
                       uint16_t addr);

void unsafe_writeMemory8(emulator_state *state, uint16_t addr, uint8_t value);
void unsafe_writeMemory16(emulator_state *state, uint16_t addr, uint16_t value);
void unsafe_writeMemoryRegion(emulator_state *state, void *buffer,
                              size_t bufLen, uint16_t addr);

#endif
