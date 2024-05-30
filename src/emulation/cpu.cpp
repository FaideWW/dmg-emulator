#include "emulator.h"

const char *printRegisterName(int regId) {
  switch (regId) {
  case 0:
    return "B";
  case 1:
    return "C";
  case 2:
    return "D";
  case 3:
    return "E";
  case 4:
    return "H";
  case 5:
    return "L";
  case 7:
    return "A";
  case 8:
    return "F";
  }
  return "?";
}

inline void logUnsupportedOpcode(emulator_state *state, uint8_t opcode) {
  debugLog.AddLog("[WARN] Unsupported opcode 0x%02hhX at address 0x%04hX\n",
                  opcode, state->cpu.pc);
}

inline uint16_t readRegister16(uint8_t hi, uint8_t lo) {
  return ((uint16_t)hi << 8) | lo;
}

// all of these operations return the number of cycles taken to perform them

// writes the byte at the program counter to v and advances the program counter
// by 1. cycles: 4
inline int consume8(emulator_state *state, uint8_t *dest) {
  dmg_cpu *cpu = &state->cpu;
  int cycles = 0;
  cycles += readMemory8(state, cpu->pc, dest);
  cpu->pc++;
  return cycles;
}
inline int consume8(emulator_state *state, int8_t *dest) {
  dmg_cpu *cpu = &state->cpu;
  int cycles = 0;
  uint8_t rawDest;
  cycles += readMemory8(state, cpu->pc, &rawDest);
  *dest = (int8_t)rawDest;
  cpu->pc++;
  return cycles;
}

// writes the short at the program counter to v and advances the program counter
// by 2. cycles: 8
inline int consume16(emulator_state *state, uint16_t *dest) {
  dmg_cpu *cpu = &state->cpu;
  int cycles = 0;
  cycles += readMemory16(state, cpu->pc, dest);
  cpu->pc += 2;
  return cycles;
}
inline int consume16(emulator_state *state, int16_t *dest) {
  dmg_cpu *cpu = &state->cpu;
  int cycles = 0;
  uint16_t rawDest;
  cycles += readMemory16(state, cpu->pc, &rawDest);
  *dest = (int16_t)rawDest;
  cpu->pc += 2;
  return cycles;
}

// pushes a 16bit value onto the stack, and decrements the stack pointer
// cycles: 12
inline int stackPush16(emulator_state *state, uint16_t value) {
  dmg_cpu *cpu = &state->cpu;
  int cycles = 0;
  // the stack pointer always points the the last byte on the stack, so in order
  // to write our 2-byte value we have to move the stack pointer down by 2 to
  // make room
  cpu->sp -= 2;
  cycles += writeMemory16(state, cpu->sp, value);
  cycles += 4;
  return cycles;
}

// pops a 16bit value off the stack, and increments the stack pointer
// cycles: 8
inline int stackPop16(emulator_state *state, uint16_t *value) {
  dmg_cpu *cpu = &state->cpu;
  int cycles = 0;
  cycles += readMemory16(state, cpu->sp, value);
  cpu->sp += 2;

  return cycles;
}

// loads `value` into the 8 bit register `reg`.
inline void ldR8(uint8_t *reg, uint8_t value) { *reg = value; }

// loads the short value into the 16 bit register `hilo`
inline void ldR16(uint8_t *hi, uint8_t *lo, uint16_t value) {
  uint8_t hiByte = (value & 0xFF00) >> 8;
  uint8_t loByte = (value & 0x00FF);

  *hi = hiByte;
  *lo = loByte;
}

// adds x + y, and updates the `flags` register
inline uint16_t add16(uint16_t x, int8_t y, uint8_t *flags) {
  // TODO: confirm the expected overflow behavior with a negative operand
  bool halfCarry = (x & 0xF) + (y & 0xF) > 0xF;
  if (halfCarry) { // H is set if we overflow into bit 4
    setBit(flags, FLAG_H);
  } else {
    clearBit(flags, FLAG_H);
  }

  bool carry = (x & 0xFF) + (y & 0xFF) > 0xFF;
  if (carry) {
    setBit(flags, FLAG_C);
  } else {
    clearBit(flags, FLAG_C);
  }

  clearBit(flags, FLAG_Z);
  clearBit(flags, FLAG_N);
  return x + y;
}

// adds x + y, and updates the `flags` register
inline uint8_t add8(uint8_t x, uint8_t y, uint8_t *flags, bool updateCarry) {

  bool halfCarry = (x & 0xF) + (y & 0xF) > 0xF;
  if (halfCarry) { // H is set if we overflow into bit 4
    setBit(flags, FLAG_H);
  } else {
    clearBit(flags, FLAG_H);
  }

  uint8_t result = x + y;
  if (result == 0x00) {
    setBit(flags, FLAG_Z);
  } else {
    clearBit(flags, FLAG_Z);
  }

  if (updateCarry) {
    uint16_t carryResult = x + y;
    if (carryResult > 0xFF) {
      setBit(flags, FLAG_C);
    } else {
      clearBit(flags, FLAG_C);
    }
  }

  clearBit(flags, FLAG_N);
  return x + y;
}

// adds x + y + carry flag, and updates the `flags` register
inline uint8_t adc8(uint8_t x, uint8_t y, uint8_t *flags) {
  uint8_t carry = checkBit(*flags, FLAG_C);
  return add8(x, y + carry, flags, true);
}

// increments the 8 bit register `reg`, and updates the `flags` register
inline void incR8(uint8_t *reg, uint8_t *flags) {
  uint8_t result = add8(*reg, 1, flags, false);
  ldR8(reg, result);
}

// adds `value` to the 8 bit register `reg`, and updates the `flags` register
inline void adcR8(uint8_t *reg, uint8_t value, uint8_t *flags) {
  uint8_t result = adc8(*reg, value, flags);
  ldR8(reg, result);
}

// adds `value` to the 8 bit register `reg`, and updates the `flags` register
inline void addR8(uint8_t *reg, uint8_t value, uint8_t *flags) {
  uint8_t result = add8(*reg, value, flags, true);
  ldR8(reg, result);
}

// increments the 16 bit register `hilo`
inline void incR16(uint8_t *hi, uint8_t *lo) {
  uint16_t value = readRegister16(*hi, *lo);
  ldR16(hi, lo, value + 1);
}

// subtracts y from x, and updates the `flags` register
inline uint8_t sub8(uint8_t x, uint8_t y, uint8_t *flags, bool updateCarry) {
  bool halfCarry = (int)(x & 0xF) - (int)(y & 0xF) < 0;
  if (halfCarry) { // H is set if we have to borrow from bit 4
    setBit(flags, FLAG_H);
  } else {
    clearBit(flags, FLAG_H);
  }

  uint8_t result = x - y;
  if (result == 0x00) {
    setBit(flags, FLAG_Z);
  } else {
    clearBit(flags, FLAG_Z);
  }

  if (updateCarry) {
    if (y > x) {
      setBit(flags, FLAG_C);
    } else {
      clearBit(flags, FLAG_C);
    }
  }

  setBit(flags, FLAG_N);
  return result;
}

// subtracts y and the carry flag from x, and updates the `flags` register
inline uint8_t sbc8(uint8_t x, uint8_t y, uint8_t *flags) {
  uint8_t carry = checkBit(*flags, FLAG_C);
  return sub8(x, y + carry, flags, true);
}

// decrements the 8 bit register `reg`, and updates the `flags` register
inline void decR8(uint8_t *reg, uint8_t *flags) {
  uint8_t result = sub8(*reg, 1, flags, false);
  ldR8(reg, result);
}

// adds `value` to the 8 bit register `reg`, and updates the `flags` register
inline void sbcR8(uint8_t *reg, uint8_t value, uint8_t *flags) {
  uint8_t result = sbc8(*reg, value, flags);
  ldR8(reg, result);
}

// adds `value` to the 8 bit register `reg`, and updates the `flags` register
inline void subR8(uint8_t *reg, uint8_t value, uint8_t *flags) {
  uint8_t result = sub8(*reg, value, flags, true);
  ldR8(reg, result);
}

// bitwise ANDs `value` into 8 bit register `reg`
inline void andR8(uint8_t *reg, uint8_t value, uint8_t *flags) {
  uint8_t result = *reg & value;
  if (result == 0x00) {
    setBit(flags, FLAG_Z);
  } else {
    clearBit(flags, FLAG_Z);
  }

  clearBit(flags, FLAG_N);
  setBit(flags, FLAG_H);
  clearBit(flags, FLAG_C);

  ldR8(reg, result);
}

// bitwise XORs `value` into 8 bit register `reg`
inline void xorR8(uint8_t *reg, uint8_t value, uint8_t *flags) {
  uint8_t result = *reg ^ value;
  if (result == 0x00) {
    setBit(flags, FLAG_Z);
  } else {
    clearBit(flags, FLAG_Z);
  }

  clearBit(flags, FLAG_N);
  clearBit(flags, FLAG_H);
  clearBit(flags, FLAG_C);

  ldR8(reg, result);
}

// bitwise ORs `value` into 8 bit register `reg`
inline void orR8(uint8_t *reg, uint8_t value, uint8_t *flags) {
  uint8_t result = *reg | value;
  if (result == 0x00) {
    setBit(flags, FLAG_Z);
  } else {
    clearBit(flags, FLAG_Z);
  }

  clearBit(flags, FLAG_N);
  clearBit(flags, FLAG_H);
  clearBit(flags, FLAG_C);

  ldR8(reg, result);
}

// Shifts an 8-bit value left, updating the flags register
inline uint8_t sla8(uint8_t value, uint8_t *flags) {
  bool leftBit = checkBit(value, 7);
  if (leftBit == 1) {
    setBit(flags, FLAG_C);
  } else {
    clearBit(flags, FLAG_C);
  }

  uint8_t result = (value << 1);
  if (result == 0) {
    setBit(flags, FLAG_Z);
  } else {
    clearBit(flags, FLAG_Z);
  }
  clearBit(flags, FLAG_N);
  clearBit(flags, FLAG_H);
  return result;
}

// rotates an 8-bit value left, updating the flags register
inline uint8_t rlc8(uint8_t value, uint8_t *flags) {
  bool leftBit = checkBit(value, 7);
  uint8_t result = sla8(value, flags) | leftBit;
  if (result == 0) {
    setBit(flags, FLAG_Z);
  } else {
    clearBit(flags, FLAG_Z);
  }
  return result;
}

// rotates an 8 bit value left, through the carry flag, updating
// the flags register
inline uint8_t rl8(uint8_t value, uint8_t *flags) {
  bool carry = checkBit(*flags, FLAG_C);
  uint8_t result = sla8(value, flags) | carry;
  if (result == 0) {
    setBit(flags, FLAG_Z);
  } else {
    clearBit(flags, FLAG_Z);
  }
  return result;
}

// shifts an 8-bit value right arithmetically (bit 7 is unchanged),
// updating the flags register
inline uint8_t sra8(uint8_t value, uint8_t *flags) {
  bool rightBit = checkBit(value, 0);
  if (rightBit == 1) {
    setBit(flags, FLAG_C);
  } else {
    clearBit(flags, FLAG_C);
  }

  uint8_t result = (value >> 1) | (value & 0x80);
  if (result == 0) {
    setBit(flags, FLAG_Z);
  } else {
    clearBit(flags, FLAG_Z);
  }
  clearBit(flags, FLAG_N);
  clearBit(flags, FLAG_H);
  return result;
}
// shifts an 8-bit value right logically (bit 7 is 0),
// updating the flags register
inline uint8_t srl8(uint8_t value, uint8_t *flags) {
  bool rightBit = checkBit(value, 0);
  if (rightBit == 1) {
    setBit(flags, FLAG_C);
  } else {
    clearBit(flags, FLAG_C);
  }

  uint8_t result = (value >> 1);
  if (result == 0) {
    setBit(flags, FLAG_Z);
  } else {
    clearBit(flags, FLAG_Z);
  }
  clearBit(flags, FLAG_N);
  clearBit(flags, FLAG_H);
  return result;
}

// rotates an 8-bit value right, updating the flags register
inline uint8_t rrc8(uint8_t value, uint8_t *flags) {
  bool rightBit = checkBit(value, 0);
  uint8_t result = srl8(value, flags) | (rightBit << 7);
  if (result == 0) {
    setBit(flags, FLAG_Z);
  } else {
    clearBit(flags, FLAG_Z);
  }
  return result;
}

// rotates an 8 bit value right, through the carry flag, updating
// the flags register
inline uint8_t rr8(uint8_t value, uint8_t *flags) {
  bool carry = checkBit(*flags, FLAG_C);
  uint8_t result = srl8(value, flags) | (carry << 7);
  if (result == 0) {
    setBit(flags, FLAG_Z);
  } else {
    clearBit(flags, FLAG_Z);
  }
  return result;
}

inline uint8_t swap8(uint8_t value, uint8_t *flags) {
  uint8_t hiNibble = (value & 0xF0) >> 4;
  uint8_t loNibble = value & 0x0F;

  uint8_t result = (loNibble << 4) | hiNibble;
  if (result == 0x00) {
    setBit(flags, FLAG_Z);
  } else {
    clearBit(flags, FLAG_Z);
  }

  clearBit(flags, FLAG_N);
  clearBit(flags, FLAG_H);
  clearBit(flags, FLAG_C);
  return result;
}

// maps a 4-bit value to a register value, including reading from memory if
// needed, and assigns that value to `dest`
// cycles: 4 if a memory read occurs, 0 otherwise
inline int nibbleDecode(emulator_state *state, uint8_t nibble, uint8_t *dest,
                        int *regId) {
  dmg_cpu *cpu = &state->cpu;
  uint8_t value;
  int cycles = 0;

  if ((nibble % 0x08) == 0x6) {
    uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
    cycles += readMemory8(state, addr, &value);
  } else {
    switch (nibble % 0x08) {
    case 0x0:
      value = cpu->reg.b;
      *regId = REGID_B;
      break;
    case 0x1:
      value = cpu->reg.c;
      *regId = REGID_C;
      break;
    case 0x2:
      value = cpu->reg.d;
      *regId = REGID_D;
      break;
    case 0x3:
      value = cpu->reg.e;
      *regId = REGID_E;
      break;
    case 0x4:
      value = cpu->reg.h;
      *regId = REGID_H;
      break;
    case 0x5:
      value = cpu->reg.l;
      *regId = REGID_L;
      break;
    case 0x7:
      value = cpu->reg.a;
      *regId = REGID_A;
      break;
    }
  }
  *dest = value;
  return cycles;
}

// maps a 4-bit value to a register pointer, and assigns it to `dest`. reutrns
// NULL if `nibble` == 0x6 or 0xE, as these are memory-mapped values and not
// registers
inline uint8_t *registerDecode(emulator_state *state, uint8_t nibble,
                               int *regId) {
  dmg_cpu *cpu = &state->cpu;
  uint8_t *dest = NULL;
  if ((nibble % 0x08) != 0x6) {
    switch (nibble % 0x08) {
    case 0x0:
      dest = &cpu->reg.b;
      *regId = REGID_B;
      break;
    case 0x1:
      dest = &cpu->reg.c;
      *regId = REGID_C;
      break;
    case 0x2:
      dest = &cpu->reg.d;
      *regId = REGID_D;
      break;
    case 0x3:
      dest = &cpu->reg.e;
      *regId = REGID_E;
      break;
    case 0x4:
      dest = &cpu->reg.h;
      *regId = REGID_H;
      break;
    case 0x5:
      dest = &cpu->reg.l;
      *regId = REGID_L;
      break;
    case 0x7:
      dest = &cpu->reg.a;
      *regId = REGID_A;
      break;
    }
  }
  return dest;
}

// --------- 16 bit operations -----------

// decrements the 16 bit register `hilo`
inline void decR16(uint8_t *hi, uint8_t *lo) {
  uint16_t value = readRegister16(*hi, *lo);
  ldR16(hi, lo, value - 1);
}

// adds `val` to the value in 16 bit register `hilo`, and updates the `flags`
// register cycles: 4
inline int addR16(uint8_t *hi, uint8_t *lo, uint16_t val, uint8_t *flags) {
  uint16_t hilo = readRegister16(*hi, *lo);
  bool halfCarry = checkBit16(val, 11) && checkBit16(hilo, 11);
  bool carry = checkBit16(val, 15) && checkBit16(hilo, 15);
  ldR16(hi, lo, hilo + val);
  setBit(flags, FLAG_N);
  if (halfCarry) {
    setBit(flags, FLAG_H);
  } else {
    clearBit(flags, FLAG_H);
  }
  if (carry) {
    setBit(flags, FLAG_C);
  } else {
    clearBit(flags, FLAG_C);
  }
  return 4;
}

int doExtendedInstruction(emulator_state *state);
int stepCPU(emulator_state *state) {
  dmg_cpu *cpu = &state->cpu;
  int cycles = 0;

  uint16_t pcStart = cpu->pc;
  // Fetch instruction at the program counter
  uint8_t opcode;
  cycles += consume8(state, &opcode);

  uint8_t hiNibble = (opcode & 0xF0) >> 4;
  uint8_t loNibble = opcode & 0x0F;

  debugLog.AddLog("[$%04hX][%02X] ", pcStart, opcode);

  switch (opcode) {
  case 0x00: { // NOP
    debugLog.AddLog("NOP;");
    break;
  }
  case 0x01: { // LD BC, n16
    uint16_t n;
    cycles += consume16(state, &n);
    ldR16(&cpu->reg.b, &cpu->reg.c, n);
    debugLog.AddLog("LD BC, %04hX;", n);
    break;
  }
  case 0x02: { // LD [BC], A
    uint16_t addr = readRegister16(cpu->reg.b, cpu->reg.c);
    cycles += writeMemory8(state, addr, cpu->reg.a);
    debugLog.AddLog("LD [BC], A; // BC=%04hx", addr);
    break;
  }
  case 0x03: { // INC BC
    incR16(&cpu->reg.b, &cpu->reg.c);
    debugLog.AddLog("INC BC;");
    break;
  }
  case 0x04: { // INC B
    incR8(&cpu->reg.b, &cpu->reg.f);
    debugLog.AddLog("INC B;");
    break;
  }
  case 0x05: { // DEC B
    decR8(&cpu->reg.b, &cpu->reg.f);
    debugLog.AddLog("DEC B;");
    break;
  }
  case 0x06: { // LD B, n8
    uint8_t n;
    cycles += consume8(state, &n);
    ldR8(&cpu->reg.b, n);
    debugLog.AddLog("LD B, %02X;", n);
    break;
  }
  case 0x07: { // RLCA
    uint8_t value = rlc8(cpu->reg.a, &cpu->reg.f);
    ldR8(&cpu->reg.a, value);
    debugLog.AddLog("RLCA;");
    break;
  }
  case 0x08: { // LD [a16], SP
    uint16_t addr;
    cycles += consume16(state, &addr);
    cycles += writeMemory16(state, addr, cpu->sp);
    debugLog.AddLog("LD [a16], SP; // a16=%04hX", addr);
    break;
  }
  case 0x09: { // ADD HL, BC
    uint16_t val = readRegister16(cpu->reg.b, cpu->reg.c);
    cycles += addR16(&cpu->reg.h, &cpu->reg.l, val, &cpu->reg.f);
    debugLog.AddLog("LD HL, BC;");
    break;
  }
  case 0x0A: { // LD A, [BC]
    uint16_t addr = readRegister16(cpu->reg.b, cpu->reg.c);
    uint8_t value;
    cycles += readMemory8(state, addr, &value);
    ldR8(&cpu->reg.a, value);
    debugLog.AddLog("LD HL, BC;");
    break;
  }
  case 0x0B: { // DEC BC
    decR16(&cpu->reg.b, &cpu->reg.c);
    debugLog.AddLog("DEC BC;");
    break;
  }
  case 0x0C: { // INC C
    incR8(&cpu->reg.c, &cpu->reg.f);
    debugLog.AddLog("INC C;");
    break;
  }
  case 0x0D: { // DEC C
    decR8(&cpu->reg.c, &cpu->reg.f);
    debugLog.AddLog("DEC C;");
    break;
  }
  case 0x0E: { // LD C, n8
    uint8_t n;
    cycles += consume8(state, &n);
    ldR8(&cpu->reg.c, n);
    debugLog.AddLog("LD C, %02X;", n);
    break;
  }
  case 0x0F: { // RRCA
    // Rotates right register `a` by 1 bit, setting the carry flag to the bit
    // that falls out (bit 0)
    uint8_t value = rrc8(cpu->reg.a, &cpu->reg.f);
    ldR8(&cpu->reg.a, value);
    debugLog.AddLog("RRCA;");
    break;
  }

  case 0x10: { // STOP n8
    // note: STOP always consumes 2 bytes but the n8 value is not used
    uint8_t n;
    cycles += consume8(state, &n);
    // STOP instruction resets the divider register
    internal_writeMemory8(state, MEM_DIV, 0x00);
    cpu->isStopped = true;
    debugLog.AddLog("STOP; // n8=%02X", n);
    break;
  }
  case 0x11: { // LD DE, n16
    uint16_t value;
    cycles += consume16(state, &value);
    ldR16(&cpu->reg.d, &cpu->reg.e, value);
    debugLog.AddLog("LD DE, %04hX;", value);
    break;
  }
  case 0x12: { // LD [DE], A
    uint16_t addr = readRegister16(cpu->reg.d, cpu->reg.e);
    cycles += writeMemory8(state, addr, cpu->reg.a);
    debugLog.AddLog("LD [DE], A; // DE=%04hX", addr);
    break;
  }
  case 0x13: { // INC DE
    incR16(&cpu->reg.d, &cpu->reg.e);
    debugLog.AddLog("INC DE;");
    break;
  }
  case 0x14: { // INC D
    incR8(&cpu->reg.d, &cpu->reg.f);
    debugLog.AddLog("INC D;");
    break;
  }
  case 0x15: { // DEC D
    decR8(&cpu->reg.d, &cpu->reg.f);
    debugLog.AddLog("DEC D;");
    break;
  }
  case 0x16: { // LD D, n8
    uint8_t n;
    cycles += consume8(state, &n);
    ldR8(&cpu->reg.d, n);
    debugLog.AddLog("LD D, %02X;", n);
    break;
  }
  case 0x17: { // RLA
    // Rotates left register `a` by 1 bit through the carry flag, setting the
    // carry flag to the bit that falls out (bit 7) and updating bit 0 to the
    // previous value of the carry flag
    uint8_t value = rl8(cpu->reg.a, &cpu->reg.f);
    ldR8(&cpu->reg.a, value);
    debugLog.AddLog("RLA;");
    break;
  }
  case 0x18: { // JR e8
    // note that e8 is a signed value
    int8_t e;
    cycles += consume8(state, &e);
    cpu->pc = cpu->pc + e;
    cycles += 4;
    debugLog.AddLog("JR %d;", e);
    break;
  }
  case 0x19: { // ADD HL, DE
    uint16_t val = readRegister16(cpu->reg.d, cpu->reg.e);
    cycles += addR16(&cpu->reg.h, &cpu->reg.l, val, &cpu->reg.f);
    debugLog.AddLog("ADD HL, DE");
    break;
  }
  case 0x1A: { // LD A, [DE]
    uint16_t addr = readRegister16(cpu->reg.d, cpu->reg.e);
    uint8_t value;
    cycles += readMemory8(state, addr, &value);
    ldR8(&cpu->reg.a, value);
    debugLog.AddLog("LD A, [DE]; // DE=%04hX", addr);
    break;
  }
  case 0x1B: { // DEC DE
    decR16(&cpu->reg.d, &cpu->reg.e);
    debugLog.AddLog("DEC DE;");
    break;
  }
  case 0x1C: { // INC E
    incR8(&cpu->reg.e, &cpu->reg.f);
    debugLog.AddLog("INC E;");
    break;
  }
  case 0x1D: { // DEC E
    decR8(&cpu->reg.e, &cpu->reg.f);
    debugLog.AddLog("DEC E;");
    break;
  }
  case 0x1E: { // LD E, n8
    uint8_t n;
    cycles += consume8(state, &n);
    ldR8(&cpu->reg.e, n);
    debugLog.AddLog("LD E, %02X;", n);
    break;
  }
  case 0x1F: { // RRA
    // Rotates right register `a` by 1 bit through the carry flag, setting the
    // carry flag to the bit that falls out (bit 0) and updating bit 7 to the
    // previous value of the carry flag
    uint8_t value = rr8(cpu->reg.a, &cpu->reg.f);
    ldR8(&cpu->reg.a, value);
    debugLog.AddLog("RRA;");
    break;
  }

  case 0x20: { // JR NZ, e8
    // note that e8 is a signed value
    int8_t e;
    cycles += consume8(state, &e);
    if (!checkBit(cpu->reg.f, FLAG_Z)) {
      cpu->pc = cpu->pc + e;
      cycles += 4;
    }
    debugLog.AddLog("JR NZ, %d;", e);
    break;
  }
  case 0x21: { // LD HL, n16
    uint16_t value;
    cycles += consume16(state, &value);
    ldR16(&cpu->reg.h, &cpu->reg.l, value);
    debugLog.AddLog("LD HL, %04hX;", value);
    break;
  }
  case 0x22: { // LD [HL+] A
    uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
    cycles += writeMemory8(state, addr, cpu->reg.a);
    debugLog.AddLog("LD [HL+], A; // HL=%04hX", addr);
    incR16(&cpu->reg.h, &cpu->reg.l);
    break;
  }
  case 0x23: { // INC HL
    incR16(&cpu->reg.h, &cpu->reg.l);
    debugLog.AddLog("INC HL;");
    break;
  }
  case 0x24: { // INC H
    incR8(&cpu->reg.h, &cpu->reg.f);
    debugLog.AddLog("INC H;");
    break;
  }
  case 0x25: { // DEC H
    decR8(&cpu->reg.h, &cpu->reg.f);
    debugLog.AddLog("DEC H;");
    break;
  }
  case 0x26: { // LD H, n8
    uint8_t n;
    cycles += consume8(state, &n);
    ldR8(&cpu->reg.h, n);
    debugLog.AddLog("LD H, %02X;", n);
    break;
  }
  case 0x27: { // DAA
    // TODO:
    // https://www.righto.com/2023/01/understanding-x86s-decimal-adjust-after.html
    debugLog.AddLog("DAA;");
    break;
  }
  case 0x28: { // JR Z, e8
    // note that e8 is a signed value
    int8_t e;
    cycles += consume8(state, &e);
    if (checkBit(cpu->reg.f, FLAG_Z)) {
      cpu->pc = cpu->pc + e;
      cycles += 4;
    }
    debugLog.AddLog("JR Z, %d;", e);
    break;
  }
  case 0x29: { // ADD HL, HL
    uint16_t val = readRegister16(cpu->reg.h, cpu->reg.l);
    cycles += addR16(&cpu->reg.h, &cpu->reg.l, val, &cpu->reg.f);
    debugLog.AddLog("ADD HL, HL;");
    break;
  }
  case 0x2A: { // LD A, [HL+]
    uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
    uint8_t value;
    cycles += readMemory8(state, addr, &value);
    ldR8(&cpu->reg.a, value);
    debugLog.AddLog("LD A, [HL+]; // HL=%04hX", addr);
    incR16(&cpu->reg.h, &cpu->reg.l);
    break;
  }
  case 0x2B: { // DEC HL
    decR16(&cpu->reg.h, &cpu->reg.l);
    debugLog.AddLog("DEC HL;");
    break;
  }
  case 0x2C: { // INC L
    incR8(&cpu->reg.l, &cpu->reg.f);
    debugLog.AddLog("INC L;");
    break;
  }
  case 0x2D: { // DEC L
    decR8(&cpu->reg.l, &cpu->reg.f);
    debugLog.AddLog("DEC L;");
    break;
  }
  case 0x2E: { // LD L, n8
    uint8_t n;
    cycles += consume8(state, &n);
    ldR8(&cpu->reg.l, n);
    debugLog.AddLog("LD L, %02X;", n);
    break;
  }
  case 0x2F: { // CPL
    ldR8(&cpu->reg.a, ~cpu->reg.a);
    debugLog.AddLog("CPL;");
    break;
  }

  case 0x30: { // JR NC, e8
    // note that e8 is a signed value
    int8_t e;
    cycles += consume8(state, &e);
    if (!checkBit(cpu->reg.f, FLAG_C)) {
      cpu->pc = cpu->pc + e;
      cycles += 4; // for updating pc
    }
    debugLog.AddLog("JR NC, %d;", e);
    break;
  }
  case 0x31: { // LD SP, n16
    uint16_t value;
    cycles += consume16(state, &value);
    cpu->sp = value;
    debugLog.AddLog("LD SP, %04hX;", value);
    break;
  }
  case 0x32: { // LD [HL-], A
    uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
    cycles += writeMemory8(state, addr, cpu->reg.a);
    decR16(&cpu->reg.h, &cpu->reg.l);
    debugLog.AddLog("LD [HL-], A; // HL=%04hX", addr);
    break;
  }
  case 0x33: { // INC SP
    cpu->sp++;
    debugLog.AddLog("INC SP;");
    break;
  }
  case 0x34: { // INC [HL]
    uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
    uint8_t value;
    cycles += readMemory8(state, addr, &value);
    uint8_t result = add8(value, 1, &cpu->reg.f, false);
    cycles += writeMemory8(state, addr, result);
    debugLog.AddLog("INC [HL]; // HL=%04hX", addr);
    break;
  }
  case 0x35: { // DEC [HL]
    uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
    uint8_t value;
    cycles += readMemory8(state, addr, &value);
    uint8_t result = sub8(value, 1, &cpu->reg.f, false);
    cycles += writeMemory8(state, addr, result);
    debugLog.AddLog("DEC [HL]; // HL=%04hX", addr);
    break;
  }
  case 0x36: { // LD [HL], n8
    uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
    uint8_t value;
    cycles += consume8(state, &value);
    cycles += writeMemory8(state, addr, value);
    debugLog.AddLog("LD [HL], %02X; // HL=%04hX", value, addr);
    break;
  }
  case 0x37: { // SCF
    clearBit(&cpu->reg.f, FLAG_N);
    clearBit(&cpu->reg.f, FLAG_H);
    setBit(&cpu->reg.f, FLAG_C);
    debugLog.AddLog("SCF;");
    break;
  }
  case 0x38: { // JR C, e8
    // note that e8 is a signed value
    int8_t e;
    cycles += consume8(state, &e);
    if (checkBit(cpu->reg.f, FLAG_C)) {
      cpu->pc = cpu->pc + e;
      cycles += 4;
    }
    debugLog.AddLog("JR C, %d;", e);
    break;
  }
  case 0x39: { // ADD HL, SP
    cycles += addR16(&cpu->reg.h, &cpu->reg.l, cpu->sp, &cpu->reg.f);
  }
  case 0x3A: { // LD A, [HL-]
    uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
    uint8_t value;
    cycles += readMemory8(state, addr, &value);
    ldR8(&cpu->reg.a, value);
    decR16(&cpu->reg.h, &cpu->reg.l);
    debugLog.AddLog("ADD HL, SP;");
    break;
  }
  case 0x3B: { // DEC SP
    cpu->sp--;
    debugLog.AddLog("DEC SP;");
    break;
  }
  case 0x3C: { // INC A
    incR8(&cpu->reg.a, &cpu->reg.f);
    debugLog.AddLog("INC A;");
    break;
  }
  case 0x3D: { // DEC A
    decR8(&cpu->reg.a, &cpu->reg.f);
    debugLog.AddLog("DEC A;");
    break;
  }
  case 0x3E: { // LD A, n8
    uint8_t n;
    cycles += consume8(state, &n);
    ldR8(&cpu->reg.a, n);
    debugLog.AddLog("LD A, %02X;", n);
    break;
  }
  case 0x3F: { // CCF
    clearBit(&cpu->reg.f, FLAG_N);
    clearBit(&cpu->reg.f, FLAG_H);
    toggleBit(&cpu->reg.f, FLAG_C);
    debugLog.AddLog("CCF;");
    break;
  }

    // TODO: clean this up, wtf

  case 0x40: // LD B, B
  case 0x41: // LD B, C
  case 0x42: // LD B, D
  case 0x43: // LD B, E
  case 0x44: // LD B, H
  case 0x45: // LD B, L
  case 0x46: // LD B, [HL]
  case 0x47: // LD B, A
  case 0x48: // LD C, B
  case 0x49: // LD C, C
  case 0x4A: // LD C, D
  case 0x4B: // LD C, E
  case 0x4C: // LD C, H
  case 0x4D: // LD C, L
  case 0x4E: // LD C, [HL]
  case 0x4F: // LD C, A
  case 0x50: // LD D, B
  case 0x51: // LD D, C
  case 0x52: // LD D, D
  case 0x53: // LD D, E
  case 0x54: // LD D, H
  case 0x55: // LD D, L
  case 0x56: // LD D, [HL]
  case 0x57: // LD D, A
  case 0x58: // LD E, B
  case 0x59: // LD E, C
  case 0x5A: // LD E, D
  case 0x5B: // LD E, E
  case 0x5C: // LD E, H
  case 0x5D: // LD E, L
  case 0x5E: // LD E, [HL]
  case 0x5F: // LD E, A
  case 0x60: // LD H, B
  case 0x61: // LD H, C
  case 0x62: // LD H, D
  case 0x63: // LD H, E
  case 0x64: // LD H, H
  case 0x65: // LD H, L
  case 0x66: // LD H, [HL]
  case 0x67: // LD H, A
  case 0x68: // LD L, B
  case 0x69: // LD L, C
  case 0x6A: // LD L, D
  case 0x6B: // LD L, E
  case 0x6C: // LD L, H
  case 0x6D: // LD L, L
  case 0x6E: // LD L, [HL]
  case 0x6F: // LD L, A
  case 0x70: // LD [HL], B
  case 0x71: // LD [HL], C
  case 0x72: // LD [HL], D
  case 0x73: // LD [HL], E
  case 0x74: // LD [HL], H
  case 0x75: // LD [HL], L
  case 0x77: // LD [HL], A
  case 0x78: // LD A, B
  case 0x79: // LD A, C
  case 0x7A: // LD A, D
  case 0x7B: // LD A, E
  case 0x7C: // LD A, H
  case 0x7D: // LD A, L
  case 0x7E: // LD A, [HL]
  case 0x7F: // LD A, A
  {
    uint8_t value;
    int regId;
    cycles += nibbleDecode(state, loNibble, &value, &regId);
    debugLog.AddLog("LD ");
    if (hiNibble == 0x7 && loNibble < 0x8) { // writing to memory
      uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
      debugLog.AddLog("[HL], %s;", printRegisterName(regId));
      cycles += writeMemory8(state, addr, value);
    } else {
      uint8_t *reg;
      switch (hiNibble) {
      case 0x4: {
        if (loNibble < 0x8) {
          reg = &cpu->reg.b;
          debugLog.AddLog("B, %s;", printRegisterName(regId));
        } else {
          reg = &cpu->reg.c;
          debugLog.AddLog("C, %s;", printRegisterName(regId));
        }
        break;
      }
      case 0x5: {
        if (loNibble < 0x8) {
          reg = &cpu->reg.d;
          debugLog.AddLog("D, %s;", printRegisterName(regId));
        } else {
          reg = &cpu->reg.e;
          debugLog.AddLog("E, %s;", printRegisterName(regId));
        }
        break;
      }
      case 0x6: {
        if (loNibble < 0x8) {
          reg = &cpu->reg.h;
          debugLog.AddLog("H, %s;", printRegisterName(regId));
        } else {
          reg = &cpu->reg.l;
          debugLog.AddLog("L, %s;", printRegisterName(regId));
        }
        break;
      }
      case 0x7: {
        reg = &cpu->reg.a;
        debugLog.AddLog("A, %s;", printRegisterName(regId));
        break;
      }
      }
      ldR8(reg, value);
    }
    break;
  }

  case 0x76: { // HALT
    cpu->isHalted = true;
    break;
  }

  case 0x80: // ADD A, B
  case 0x81: // ADD A, C
  case 0x82: // ADD A, D
  case 0x83: // ADD A, E
  case 0x84: // ADD A, H
  case 0x85: // ADD A, L
  case 0x86: // ADD A, [HL]
  case 0x87: // ADD A, A
  case 0x88: // ADC A, B
  case 0x89: // ADC A, C
  case 0x8A: // ADC A, D
  case 0x8B: // ADC A, E
  case 0x8C: // ADC A, H
  case 0x8D: // ADC A, L
  case 0x8E: // ADC A, [HL]
  case 0x8F: // ADC A, A
  {
    uint8_t value;
    int regId;
    cycles += nibbleDecode(state, loNibble, &value, &regId);

    if (loNibble < 0x08) {
      addR8(&cpu->reg.a, value, &cpu->reg.f);
      debugLog.AddLog("ADD A, %s;", printRegisterName(regId));
    } else {
      adcR8(&cpu->reg.a, value, &cpu->reg.f);
      debugLog.AddLog("ADC A, %s;", printRegisterName(regId));
    }
    break;
  }

  case 0x90: // SUB A, B
  case 0x91: // SUB A, C
  case 0x92: // SUB A, D
  case 0x93: // SUB A, E
  case 0x94: // SUB A, H
  case 0x95: // SUB A, L
  case 0x96: // SUB A, [HL]
  case 0x97: // SUB A, A
  case 0x98: // SBC A, B
  case 0x99: // SBC A, C
  case 0x9A: // SBC A, D
  case 0x9B: // SBC A, E
  case 0x9C: // SBC A, H
  case 0x9D: // SBC A, L
  case 0x9E: // SBC A, [HL]
  case 0x9F: // SBC A, A
  {
    uint8_t value;
    int regId;
    cycles += nibbleDecode(state, loNibble, &value, &regId);

    if (loNibble < 0x08) {
      subR8(&cpu->reg.a, value, &cpu->reg.f);
      debugLog.AddLog("SUB A, %s;", printRegisterName(regId));
    } else {
      sbcR8(&cpu->reg.a, value, &cpu->reg.f);
      debugLog.AddLog("SBC A, %s;", printRegisterName(regId));
    }
    break;
  }

  case 0xA0: // AND A, B
  case 0xA1: // AND A, C
  case 0xA2: // AND A, D
  case 0xA3: // AND A, E
  case 0xA4: // AND A, H
  case 0xA5: // AND A, L
  case 0xA6: // AND A, [HL]
  case 0xA7: // AND A, A
  case 0xA8: // XOR A, B
  case 0xA9: // XOR A, C
  case 0xAA: // XOR A, D
  case 0xAB: // XOR A, E
  case 0xAC: // XOR A, H
  case 0xAD: // XOR A, L
  case 0xAE: // XOR A, [HL]
  case 0xAF: // XOR A, A
  {
    uint8_t value;
    int regId;
    cycles += nibbleDecode(state, loNibble, &value, &regId);

    if (loNibble < 0x08) {
      andR8(&cpu->reg.a, value, &cpu->reg.f);
      debugLog.AddLog("AND A, %s;", printRegisterName(regId));
    } else {
      xorR8(&cpu->reg.a, value, &cpu->reg.f);
      debugLog.AddLog("XOR A, %s;", printRegisterName(regId));
    }
    break;
  }
  case 0xB0: // OR A, B
  case 0xB1: // OR A, C
  case 0xB2: // OR A, D
  case 0xB3: // OR A, E
  case 0xB4: // OR A, H
  case 0xB5: // OR A, L
  case 0xB6: // OR A, [HL]
  case 0xB7: // OR A, A
  case 0xB8: // CP A, B
  case 0xB9: // CP A, C
  case 0xBA: // CP A, D
  case 0xBB: // CP A, E
  case 0xBC: // CP A, H
  case 0xBD: // CP A, L
  case 0xBE: // CP A, [HL]
  case 0xBF: // CP A, A
  {
    uint8_t value;
    int regId;
    cycles += nibbleDecode(state, loNibble, &value, &regId);

    if (loNibble < 0x08) {
      orR8(&cpu->reg.a, value, &cpu->reg.f);
      debugLog.AddLog("OR A, %s;", printRegisterName(regId));
    } else {
      // the CP instruction subtracts a value from A, updates the flags but does
      // not store the result
      sub8(cpu->reg.a, value, &cpu->reg.f, true);
      debugLog.AddLog("CP A, %s;", printRegisterName(regId));
    }
    break;
  }

  case 0xC0: { // RET NZ
    // 4 cycles for the flag check
    cycles += 4;
    if (!checkBit(cpu->reg.f, FLAG_Z)) {
      cycles += stackPop16(state, &cpu->pc);
      cycles += 4;
    }
    debugLog.AddLog("RET NZ;");
    break;
  }
  case 0xC1: { // POP BC
    uint16_t result;
    cycles += stackPop16(state, &result);
    ldR16(&cpu->reg.b, &cpu->reg.c, result);
    debugLog.AddLog("POP BC;");
    break;
  }
  case 0xC2: { // JP NZ, a16
    uint16_t addr;
    cycles += consume16(state, &addr);
    if (!checkBit(cpu->reg.f, FLAG_Z)) {
      cpu->pc = addr;
      cycles += 4; // 4 for updating the pc
    }
    debugLog.AddLog("JP NZ, %04hX;", addr);
    break;
  }
  case 0xC3: { // JP a16
    uint16_t addr;
    cycles += consume16(state, &addr);
    cpu->pc = addr;
    cycles += 4; // 4 for updating the pc
    debugLog.AddLog("JP %04hX;", addr);
    break;
  }
  case 0xC4: { // CALL NZ, a16
    uint16_t addr;
    cycles += consume16(state, &addr);
    if (!checkBit(cpu->reg.f, FLAG_Z)) {
      cycles += stackPush16(state, cpu->pc);
      cpu->pc = addr;
    }
    debugLog.AddLog("CALL NZ, %04hX;", addr);
    break;
  }
  case 0xC5: { // PUSH BC
    uint16_t value = readRegister16(cpu->reg.b, cpu->reg.c);
    cycles += stackPush16(state, value);
    debugLog.AddLog("PUSH BC;");
    break;
  }
  case 0xC6: { // ADD A, n8
    uint8_t n;
    cycles += consume8(state, &n);
    addR8(&cpu->reg.a, n, &cpu->reg.f);
    debugLog.AddLog("ADD A, %02X;", n);
    break;
  }
  case 0xC7: { // RST $00
    uint16_t n = 0x0000;
    cycles += stackPush16(state, cpu->pc);
    cpu->pc = n;
    debugLog.AddLog("RST $00;");
    break;
  }
  case 0xC8: { // RET Z
    cycles += 4;
    if (checkBit(cpu->reg.f, FLAG_Z)) {
      cycles += stackPop16(state, &cpu->pc);
      cycles += 4;
    }
    debugLog.AddLog("RET Z;");
    break;
  }
  case 0xC9: { // RET
    cycles += stackPop16(state, &cpu->pc);
    cycles += 4;
    debugLog.AddLog("RET;");
    break;
  }
  case 0xCA: { // JP Z, a16
    uint16_t addr;
    cycles += consume16(state, &addr);
    if (checkBit(cpu->reg.f, FLAG_Z)) {
      cpu->pc = addr;
      cycles += 4; // 4 for updating the pc
    }
    debugLog.AddLog("JP Z, %04hX;", addr);
    break;
  }
  case 0xCB: { // call extended instruction table
    cycles += doExtendedInstruction(state);
    break;
  }
  case 0xCC: { // CALL Z, a16
    uint16_t addr;
    cycles += consume16(state, &addr);
    if (checkBit(cpu->reg.f, FLAG_Z)) {
      cycles += stackPush16(state, cpu->pc);
      cpu->pc = addr;
    }
    debugLog.AddLog("CALL Z, %04hX;", addr);
    break;
  }
  case 0xCD: { // CALL a16
    uint16_t addr;
    cycles += consume16(state, &addr);
    cycles += stackPush16(state, cpu->pc);
    cpu->pc = addr;
    debugLog.AddLog("CALL %04hX;", addr);
    break;
  }
  case 0xCE: { // ADC A, n8
    uint8_t n;
    cycles += consume8(state, &n);
    adcR8(&cpu->reg.a, n, &cpu->reg.f);
    debugLog.AddLog("ADC A, %02X;", n);
    break;
  }
  case 0xCF: { // RST $08
    uint16_t n = 0x0008;
    cycles += stackPush16(state, cpu->pc);
    cpu->pc = n;
    debugLog.AddLog("RST $08;");
    break;
  }

  case 0xD0: { // RET NC
    // 4 cycles for the flag check
    cycles += 4;
    if (!checkBit(cpu->reg.f, FLAG_C)) {
      cycles += stackPop16(state, &cpu->pc);
      cycles += 4; // 4 more for setting pc
    }
    debugLog.AddLog("RET NC;");
    break;
  }
  case 0xD1: { // POP DE
    uint16_t result;
    cycles += stackPop16(state, &result);
    ldR16(&cpu->reg.d, &cpu->reg.e, result);
    debugLog.AddLog("POP DE;");
    break;
  }
  case 0xD2: { // JP NC, a16
    uint16_t addr;
    cycles += consume16(state, &addr);
    if (!checkBit(cpu->reg.f, FLAG_C)) {
      cpu->pc = addr;
      cycles += 4; // 4 for updating the pc
    }
    debugLog.AddLog("JP NC, %04hX;", addr);
    break;
  }
  case 0xD3: { // no instruction
    logUnsupportedOpcode(state, 0xD3);
    break;
  }
  case 0xD4: { // CALL NC, a16
    uint16_t addr;
    cycles += consume16(state, &addr);
    if (!checkBit(cpu->reg.f, FLAG_C)) {
      cycles += stackPush16(state, cpu->pc);
      cpu->pc = addr;
    }
    debugLog.AddLog("CALL NC, %04hX;", addr);
    break;
  }
  case 0xD5: { // PUSH DE
    uint16_t value = readRegister16(cpu->reg.d, cpu->reg.e);
    cycles += stackPush16(state, value);
    debugLog.AddLog("PUSH DE;");
    break;
  }
  case 0xD6: { // SUB A, n8
    uint8_t n;
    cycles += consume8(state, &n);
    subR8(&cpu->reg.a, n, &cpu->reg.f);
    debugLog.AddLog("SUB A, %02X;", n);
    break;
  }
  case 0xD7: { // RST $10
    uint16_t n = 0x0010;
    cycles += stackPush16(state, cpu->pc);
    cpu->pc = n;
    debugLog.AddLog("RST $10;");
    break;
  }
  case 0xD8: { // RET C
    // 4 cycles for the flag check
    cycles += 4;
    if (!checkBit(cpu->reg.f, FLAG_C)) {
      cycles += stackPop16(state, &cpu->pc);
      cycles += 4; // 4 more for setting pc
    }
    debugLog.AddLog("RET C;");
    break;
  }
  case 0xD9: { // RETI
    cycles += stackPop16(state, &cpu->pc);
    cycles += 4;
    cpu->ime = true;
    debugLog.AddLog("RETI;");
    break;
  }
  case 0xDA: { // JP C, a16
    uint16_t addr;
    cycles += consume16(state, &addr);
    if (checkBit(cpu->reg.f, FLAG_C)) {
      cpu->pc = addr;
      cycles += 4; // 4 for updating the pc
    }
    debugLog.AddLog("JP C, %04hX;", addr);
    break;
  }
  case 0xDB: { // no instruction
    logUnsupportedOpcode(state, 0xD3);
    break;
  }
  case 0xDC: { // CALL C, a16
    uint16_t addr;
    cycles += consume16(state, &addr);
    if (checkBit(cpu->reg.f, FLAG_C)) {
      cycles += stackPush16(state, cpu->pc);
      cpu->pc = addr;
    }
    debugLog.AddLog("CALL C, %04hX;", addr);
    break;
  }
  case 0xDD: { // no instruction
    logUnsupportedOpcode(state, 0xD3);
    break;
  }
  case 0xDE: { // SBC A, n8
    uint8_t n;
    cycles += consume8(state, &n);
    sbcR8(&cpu->reg.a, n, &cpu->reg.f);
    debugLog.AddLog("SBC A, %02X;", n);
    break;
  }
  case 0xDF: { // RST $18
    uint16_t n = 0x0018;
    cycles += stackPush16(state, cpu->pc);
    cpu->pc = n;
    debugLog.AddLog("RST $18;");
    break;
  }

  case 0xE0: { // LDH [a8], A
    uint16_t addr = 0xFF00;
    uint8_t e;
    cycles += consume8(state, &e);

    cycles += writeMemory8(state, addr + e, cpu->reg.a);
    debugLog.AddLog("LDH [$FF00+a8]; // a8=%02X", e);
    break;
  }
  case 0xE1: { // POP HL
    uint16_t result;
    cycles += stackPop16(state, &result);
    ldR16(&cpu->reg.h, &cpu->reg.l, result);
    debugLog.AddLog("POP HL;");
    break;
  }
  case 0xE2: { // LDH [C], A
    uint16_t addr = 0xFF00 + cpu->reg.c;
    cycles += writeMemory8(state, addr, cpu->reg.a);
    debugLog.AddLog("LDH [$FF00+C], A; // C=%02X", cpu->reg.c);
    break;
  }
  case 0xE3: // no instruction
  case 0xE4: // no instruction
  {
    logUnsupportedOpcode(state, 0xD3);
    break;
  }
  case 0xE5: { // PUSH HL
    uint16_t value = readRegister16(cpu->reg.h, cpu->reg.l);
    cycles += stackPush16(state, value);
    debugLog.AddLog("PUSH HL");
    break;
  }
  case 0xE6: { // AND A, n8
    uint8_t n;
    cycles += consume8(state, &n);
    andR8(&cpu->reg.a, n, &cpu->reg.f);
    debugLog.AddLog("AND A, %02X;", n);
    break;
  }
  case 0xE7: { // RST $20
    uint16_t n = 0x0020;
    cycles += stackPush16(state, cpu->pc);
    cpu->pc = n;
    debugLog.AddLog("RST $20;");
    break;
  }
  case 0xE8: { // ADD SP, e8
    int8_t n;
    cycles += consume8(state, &n);
    uint16_t next = add16(cpu->sp, n, &cpu->reg.f);
    cpu->sp = next;
    cycles += 8;
    debugLog.AddLog("ADD SP, %d;", n);
    break;
  }
  case 0xE9: { // JP HL
    cpu->pc = readRegister16(cpu->reg.h, cpu->reg.l);
    debugLog.AddLog("JP HL;");
    break;
  }
  case 0xEA: { // LD [a16], A
    uint16_t addr;
    cycles += consume16(state, &addr);
    cycles += writeMemory8(state, addr, cpu->reg.a);
    debugLog.AddLog("LD [a16], A; // a16=%04hX", addr);
    break;
  }
  case 0xEB: // no instruction
  case 0xEC: // no instruction
  case 0xED: // no instruction
  {
    logUnsupportedOpcode(state, 0xD3);
    break;
  }
  case 0xEE: { // XOR A, n8
    uint8_t n;
    cycles += consume8(state, &n);
    xorR8(&cpu->reg.a, n, &cpu->reg.f);
    debugLog.AddLog("XOR A, %02X;", n);
    break;
  }
  case 0xEF: { // RST $28
    uint16_t n = 0x0028;
    cycles += stackPush16(state, cpu->pc);
    cpu->pc = n;
    debugLog.AddLog("RST $28;");
    break;
  }

  case 0xF0: { // LDH A, [a8]
    uint8_t n;
    cycles += consume8(state, &n);
    uint16_t addr = 0xFF00 + n;
    uint8_t value;
    cycles += readMemory8(state, addr, &value);
    ldR8(&cpu->reg.a, value);
    debugLog.AddLog("LDH A, [$FF00+a8]; // a8=%02X", n);
    break;
  }
  case 0xF1: { // POP AF
    uint16_t result;
    cycles += stackPop16(state, &result);
    ldR16(&cpu->reg.a, &cpu->reg.f, result);
    debugLog.AddLog("POP AF;");
    break;
  }
  case 0xF2: { // LDH A, [C]
    uint16_t addr = 0xFF00 + cpu->reg.c;
    uint8_t value;
    cycles += readMemory8(state, addr, &value);
    ldR8(&cpu->reg.a, value);
    debugLog.AddLog("LDH A, [$FF00+C]; // C=%02X", cpu->reg.c);
    break;
  }
  case 0xF3: { // DI
    cpu->ime = false;
    debugLog.AddLog("DI;");
    break;
  }
  case 0xF4: { // no instruction
    logUnsupportedOpcode(state, 0xD3);
    break;
  }
  case 0xF5: { // PUSH AF
    uint16_t value = readRegister16(cpu->reg.a, cpu->reg.f);
    cycles += stackPush16(state, value);
    debugLog.AddLog("PUSH AF;");
    break;
  }
  case 0xF6: { // OR A, n8
    uint8_t n;
    cycles += consume8(state, &n);
    orR8(&cpu->reg.a, n, &cpu->reg.f);
    debugLog.AddLog("OR A, %02X;", n);
    break;
  }
  case 0xF7: { // RST $30
    uint16_t n = 0x0030;
    cycles += stackPush16(state, cpu->pc);
    cpu->pc = n;
    debugLog.AddLog("RST $30;");
    break;
  }
  case 0xF8: { // LD HL, SP + e8
    int8_t n;
    cycles += consume8(state, &n);
    uint16_t value = add16(cpu->sp, n, &cpu->reg.f);
    cycles += 4;
    ldR16(&cpu->reg.h, &cpu->reg.l, value);
    debugLog.AddLog("LD HL, SP + %d;", n);
    break;
  }
  case 0xF9: { // LD SP, HL
    uint16_t value = readRegister16(cpu->reg.h, cpu->reg.l);
    cpu->sp = value;
    cycles += 4;
    debugLog.AddLog("LD SP, HL;");
    break;
  }
  case 0xFA: { // LD A, [a16]
    uint16_t addr;
    cycles += consume16(state, &addr);
    cycles += readMemory8(state, addr, &cpu->reg.a);
    debugLog.AddLog("LD A, [a16]; // a16=%04hX", addr);
    break;
  }
  case 0xFB: { // EI
    cpu->ime = true;
    debugLog.AddLog("EI;");
    break;
  }
  case 0xFC: // no instruction
  case 0xFD: // no instruction
  {
    logUnsupportedOpcode(state, 0xD3);
    break;
  }
  case 0xFE: { // CP A, n8
    uint8_t n;
    cycles += consume8(state, &n);

    // the CP instruction subtracts a value from A, updates the flags but does
    // not store the result
    sub8(cpu->reg.a, n, &cpu->reg.f, true);
    debugLog.AddLog("CP A, %02X;", n);
    break;
  }
  case 0xFF: { // RST $38
    uint16_t n = 0x0038;
    cycles += stackPush16(state, cpu->pc);
    cpu->pc = n;
    debugLog.AddLog("RST $38;");
    break;
  }
  }

  debugLog.AddLog(" (took %d cycles)\n", cycles);
  return cycles;
}

int doExtendedInstruction(emulator_state *state) {
  dmg_cpu *cpu = &state->cpu;
  int cycles = 0;

  // Fetch instruction at the program counter
  uint8_t opcode;
  cycles += consume8(state, &opcode);

  debugLog.AddLog("[%02X] ", opcode);

  uint8_t hiNibble = (opcode & 0xF0) >> 4;
  uint8_t loNibble = opcode & 0x0F;

  uint8_t value;
  int regId;
  cycles += nibbleDecode(state, loNibble, &value, &regId);
  switch (hiNibble) {
  case 0x0: {
    if (loNibble < 0x8) { // RLC r
      if (loNibble == 0x6) {
        uint8_t value;
        uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
        cycles += readMemory8(state, addr, &value);
        value = rlc8(value, &cpu->reg.f);
        cycles += writeMemory8(state, addr, value);
        debugLog.AddLog("RLC [HL]; // HL=%04hX", addr);
      } else {
        uint8_t *reg = registerDecode(state, loNibble, &regId);
        uint8_t value = rlc8(*reg, &cpu->reg.f);
        ldR8(reg, value);
        debugLog.AddLog("RLC %s;", printRegisterName(regId));
      }
    } else { // RRC r
      if (loNibble == 0xE) {
        uint8_t value;
        uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
        cycles += readMemory8(state, addr, &value);
        value = rrc8(value, &cpu->reg.f);
        cycles += writeMemory8(state, addr, value);
        debugLog.AddLog("RRC [HL]; // HL=%04hX", addr);
      } else {
        uint8_t *reg = registerDecode(state, loNibble, &regId);
        uint8_t value = rrc8(*reg, &cpu->reg.f);
        ldR8(reg, value);
        debugLog.AddLog("RRC %s;", printRegisterName(regId));
      }
    }
    break;
  }
  case 0x1: {
    if (loNibble < 0x8) { // RL r
      if (loNibble == 0x6) {
        uint8_t value;
        uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
        cycles += readMemory8(state, addr, &value);
        value = rl8(value, &cpu->reg.f);
        cycles += writeMemory8(state, addr, value);
        debugLog.AddLog("RL [HL]; // HL=%04hX", addr);
      } else {
        uint8_t *reg = registerDecode(state, loNibble, &regId);
        uint8_t value = rl8(*reg, &cpu->reg.f);
        ldR8(reg, value);
        debugLog.AddLog("RL %s;", printRegisterName(regId));
      }
    } else { // RR r
      if (loNibble == 0xE) {
        uint8_t value;
        uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
        cycles += readMemory8(state, addr, &value);
        value = rr8(value, &cpu->reg.f);
        cycles += writeMemory8(state, addr, value);
        debugLog.AddLog("RR [HL]; // HL=%04hX", addr);
      } else {
        uint8_t *reg = registerDecode(state, loNibble, &regId);
        uint8_t value = rr8(*reg, &cpu->reg.f);
        ldR8(reg, value);
        debugLog.AddLog("RR %s;", printRegisterName(regId));
      }
    }
    break;
  }
  case 0x2: {
    if (loNibble < 0x8) { // SLA r
      if (loNibble == 0x6) {
        uint8_t value;
        uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
        cycles += readMemory8(state, addr, &value);
        value = sla8(value, &cpu->reg.f);
        cycles += writeMemory8(state, addr, value);
        debugLog.AddLog("SLA [HL]; // HL=%04hX", addr);
      } else {
        uint8_t *reg = registerDecode(state, loNibble, &regId);
        uint8_t value = sla8(*reg, &cpu->reg.f);
        ldR8(reg, value);
        debugLog.AddLog("SLA %s;", printRegisterName(regId));
      }
    } else { // SRA r
      if (loNibble == 0xE) {
        uint8_t value;
        uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
        cycles += readMemory8(state, addr, &value);
        value = sra8(value, &cpu->reg.f);
        cycles += writeMemory8(state, addr, value);
        debugLog.AddLog("SRA [HL]; // HL=%04hX", addr);
      } else {
        uint8_t *reg = registerDecode(state, loNibble, &regId);
        uint8_t value = sra8(*reg, &cpu->reg.f);
        ldR8(reg, value);
        debugLog.AddLog("SRA %s;", printRegisterName(regId));
      }
    }
    break;
  }
  case 0x3: {
    if (loNibble < 0x8) { // SWAP r
      if (loNibble == 0x6) {
        uint8_t value;
        uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
        cycles += readMemory8(state, addr, &value);
        value = swap8(value, &cpu->reg.f);
        cycles += writeMemory8(state, addr, value);
        debugLog.AddLog("SWAP [HL]; // HL=%04hX", addr);
      } else {
        uint8_t *reg = registerDecode(state, loNibble, &regId);
        uint8_t value = swap8(*reg, &cpu->reg.f);
        ldR8(reg, value);
        debugLog.AddLog("SWAP %s;", printRegisterName(regId));
      }
    } else { // SRL r
      if (loNibble == 0xE) {
        uint8_t value;
        uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
        cycles += readMemory8(state, addr, &value);
        value = srl8(value, &cpu->reg.f);
        cycles += writeMemory8(state, addr, value);
        debugLog.AddLog("SRL [HL]; // HL=%04hX", addr);
      } else {
        uint8_t *reg = registerDecode(state, loNibble, &regId);
        uint8_t value = srl8(*reg, &cpu->reg.f);
        ldR8(reg, value);
        debugLog.AddLog("SRL %s;", printRegisterName(regId));
      }
    }
    break;
  }
  case 0x4: // BIT 0/1, r
  case 0x5: // BIT 2/3, r
  case 0x6: // BIT 4/5, r
  case 0x7: // BIT 6/7, r
  {
    uint8_t value;
    int bitIndex = (hiNibble - 0x4) * 2 + (loNibble < 0x8 ? 0 : 1);
    if (loNibble == 0x6) {
      uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
      cycles += readMemory8(state, addr, &value);
      debugLog.AddLog("BIT %d, [HL]; // HL=%04hX", bitIndex, addr);
    } else {
      uint8_t *reg = registerDecode(state, loNibble, &regId);
      value = swap8(*reg, &cpu->reg.f);
      debugLog.AddLog("BIT %d, %s;", bitIndex, printRegisterName(regId));
    }
    if (checkBit16(value, bitIndex)) {
      clearBit(&cpu->reg.f, FLAG_Z);
    } else {
      setBit(&cpu->reg.f, FLAG_Z);
    }
    clearBit(&cpu->reg.f, FLAG_N);
    setBit(&cpu->reg.f, FLAG_H);
    break;
  }
  case 0x8: // RES 0/1, r
  case 0x9: // RES 2/3, r
  case 0xA: // RES 4/5, r
  case 0xB: // RES 6/7, r
  {
    int bitIndex = (hiNibble - 0x4) * 2 + (loNibble < 0x8 ? 0 : 1);
    if (loNibble == 0x6) {
      uint8_t value;
      uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
      cycles += readMemory8(state, addr, &value);
      clearBit(&value, bitIndex);
      cycles += writeMemory8(state, addr, value);
      debugLog.AddLog("RES %d, [HL]; // HL=%04hX", bitIndex, addr);
    } else {
      uint8_t *reg = registerDecode(state, loNibble, &regId);
      clearBit(reg, bitIndex);
      debugLog.AddLog("RES %d, %s;", bitIndex, printRegisterName(regId));
    }
    break;
  }
  case 0xC: // SET 0/1, r
  case 0xD: // SET 2/3, r
  case 0xE: // SET 4/5, r
  case 0xF: // SET 6/7, r
  {
    int bitIndex = (hiNibble - 0x4) * 2 + (loNibble < 0x8 ? 0 : 1);
    if (loNibble == 0x6) {
      uint8_t value;
      uint16_t addr = readRegister16(cpu->reg.h, cpu->reg.l);
      cycles += readMemory8(state, addr, &value);
      setBit(&value, bitIndex);
      cycles += writeMemory8(state, addr, value);
      debugLog.AddLog("SET %d, [HL]; // HL=%04hX", bitIndex, addr);
    } else {
      uint8_t *reg = registerDecode(state, loNibble, &regId);
      setBit(reg, bitIndex);
      debugLog.AddLog("SET %d, %s;", bitIndex, printRegisterName(regId));
    }
    break;
  }
  }
  return cycles;
}

// request an interrupt to be handled by the CPU. note that if IME is disabled,
// interrupts will not be serviced until it is re-enabled.
// https://gbdev.io/pandocs/Interrupts.html#interrupt-priorities
void requestInterrupt(emulator_state *state, int interruptBitIndex) {
  uint8_t interruptFlags;
  internal_readMemory8(state, MEM_IF, &interruptFlags);
  setBit(&interruptFlags, interruptBitIndex);
  internal_writeMemory8(state, MEM_IF, interruptFlags);
}

// https://gbdev.io/pandocs/Interrupts.html#interrupt-handling
int serviceInterrupts(emulator_state *state) {
  dmg_cpu *cpu = &state->cpu;
  int cycles = 0;

  uint8_t interruptFlags;
  internal_readMemory8(state, MEM_IF, &interruptFlags);
  uint8_t interruptEnable;
  internal_readMemory8(state, MEM_IE, &interruptEnable);
  // check if interrupts are enabled, and there is an interrupt requested
  if (cpu->ime && (interruptFlags & 0xFF) != 0) {
    // disable the ime
    cpu->ime = false;

    // two NOPs are executed by the CPU here
    cycles += 8;
    uint16_t dest = 0;

    // interrupts are serviced in order of priority (0-4 in the bitset)
    // we acknowledge the request by clearing the bit, and then jump to the
    // designated interrupt handler
    if (checkBit(interruptFlags, INTERRUPT_VBLANK) &&
        checkBit(interruptEnable, INTERRUPT_VBLANK)) {
      clearBit(&interruptFlags, INTERRUPT_VBLANK);
      dest = MEM_INTERRUPT_VBLANK;
    } else if (checkBit(interruptFlags, INTERRUPT_LCD) &&
               checkBit(interruptEnable, INTERRUPT_LCD)) {
      clearBit(&interruptFlags, INTERRUPT_LCD);
      dest = MEM_INTERRUPT_STAT;
    } else if (checkBit(interruptFlags, INTERRUPT_TIMER) &&
               checkBit(interruptEnable, INTERRUPT_TIMER)) {
      clearBit(&interruptFlags, INTERRUPT_TIMER);
      dest = MEM_INTERRUPT_TIMER;
    } else if (checkBit(interruptFlags, INTERRUPT_SERIAL) &&
               checkBit(interruptEnable, INTERRUPT_SERIAL)) {
      clearBit(&interruptFlags, INTERRUPT_SERIAL);
      dest = MEM_INTERRUPT_SERIAL;
    } else if (checkBit(interruptFlags, INTERRUPT_JOYPAD) &&
               checkBit(interruptEnable, INTERRUPT_JOYPAD)) {
      clearBit(&interruptFlags, INTERRUPT_JOYPAD);
      dest = MEM_INTERRUPT_JOYPAD;
    }

    cycles += stackPush16(state, cpu->pc);
    cpu->pc = dest;
  }
  return cycles;
}

void updateStat(emulator_state *state) {
  uint8_t ly, lyc, stat;
  internal_readMemory8(state, MEM_LY, &ly);
  internal_readMemory8(state, MEM_LYC, &lyc);
  if (ly == lyc) {
    internal_readMemory8(state, MEM_STAT, &stat);
    setBit(&stat, 2);
    internal_writeMemory8(state, MEM_STAT, stat);
  }
}
