#include "emulator.h"
#include <string.h>

// Read a uint8_t from memory, respecting all read rules
uint8_t readMemory8(emulator_state *state, uint16_t addr) {
  uint8_t result = state->memory.mainMemory[addr];
  return result;
}
// Read a uint16_t from memory, respecting all read rules
uint16_t readMemory16(emulator_state *state, uint16_t addr) {
  uint8_t hi = readMemory8(state, addr);
  uint8_t lo = readMemory8(state, addr + 8);
  return ((uint16_t)hi << 8) | lo;
}

// Read a uint16_t from memory, respecting all read rules
void readMemoryRegion(emulator_state *state, void *buffer, uint16_t start,
                      size_t len) {
  assert(start + len < 0xFFFF);
  memcpy(buffer, state->memory.mainMemory + start, len);
}

// Write a uint8_t to memory, respecting all write rules
void writeMemory8(emulator_state *state, uint16_t addr, uint8_t value) {
  state->memory.mainMemory[addr] = value;
}
void writeMemory16(emulator_state *state, uint16_t addr, uint16_t value) {
  uint8_t lo = value & 0x00FF;
  uint8_t hi = (value & 0xFF00) >> 8;
  writeMemory8(state, addr, hi);
  writeMemory8(state, addr + 8, lo);
}

void writeMemoryRegion(emulator_state *state, void *buffer, size_t bufLen,
                       uint16_t start) {
  assert(start + bufLen < 0xFFFF);
  memcpy(state->memory.mainMemory + start, buffer, bufLen);
}
