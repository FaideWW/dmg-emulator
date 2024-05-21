#include "emulator.h"

inline void setFlag(uint8_t *f, int flagBit) { *f = *f | (1 << flagBit); }
inline void clearFlag(uint8_t *f, int flagBit) { *f = *f & ~(1 << flagBit); }
inline bool checkFlag(uint8_t *f, int flagBit) { return (*f >> flagBit) & 1; }

inline uint16_t readRegister16(uint8_t hi, uint8_t lo) {
  return ((uint16_t)hi << 8) | lo;
}

inline void loadRegister8(uint8_t *reg, uint8_t value) { *reg = value; }

inline void loadRegister16(uint8_t *hi, uint8_t *lo, uint16_t value) {
  uint8_t hiByte = (value & 0xFF00) >> 8;
  uint8_t loByte = (value & 0x00FF);

  *hi = hiByte;
  *lo = loByte;
}

inline uint8_t consume8(emulator_state *state) {
  dmg_cpu cpu = state->cpu;
  uint8_t result = readMemory8(state, cpu.pc);
  cpu.pc++;
  return result;
}

inline uint16_t consume16(emulator_state *state) {
  dmg_cpu cpu = state->cpu;
  uint16_t result = readMemory16(state, cpu.pc);
  cpu.pc += 2;
  return result;
}

inline int incR16(uint8_t *hi, uint8_t *lo) {
  uint16_t value = readRegister16(*hi, *lo);
  value++;
  loadRegister16(hi, lo, value);
  return 4;
}

inline int incR8(uint8_t *reg, uint8_t *flags) {
  uint8_t value = *reg;
  // set halfcarry and zero flags
  if (value == 0xFF) {
    setFlag(flags, FLAG_H);
    setFlag(flags, FLAG_Z);
  } else {
    clearFlag(flags, FLAG_H);
    clearFlag(flags, FLAG_Z);
  }

  clearFlag(flags, FLAG_N);

  loadRegister8(reg, value + 1);
  return 0;
}

inline int decR8(uint8_t *reg, uint8_t *flags) {
  uint8_t value = *reg;
  // set halfcarry and zero flags
  if (value == 0x00) {
    setFlag(flags, FLAG_H);
  } else {
    clearFlag(flags, FLAG_H);
  }

  setFlag(flags, FLAG_N);

  uint8_t result = value - 1;
  if (result == 0x00) {
    setFlag(flags, FLAG_Z);
  } else {
    clearFlag(flags, FLAG_Z);
  }

  loadRegister8(reg, result);
  return 0;
}

int stepCPU(emulator_state *state) {
  dmg_cpu cpu = state->cpu;
  int cycles = 0;

  // Fetch instruction at the program counter
  uint8_t instruction = consume8(state);
  cycles += 4;

  /* uint8_t hiNibble = (instruction & 0xF0) >> 4; */
  /* uint8_t loNibble = instruction & 0x0F; */

  switch (instruction) {
  case 0x00: { // NOP
    break;
  }
  case 0x01: { // LD BC, n16
    uint16_t n = consume16(state);
    loadRegister16(&cpu.reg.b, &cpu.reg.c, n);
    cycles += 8;
    break;
  }
  case 0x02: { // LD [BC], A
    uint16_t addr = readRegister16(cpu.reg.b, cpu.reg.c);
    uint8_t value = readMemory8(state, addr);
    loadRegister8(&cpu.reg.a, value);
    cycles += 4;
    break;
  }
  case 0x03: { // INC BC
    cycles += incR16(&cpu.reg.b, &cpu.reg.c);
    break;
  }
  case 0x04: { // INC B
    cycles += incR8(&cpu.reg.b, &cpu.reg.f);
    break;
  }
  case 0x05: { // DEC B
    cycles += decR8(&cpu.reg.b, &cpu.reg.f);
    break;
  }
  case 0x06: { // NOP
    cycles = 4;
    break;
  }
  case 0x07: { // NOP
    cycles = 4;
    break;
  }
  }
  return cycles;
}
