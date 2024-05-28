#include "emulator.h"
#include <string.h>

// Read a uint8_t from memory, respecting all read rules
// cycles: 4
int readMemory8(emulator_state *state, uint16_t addr, uint8_t *dest) {
  if (addr < 0x100) {
    if (state->isBooting) {
      *dest = state->bootROM[addr];
    } else {
      *dest = state->memory.mainMemory[addr];
    }
  } else {
    *dest = state->memory.mainMemory[addr];
  }
  return 4;
}

// Read a uint16_t from memory, respecting all read rules
// cycles: 8
int readMemory16(emulator_state *state, uint16_t addr, uint16_t *dest) {
  uint8_t lo, hi;
  readMemory8(state, addr, &lo);
  readMemory8(state, addr + 1, &hi);
  *dest = ((uint16_t)hi << 8) | lo;
  return 8;
}

// Read a uint16_t from memory, respecting all read rules
void readMemoryRegion(emulator_state *state, void *buffer, uint16_t start,
                      size_t len) {
  assert(start + len < 0xFFFF);
  memcpy(buffer, state->memory.mainMemory + start, len);
}

void internal_readMemory8(emulator_state *state, uint16_t addr, uint8_t *dest) {
  *dest = state->memory.mainMemory[addr];
}

// Write a uint8_t to memory, respecting all write rules
// cycles: 4
int writeMemory8(emulator_state *state, uint16_t addr, uint8_t value) {
  // during a DMA transfer, most memory is inaccessible with the exception of
  // HRAM.
  uint8_t ppuMode = readPPUMode(state);
  if (!state->ppu.inDMA ||
      (addr >= MEM_HRAM && addr < MEM_HRAM + MEM_HRAM_SIZE)) {
    if (addr >= MEM_VRAM && addr < MEM_VRAM + MEM_VRAM_SIZE) {
      // VRAM is only accessible outside of ppu mode 3 (pixel transfer)
      if (ppuMode != 3) {
        state->memory.mainMemory[addr] = value;
      }
    } else if (addr >= MEM_WRAM && addr < MEM_WRAM + ECHO_RAM_SIZE) {
      // writes to working ram are mirrored to echo ram (and vice versa), due to
      // a quirk in the hardware.
      uint16_t offset = addr - MEM_WRAM;
      state->memory.mainMemory[addr] = value;
      state->memory.mainMemory[MEM_ECHO_RAM + offset] = value;
    } else if (addr >= MEM_ECHO_RAM && addr < MEM_ECHO_RAM + ECHO_RAM_SIZE) {
      // writes to echo ram are mirrored to working ram (and vice versa), due to
      // a quirk in the hardware.
      uint16_t offset = addr - MEM_ECHO_RAM;
      state->memory.mainMemory[addr] = value;
      state->memory.mainMemory[MEM_WRAM + offset] = value;
    } else if (addr >= MEM_OAM && addr < MEM_OAM + MEM_OAM_SIZE) {
      // OAM is only accessible outside of ppu modes 2 and 3
      if (ppuMode != 3 && ppuMode != 2) {
        state->memory.mainMemory[addr] = value;
      }
    } else if (addr == MEM_DIV) {
      // Writing any value to the divider register resets it to $00
      state->memory.mainMemory[addr] = 0x00;
    } else if (addr == MEM_LCDC) {
      uint8_t prev = state->memory.mainMemory[addr];
      state->memory.mainMemory[addr] = value;
      /* if (!checkBit(prev, 7) && checkBit(value, 7)) { */
      /*   // make sure we reset the internal PPU state */
      /*   resetPPUState(state); */
      /* } */

      if (checkBit(prev, 7) && !checkBit(value, 7) && ppuMode != 1) {
        // disabling the LCD outside of VBlank can damage the screen on real
        // hardware. log a warning here
        debugLog.AddLog("[WARN] Disabling the LCD outside of VBlank can damage "
                        "hardware!!\n");
      }
    } else if (addr == MEM_DMA) {
      // Writing to DMA starts a DMA transfer of the data in ROM/RAM at
      // $XX00-$XX9F (where XX is the byte written) into VRAM at $FE00-$FE9F
      if (value >= 0x00 && value < 0xE0) {
        // value must be between $00 and $DF
        state->memory.mainMemory[addr] = value;
        state->ppu.inDMA = true;
        // TODO: this is probably not cycle accurate. figure out when the dma
        // transfer actually starts and buffer the duration by that amount.
        state->ppu.dmaDotsRemaining = DMA_TRANSFER_DOTS;
      }
    } else if (addr == MEM_LY) {
      // LY is read only, so we do nothing here.
      debugLog.AddLog(
          "[WARN] Attempted to write to read-only property LY ($FF44)\n");
    } else {
      state->memory.mainMemory[addr] = value;
    }
  }
  return 4;
}

// Write a uint16_t to memory, respecting all write rules
// cycles: 8
int writeMemory16(emulator_state *state, uint16_t addr, uint16_t value) {
  uint8_t lo = value & 0x00FF;
  uint8_t hi = (value & 0xFF00) >> 8;
  writeMemory8(state, addr, lo);
  writeMemory8(state, addr + 1, hi);
  return 8;
}

int writeMemoryRegion(emulator_state *state, void *buffer, size_t bufLen,
                      uint16_t start) {
  assert(start + bufLen < 0xFFFF);
  memcpy(state->memory.mainMemory + start, buffer, bufLen);
  return 0;
}

void internal_writeMemory8(emulator_state *state, uint16_t addr,
                           uint8_t value) {
  state->memory.mainMemory[addr] = value;
}
void internal_writeMemoryRegion(emulator_state *state, void *buffer,
                                size_t bufLen, uint16_t start) {
  assert(start + bufLen < 0xFFFF);
  memcpy(state->memory.mainMemory + start, buffer, bufLen);
}
