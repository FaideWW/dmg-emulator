#include "emulator.h"
#include <sys/mman.h>

static size_t decodeCartROMSize(uint8_t byteValue);
static cart_hardware decodeCartType(uint8_t byteValue);

EmulatorLog debugLog;

void initEmulator(emulator_bridge *bridge, std::string cartridgeROMPath) {
  debugLog.enableLogging();
  emulator_state *state = bridge->emulatorState;
  // load boot ROM
  read_file_result bootRomData =
      bridge->platformReadEntireFile("assets/DMG_ROM.bin");
  assert(bootRomData.contentSize == 0x100);

  uint8_t *bootRomPointer = state->bootROM;
  for (size_t byteIndex = 0; byteIndex < bootRomData.contentSize; ++byteIndex) {
    *bootRomPointer++ = *((uint8_t *)bootRomData.content + byteIndex);
  }

  if (cartridgeROMPath.length() > 0) {
    // load boot ROM
    read_file_result cartRomData =
        bridge->platformReadEntireFile(cartridgeROMPath);
    // load cartridge ROM
    uint8_t *cartRomPointer = state->cartridgeROM;
    for (size_t byteIndex = 0; byteIndex < cartRomData.contentSize;
         ++byteIndex) {
      *cartRomPointer++ = *((uint8_t *)cartRomData.content + byteIndex);
    }

    debugLog.AddLog("loaded %s; size: %dB\n", cartridgeROMPath.c_str(),
                    cartRomData.contentSize);

    internal_writeMemoryRegion(state, state->cartridgeROM,
                               cartRomData.contentSize, 0);

    uint8_t cartRomSizeValue;
    internal_readMemory8(state, MEM_CART_ROM_SIZE, &cartRomSizeValue);
    state->cartridgeROMSize = decodeCartROMSize(cartRomSizeValue);
    debugLog.AddLog("cartridge ROM size: %zuB\n", state->cartridgeROMSize);

    uint8_t cartRamSizeByte;
    internal_readMemory8(state, MEM_CART_RAM_SIZE, &cartRamSizeByte);
    size_t cartRamSize = 0;
    switch (cartRamSizeByte) {
    // 0x00: no RAM
    // 0x01: unused
    case 0x02: {
      cartRamSize = kilobytes(8);
      break;
    }
    case 0x03: {
      cartRamSize = kilobytes(32);
      break;
    }
    case 0x04: {
      cartRamSize = kilobytes(128);
      break;
    }
    case 0x05: {
      cartRamSize = kilobytes(64);
      break;
    }
    }

    state->cartridgeRAMSize = cartRamSize;
    state->isCartLoaded = true;
    state->isBooting = true;

    uint8_t cartTypeByte;
    internal_readMemory8(state, MEM_CART_TYPE, &cartTypeByte);
    state->cartridgeHardware = decodeCartType(cartTypeByte);
  }

  bridge->isInitialized = true;
}

// TODO: this is probably broken. we don't want to wipe all memory (or if we do,
// we need to reload stuff like the boot ROM
void resetEmulator(emulator_bridge *bridge) {
  emulator_state *state = bridge->emulatorState;
  // zero out the state
  memset(&state->cpu, 0, sizeof(dmg_cpu));
  memset(&state->memory, 0, sizeof(dmg_memory));
  state->cyclesDelta = 0;
}

// Advance the emulation 1 CPU instruction. return the number of cycles elapsed
int stepInstruction(emulator_bridge *bridge) {
  emulator_state *state = bridge->emulatorState;
  // Advance the clock 1 cpu instruction
  int cyclesToAdvance = stepCPU(state);

  // compare LY and LYC and update STAT if needed
  updateStat(state);

  // Service any pending interrupts (if IME is enabled)
  cyclesToAdvance += serviceInterrupts(state);

  // Advance the PPU and refresh the LCD (if appropriate)
  advancePPU(state, cyclesToAdvance, bridge->graphicsBuffer);

  // Increment timers
  advanceTimers(state, cyclesToAdvance);

  // We're done booting once the PC reaches $100
  if (state->isBooting && state->cpu.pc == 0x100) {
    state->isBooting = false;
  }

  return cyclesToAdvance;
}

// Advance the emulation by 1 frame (70224 cycles)
// The assumption is this is called every 16.67ms, or at 60FPS
// (TODO: currently this is enforced with VSYNC, find a way to cap on non-vsync
// displays).
// All we need to do is simulate the right amount of cycles and
// return, and the platform layer is responsible for syncing/sleeping for the
// right amount of time between frames.
void stepFrame(emulator_bridge *bridge) {
  int cycleDelta = stepEmulator(bridge, CPU_CYCLES_PER_FRAME -
                                            bridge->emulatorState->cyclesDelta);
  bridge->emulatorState->cyclesDelta = cycleDelta;
}

// advance the emulation an arbitrary number of steps. this is useful for
// debugging ("advance to next frame", etc.).
// this is will try to advance at LEAST this number of cycles, but may
// overrun in the case that we end on an instruction that takes more than the
// remaining cycles to complete. it may also underrun the cycle count if it hits
// a debug breakpoint. the function will return the actual number of cycles
// completed, so that if a precise timing is required the delta can be accounted
// for
int stepEmulator(emulator_bridge *bridge, int cyclesToSimulate) {
  emulator_state *state = bridge->emulatorState;
  int cyclesAdvanced = 0;
  while (cyclesAdvanced < cyclesToSimulate && !state->cpu.isStopped &&
         !state->cpu.isHalted) {
    cyclesAdvanced += stepInstruction(bridge);

    if (state->debug_breakpointEnabled &&
        state->debug_breakpoint == state->cpu.pc) {
      debugLog.AddLog("[DEBUG] pc reached breakpoint at $%04hX, pausing\n",
                      state->debug_breakpoint);
      state->isRunning = false;
      break;
    }
  }

  return cyclesAdvanced - cyclesToSimulate;
}

static cart_hardware decodeCartType(uint8_t byteValue) {
  cart_hardware result = {};
  result.supportedMBCType = true;
  switch (byteValue) {
  case 0x01: {
    result.mbc = 1;
    break;
  }
  case 0x02: {
    result.mbc = 1;
    result.ramEnabled = true;
    break;
  }
  case 0x03: {
    result.mbc = 1;
    result.ramEnabled = true;
    result.batteryEnabled = true;
    break;
  }
  case 0x05: {
    result.mbc = 2;
    break;
  }
  case 0x06: {
    result.mbc = 2;
    result.batteryEnabled = true;
    break;
  }
  case 0x08: {
    result.ramEnabled = true;
    break;
  }
  case 0x09: {
    result.ramEnabled = true;
    result.batteryEnabled = true;
    break;
  }
    // 0x0B - MMM01
    // 0x0C - MMM01+RAM
    // 0x0D - MMM01+RAM+BATTERY
  case 0x0F: {
    result.mbc = 3;
    result.timerEnabled = true;
    result.batteryEnabled = true;
    break;
  }
  case 0x10: {
    result.mbc = 3;
    result.ramEnabled = true;
    result.timerEnabled = true;
    result.batteryEnabled = true;
    break;
  }
  case 0x11: {
    result.mbc = 3;
    break;
  }
  case 0x12: {
    result.mbc = 3;
    result.ramEnabled = true;
    break;
  }
  case 0x13: {
    result.mbc = 3;
    result.ramEnabled = true;
    result.batteryEnabled = true;
    break;
  }
  case 0x19: {
    result.mbc = 5;
    break;
  }
  case 0x1A: {
    result.mbc = 5;
    result.ramEnabled = true;
    break;
  }
  case 0x1B: {
    result.mbc = 5;
    result.ramEnabled = true;
    result.batteryEnabled = true;
    break;
  }
  case 0x1C: {
    result.mbc = 5;
    result.rumbleEnabled = true;
    break;
  }
  case 0x1D: {
    result.mbc = 5;
    result.ramEnabled = true;
    result.rumbleEnabled = true;
    break;
  }
  case 0x1E: {
    result.mbc = 5;
    result.ramEnabled = true;
    result.rumbleEnabled = true;
    result.batteryEnabled = true;
    break;
  }
  case 0x20: {
    result.mbc = 6;
    break;
  }
  case 0x22: {
    result.mbc = 7;
    result.sensorEnabled = true;
    result.ramEnabled = true;
    result.rumbleEnabled = true;
    result.batteryEnabled = true;
    break;
  }
    // 0xFC - camera
    // 0xFD - bandai tama5 (?)
    // 0xFE - HuC3
    // 0xFF - HuC1+RAM+BATTERY
  default: {
    result.supportedMBCType = false;
  }
  }
  return result;
}

static size_t decodeCartROMSize(uint8_t byteValue) {
  /* assert(byteValue < 0x08); */
  return kilobytes(32) * (1 << byteValue);
}
