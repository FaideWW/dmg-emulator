#include "emulator.h"
#include <sys/mman.h>

static size_t decodeCartROMSize(uint8_t byteValue);
static cart_hardware decodeCartType(uint8_t byteValue);

void initEmulator(emulator_bridge *bridge, std::string cartridgeROMPath) {
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
    assert(cartRomData.contentSize == 0x100);
    // load cartridge ROM
    uint8_t *cartRomPointer = state->cartridgeROM;
    for (size_t byteIndex = 0; byteIndex < cartRomData.contentSize;
         ++byteIndex) {
      *cartRomPointer++ = *((uint8_t *)cartRomData.content + byteIndex);
    }

    uint8_t cartRomSizeValue = *(state->cartridgeROM + CART_ROM_SIZE);
    state->cartridgeROMSize = decodeCartROMSize(cartRomSizeValue);

    uint8_t cartRamSizeByte = *(state->cartridgeROM + CART_RAM_SIZE);
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

    uint8_t cartTypeByte = *(state->cartridgeROM + CART_TYPE);
    state->cartridgeHardware = decodeCartType(cartTypeByte);
  }

  bridge->isInitialized = true;
}

void bootEmulator(emulator_bridge *bridge) {
  emulator_state *state = bridge->emulatorState;
  dmg_cpu cpu = state->cpu;
  dmg_memory memory = state->memory;
  // zero out the state
  memset(&cpu, 0, sizeof(dmg_cpu));
  memset(&memory, 0, sizeof(dmg_memory));
  state->isBooting = true;
  state->cyclesDelta = 0;
}

// Advance the emulation by 1 CPU instruction
// The assumption is this is called every 16.67ms, or at 60FPS
// (TODO: currently this is enforced with VSYNC, find a way to cap on non-vsync
// displays).
// All we need to do is simulate the right amount of cycles and
// return, and the platform layer is responsible for syncing/sleeping for the
// right amount of time between frames.
void stepEmulator(emulator_bridge *bridge) {
  emulator_state *state = bridge->emulatorState;
  int cyclesToAdvance = 0;
  while (state->cyclesDelta < CPU_CYCLES_PER_FRAME) {
    // Advance the clock 1 cpu instruction
    cyclesToAdvance = stepCPU(state);
    // Advance the ppu by the same amount of cycles
    advancePPU(state, cyclesToAdvance);
    state->cyclesDelta += cyclesToAdvance;
  }

  drawVramToBuffer(state, bridge->graphicsBuffer);

  state->cyclesDelta -= CPU_CYCLES_PER_FRAME;
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
  assert(byteValue < 0x08);
  return kilobytes(32) * (1 << byteValue);
}
