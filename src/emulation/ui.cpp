#include "emulator.h"
#include "imgui_memory_editor.h"

static MemoryEditor main_memory;
static MemoryEditor boot_rom;
static MemoryEditor cart_rom;
static MemoryEditor cart_ram;

void initEmulatorImguiFrame(emulator_bridge *bridge, ImGuiIO &io) {
  main_memory.Open = false;
  boot_rom.Open = false;
  cart_rom.Open = false;
  cart_ram.Open = false;
}

void drawEmulatorImguiFrame(emulator_bridge *bridge, ImGuiIO &io) {
  static bool show_demo_window = false;

  if (ImGui::BeginMainMenuBar()) {

    if (ImGui::BeginMenu("Debug")) {
      ImGui::MenuItem("Show main memory", NULL, &main_memory.Open);
      ImGui::MenuItem("Show boot ROM", NULL, &boot_rom.Open);
      ImGui::MenuItem("Show cartridge ROM", NULL, &cart_rom.Open);
      ImGui::MenuItem("Show cartridge RAM", NULL, &cart_ram.Open);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Demo")) {
      ImGui::MenuItem("Show demo window", NULL, &show_demo_window);
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
  if (show_demo_window) {
    ImGui::ShowDemoWindow(&show_demo_window);
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
}
