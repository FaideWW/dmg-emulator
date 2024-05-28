#include "emulator.h"

int getTimaFrequency(uint8_t tac) {
  int timaTimer;
  switch (tac & 0x3) { // grab the bottom two bits
  case 0b00: {
    timaTimer = 256 * 4; // 256 M-cycles
    break;
  }
  case 0b01: {
    timaTimer = 4 * 4; // 4 M-cycles
    break;
  }
  case 0b10: {
    timaTimer = 16 * 4; // 16 M-cycles
    break;
  }
  case 0b11: {
    timaTimer = 64 * 4; // 64 M-cycles
    break;
  }
  }
  return timaTimer;
}

void advanceTimers(emulator_state *state, int cyclesToAdvance) {
  uint8_t div, tima, tma, tac;
  internal_readMemory8(state, MEM_DIV, &div);
  internal_readMemory8(state, MEM_TIMA, &tima);
  internal_readMemory8(state, MEM_TMA, &tma);
  internal_readMemory8(state, MEM_TAC, &tac);

  state->dividerCycles += cyclesToAdvance;
  if (state->dividerCycles > CPU_CYCLES_PER_DIVTICK) {
    state->dividerCycles = state->dividerCycles % CPU_CYCLES_PER_DIVTICK;
    internal_writeMemory8(state, MEM_DIV, div + 1);
  }

  bool timaEnabled = checkBit(tac, 2);
  int timaTimer = getTimaFrequency(tac);
  if (timaEnabled) {
    state->timaCycles += cyclesToAdvance;
    if (state->timaCycles > timaTimer) {
      state->timaCycles = state->timaCycles % timaTimer;
      if (tima == 0xFF) {
        // request timer interrupt
        requestInterrupt(state, INTERRUPT_TIMER);
        // TODO: from the pandocs: "If a TMA write is executed on the same
        // M-cycle as the content of TMA is transferred to TIMA due to a timer
        // overflow, the old value is transferred to TIMA."
        // do we need to support this?
        internal_writeMemory8(state, MEM_TIMA, tma);
      }
    }
  }
}
