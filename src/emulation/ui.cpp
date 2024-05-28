#include "emulator.h"
#include "imgui.h"
#include "imgui_memory_editor.h"

static MemoryEditor main_memory;
static MemoryEditor boot_rom;
static MemoryEditor cart_rom;
static MemoryEditor cart_ram;
static MemoryEditor graphics_buffer;

void initEmulatorImguiFrame(emulator_bridge *bridge, ImGuiIO &io) {
  main_memory.Open = true;
  boot_rom.Open = false;
  cart_rom.Open = false;
  cart_ram.Open = false;
  graphics_buffer.Open = true;
}

void drawDebugControls(emulator_bridge *bridge, bool *open) {
  ImGui::Begin("Debug Controls", open, ImGuiWindowFlags_AlwaysAutoResize);
  if (ImGui::Button("Start")) {
    bridge->emulatorState->isRunning = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Step to next Frame")) {
    int prevDelta = bridge->emulatorState->cyclesDelta;
    int cyclesToAdvance = CPU_CYCLES_PER_FRAME - prevDelta;
    stepFrame(bridge);
    debugLog.AddLog("[DEBUG] Stepping forward %d cycles\n", cyclesToAdvance);
    debugLog.AddLog("[DEBUG] Completed step - %d cycles completed\n",
                    cyclesToAdvance + bridge->emulatorState->cyclesDelta);
  }
  ImGui::SameLine();
  if (ImGui::Button("Step Instruction")) {
    stepInstruction(bridge);
  }
  ImGui::SameLine();
  if (ImGui::Button("Stop")) {
    bridge->emulatorState->isRunning = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    resetEmulator(bridge);
  }

  ImGui::Checkbox("Breakpoint enabled",
                  &bridge->emulatorState->debug_breakpointEnabled);
  ImGui::SameLine();

  static char bpBuf[5]; // 4 + null terminator
  if (ImGui::InputText("Breakpoint", bpBuf, sizeof(bpBuf))) {
    sscanf(bpBuf, "%04hX", &bridge->emulatorState->debug_breakpoint);
  }
  ImGui::End();
}

void drawDebugLog(bool *open) {
  ImGui::Begin("Debug Log", open);
  debugLog.Draw("Debug Log", open);
  ImGui::End();
}

void drawCPUWindow(dmg_cpu cpu, bool *open) {
  ImGui::Begin("CPU", open, ImGuiWindowFlags_AlwaysAutoResize);

  // registers
  ImGui::SeparatorText("Basic Registers");
  if (ImGui::BeginTable("CPU Registers", 4,
                        ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("b");
    ImGui::TableNextColumn();
    ImGui::Text("%02X", cpu.reg.b);
    ImGui::TableNextColumn();
    ImGui::Text("c");
    ImGui::TableNextColumn();
    ImGui::Text("%02X", cpu.reg.c);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("d");
    ImGui::TableNextColumn();
    ImGui::Text("%02X", cpu.reg.d);
    ImGui::TableNextColumn();
    ImGui::Text("e");
    ImGui::TableNextColumn();
    ImGui::Text("%02X", cpu.reg.e);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("h");
    ImGui::TableNextColumn();
    ImGui::Text("%02X", cpu.reg.h);
    ImGui::TableNextColumn();
    ImGui::Text("l");
    ImGui::TableNextColumn();
    ImGui::Text("%02X", cpu.reg.l);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("a");
    ImGui::TableNextColumn();
    ImGui::Text("%02X", cpu.reg.a);
    ImGui::TableNextColumn();
    ImGui::Text("f");
    ImGui::TableNextColumn();
    ImGui::Text("%02X", cpu.reg.f);
    ImGui::EndTable();
  }

  // flags
  ImGui::SeparatorText("Flags");
  bool zFlag = checkBit(cpu.reg.f, FLAG_Z);
  bool nFlag = checkBit(cpu.reg.f, FLAG_N);
  bool hFlag = checkBit(cpu.reg.f, FLAG_H);
  bool cFlag = checkBit(cpu.reg.f, FLAG_C);
  ImGui::BeginDisabled();
  ImGui::Checkbox("Z", &zFlag);
  ImGui::SameLine();
  ImGui::Checkbox("N", &nFlag);
  ImGui::SameLine();
  ImGui::Checkbox("H", &hFlag);
  ImGui::SameLine();
  ImGui::Checkbox("C", &cFlag);
  ImGui::EndDisabled();

  // special registers (pc, sp, ime)
  ImGui::SeparatorText("Special Registers");
  if (ImGui::BeginTable("Special Registers", 2,
                        ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("sp");
    ImGui::SameLine();
    char spBuffer[6];
    snprintf(spBuffer, 6, "$%04hx", cpu.sp);
    if (ImGui::Button(spBuffer) && main_memory.Open) {
      main_memory.GotoAddrAndHighlight(cpu.sp, cpu.sp + 1);
    }
    ImGui::TableNextColumn();
    ImGui::Text("cp");
    ImGui::SameLine();
    char pcBuffer[6];
    snprintf(pcBuffer, 6, "$%04hx", cpu.pc);
    if (ImGui::Button(pcBuffer) && main_memory.Open) {
      main_memory.GotoAddrAndHighlight(cpu.pc, cpu.pc + 1);
    }
    ImGui::EndTable();
  }

  ImGui::SeparatorText("Signals");
  if (ImGui::BeginTable("CPU Signals", 2, ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextColumn();
    ImGui::BeginDisabled();
    ImGui::Checkbox("IME", &cpu.ime);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Checkbox("Halted", &cpu.isHalted);
    ImGui::TableNextColumn();
    ImGui::Checkbox("Stopped", &cpu.isStopped);
    ImGui::EndDisabled();

    ImGui::EndTable();
  }

  ImGui::End();
}

void drawPPUWindow(emulator_state *state, bool *open) {
  dmg_ppu *ppu = &state->ppu;
  ImGui::Begin("PPU", open, ImGuiWindowFlags_AlwaysAutoResize);
  uint8_t lcdc, stat, ly, scx, scy, wx, wy;
  internal_readMemory8(state, MEM_LCDC, &lcdc);
  internal_readMemory8(state, MEM_STAT, &stat);
  internal_readMemory8(state, MEM_LY, &ly);
  internal_readMemory8(state, MEM_SCX, &scx);
  internal_readMemory8(state, MEM_SCY, &scy);
  internal_readMemory8(state, MEM_WX, &wx);
  internal_readMemory8(state, MEM_WY, &wy);

  ImGui::SeparatorText("PPU");
  if (ImGui::BeginTable("PPU state table", 4,
                        ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextColumn();
    ImGui::Text("Mode");
    ImGui::TableNextColumn();
    ImGui::Text("%d", readPPUMode(state));
    ImGui::TableNextColumn();
    ImGui::Text("LY");
    ImGui::TableNextColumn();
    ImGui::Text("%d", ly);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Current dot");
    ImGui::TableNextColumn();
    ImGui::Text("%d", ppu->currentDot);
    ImGui::TableNextColumn();
    ImGui::Text("Next pixel");
    ImGui::TableNextColumn();
    ImGui::Text("%d", ppu->nextLCDPixel);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("SCX");
    ImGui::TableNextColumn();
    ImGui::Text("%d", scx);
    ImGui::TableNextColumn();
    ImGui::Text("SCY");
    ImGui::TableNextColumn();
    ImGui::Text("%d", scy);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("WX");
    ImGui::TableNextColumn();
    ImGui::Text("%d", wx);
    ImGui::TableNextColumn();
    ImGui::Text("WY");
    ImGui::TableNextColumn();
    ImGui::Text("%d", wy);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("cycle overrun");
    ImGui::TableNextColumn();
    ImGui::Text("%d", ppu->cycleOverrun);

    ImGui::EndTable();
  }

  ImGui::SeparatorText("OAM");
  if (ImGui::BeginTable("OAM state table", 4,
                        ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableNextColumn();
    ImGui::Text("Sprites on line");
    ImGui::TableNextColumn();
    ImGui::Text("%d", ppu->spritesInBuffer);

    ImGui::TableNextColumn();
    ImGui::Text("Next sprite X");
    ImGui::TableNextColumn();
    if (ppu->spritesInBuffer > ppu->oamSpriteHead) {
      ImGui::Text("%d", ppu->oamSpriteBuffer[ppu->oamSpriteHead].x);
    } else {
      ImGui::Text("N/A");
    }

    ImGui::EndTable();
  }

  ImGui::SeparatorText("FIFO/Fetcher");
  if (ImGui::BeginTable("FIFO state table", 4,
                        ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableNextColumn();
    ImGui::Text("Pixels in FIFO");
    ImGui::TableNextColumn();
    ImGui::Text("%d", ppu->pixelsInFifo);
    ImGui::TableNextColumn();
    ImGui::Text("Fetcher buffered");
    ImGui::TableNextColumn();
    ImGui::Text("%s", ppu->bufferFull ? "true" : "false");

    ImGui::TableNextColumn();
    ImGui::Text("Fetcher X");
    ImGui::TableNextColumn();
    ImGui::Text("%d", ppu->internalXCounter);
    ImGui::TableNextColumn();
    ImGui::Text("Pending Fetch");
    ImGui::TableNextColumn();
    ImGui::Text("%d", ppu->fetchPendingCyclesRemaining);
    ImGui::EndTable();
  }

  ImGui::SeparatorText("LCDC");
  if (ImGui::BeginTable("LCDC flags table", 2,
                        ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextColumn();
    ImGui::CheckboxFlags("LCD enable", (uint32_t *)&lcdc, (1 << 7));
    ImGui::TableNextColumn();
    ImGui::CheckboxFlags("Window tile map", (uint32_t *)&lcdc, (1 << 6));
    ImGui::TableNextColumn();
    ImGui::CheckboxFlags("Window enable", (uint32_t *)&lcdc, (1 << 5));
    ImGui::TableNextColumn();
    ImGui::CheckboxFlags("Addressing mode", (uint32_t *)&lcdc, (1 << 4));
    ImGui::TableNextColumn();
    ImGui::CheckboxFlags("BG tile map", (uint32_t *)&lcdc, (1 << 3));
    ImGui::TableNextColumn();
    ImGui::CheckboxFlags("Tall sprites", (uint32_t *)&lcdc, (1 << 2));
    ImGui::TableNextColumn();
    ImGui::CheckboxFlags("Sprite enable", (uint32_t *)&lcdc, (1 << 1));
    ImGui::TableNextColumn();
    ImGui::CheckboxFlags("BG priority", (uint32_t *)&lcdc, (1 << 0));
    ImGui::EndTable();
  }
  ImGui::End();
}

void drawTimersWindow(emulator_state *state, bool *open) {
  ImGui::Begin("Timers", open, ImGuiWindowFlags_AlwaysAutoResize);
  uint8_t div, tima, tma, tac;
  internal_readMemory8(state, MEM_DIV, &div);
  internal_readMemory8(state, MEM_TIMA, &tima);
  internal_readMemory8(state, MEM_TMA, &tma);
  internal_readMemory8(state, MEM_TAC, &tac);
  bool timaEnabled = checkBit(tac, 2);
  int timaTimer = getTimaFrequency(tac);
  if (ImGui::BeginTable("Timers", 4, ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextColumn();
    ImGui::Text("DIV");
    ImGui::TableNextColumn();
    ImGui::Text("%02X", div);
    ImGui::TableNextColumn();
    ImGui::Text("cyc");
    ImGui::TableNextColumn();
    ImGui::Text("%.3d/%.3d", state->dividerCycles, CPU_CYCLES_PER_DIVTICK);

    if (timaEnabled) {
      ImGui::BeginDisabled();
    }
    ImGui::TableNextColumn();
    ImGui::Text("TIMA");
    ImGui::TableNextColumn();
    ImGui::Text("%02X", tima);
    ImGui::TableNextColumn();
    ImGui::Text("cyc");
    ImGui::TableNextColumn();
    ImGui::Text("%.4d/%.4d", state->timaCycles, timaTimer);
    ImGui::TableNextColumn();
    ImGui::Text("TMA");
    ImGui::TableNextColumn();
    ImGui::Text("%02X", tma);
    if (timaEnabled) {
      ImGui::EndDisabled();
    }
    ImGui::EndTable();
  }
  ImGui::Checkbox("TIMA Enabled", &timaEnabled);
  ImGui::Text("TAC Select");
  ImGui::SameLine();
  ImGui::Text("%01X", (tac & 0x3));
  ImGui::End();
}

void drawStateWindow(emulator_state *state, bool *open) {
  ImGui::Begin("Emulator State", open, ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::Checkbox("Booting", &state->isBooting);
  ImGui::Checkbox("Running", &state->isRunning);
  ImGui::Checkbox("Halted", &state->isHalted);
  ImGui::Text("cycleDelta");
  ImGui::SameLine();
  ImGui::Text("%.2d\n", state->cyclesDelta);
  ImGui::End();
}

void drawEmulatorImguiFrame(emulator_bridge *bridge, ImGuiIO &io) {
  static bool showDebugControls = true;
  static bool showCPUWindow = true;
  static bool showPPUWindow = true;
  static bool showDemoWindow = false;
  static bool showDebugLog = true;
  static bool showTimers = true;
  static bool showStateWindow = true;

  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("Emulator")) {
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Debug")) {
      ImGui::MenuItem("Enable Logging", NULL, &debugLog.enabled);
      ImGui::MenuItem("Show debug controls", NULL, &showDebugControls);
      ImGui::MenuItem("Show debug log", NULL, &showDebugLog);
      ImGui::MenuItem("Show CPU info", NULL, &showCPUWindow);
      ImGui::MenuItem("Show PPU info", NULL, &showPPUWindow);
      ImGui::MenuItem("Show emulator state", NULL, &showStateWindow);
      ImGui::MenuItem("Show timers", NULL, &showTimers);
      ImGui::MenuItem("Show main memory", NULL, &main_memory.Open);
      ImGui::MenuItem("Show graphics buffer memory", NULL,
                      &graphics_buffer.Open);
      ImGui::MenuItem("Show boot ROM", NULL, &boot_rom.Open);
      ImGui::MenuItem("Show cartridge ROM", NULL, &cart_rom.Open);
      ImGui::MenuItem("Show cartridge RAM", NULL, &cart_ram.Open);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Demo")) {
      ImGui::MenuItem("Show demo window", NULL, &showDemoWindow);
      ImGui::EndMenu();
    }
    char fpsString[255];
    snprintf(fpsString, sizeof(fpsString), "%.3f ms/frame (%.1f FPS)",
             1000.0f / io.Framerate, io.Framerate);
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x -
                    ImGui::CalcTextSize(fpsString).x);
    ImGui::Text("%s", fpsString);
    ImGui::EndMainMenuBar();
  }

  if (showDemoWindow) {
    ImGui::ShowDemoWindow(&showDemoWindow);
  }

  if (showDebugControls) {
    drawDebugControls(bridge, &showDebugControls);
  }

  if (showCPUWindow) {
    drawCPUWindow(bridge->emulatorState->cpu, &showCPUWindow);
  }

  if (showPPUWindow) {
    drawPPUWindow(bridge->emulatorState, &showPPUWindow);
  }

  if (showStateWindow) {
    drawStateWindow(bridge->emulatorState, &showCPUWindow);
  }

  if (showTimers) {
    drawTimersWindow(bridge->emulatorState, &showTimers);
  }

  if (showDebugLog) {
    drawDebugLog(&showDebugLog);
  }

  if (main_memory.Open) {
    main_memory.DrawWindow("Main Memory",
                           bridge->emulatorState->memory.mainMemory,
                           sizeof(bridge->emulatorState->memory.mainMemory));
  }
  if (boot_rom.Open) {
    boot_rom.DrawWindow("Boot ROM", bridge->emulatorState->bootROM,
                        sizeof(bridge->emulatorState->bootROM));
  }
  if (cart_rom.Open) {
    cart_rom.DrawWindow("Cartridge ROM", bridge->emulatorState->cartridgeROM,
                        bridge->emulatorState->cartridgeROMSize);
  }
  if (cart_ram.Open) {
    cart_ram.DrawWindow("Cartridge ROM", bridge->emulatorState->cartridgeRAM,
                        bridge->emulatorState->cartridgeRAMSize);
  }
  if (graphics_buffer.Open) {
    graphics_buffer.DrawWindow("Graphics Buffer", bridge->graphicsBuffer,
                               sizeof(uint32_t) * EMULATOR_SCREEN_WIDTH *
                                   EMULATOR_SCREEN_HEIGHT);
  }
}
