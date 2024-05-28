#include <stdio.h>

#ifdef NEWMAIN
#define CPP_METAL_INCLUDE
#include "VertexData.h"
#include "generated/metal_shaders.generated.h"
#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_sdl2.h"
#include "macos_main.h"
#include <QuartzCore/QuartzCore.hpp>
#include <SDL2/SDL.h>

#define RENDER_SCALE 1
uint32_t SCREEN_WIDTH = 1920;
uint32_t SCREEN_HEIGHT = 1080;

static macos_graphics_buffer graphicsBuffer;

void initGraphicsBuffer(macos_graphics_buffer *buffer, int width, int height,
                        MTL::Device *metalDevice) {
  VertexData squareVertices[]{
      {{-width * 0.5f, -height * 0.5f}, {0.0f, 0.0f}}, // top left
      {{-width * 0.5f, height * 0.5f}, {0.0f, 1.0f}},  // bottom left
      {{width * 0.5f, height * 0.5f}, {1.0f, 1.0f}},   // bottom right
      {{-width * 0.5f, -height * 0.5f}, {0.0f, 0.0f}}, // top left
      {{width * 0.5f, height * 0.5f}, {1.0f, 1.0f}},   // bottom right
      {{width * 0.5f, -height * 0.5f}, {1.0f, 0.0f}},  // top right
  };

  buffer->screenVertexBuffer = metalDevice->newBuffer(
      &squareVertices, sizeof(squareVertices), MTL::ResourceStorageModeShared);
  resizeGraphicsBuffer(&graphicsBuffer, width, height, metalDevice);
}

void resizeGraphicsBuffer(macos_graphics_buffer *buffer, int width, int height,
                          MTL::Device *metalDevice) {
  // Free buffer memory, if it exists
  if (buffer->memory != NULL) {
    munmap(buffer->memory, buffer->memSize);
    buffer->memory = NULL;
  }

  buffer->width = width;
  buffer->height = height;
  buffer->bytesPerPixel = 4;
  buffer->pitch = buffer->width * buffer->bytesPerPixel;

  size_t memSize = sizeof(uint8_t) * (buffer->width * buffer->height) *
                   buffer->bytesPerPixel;
  buffer->memory = mmap(0, memSize, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  buffer->memSize = memSize;

  // Resize texture
  if (buffer->texture != NULL) {
    buffer->texture->release();
    buffer->texture = NULL;
  }

  MTL::TextureDescriptor *textureDescriptor =
      MTL::TextureDescriptor::alloc()->init();

  textureDescriptor->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
  textureDescriptor->setWidth(width);
  textureDescriptor->setHeight(height);

  buffer->texture = metalDevice->newTexture(textureDescriptor);
  textureDescriptor->release();
}

void releaseGraphicsBuffer(macos_graphics_buffer *buffer) {
  munmap(buffer->memory, buffer->memSize);
  buffer->memory = NULL;
  buffer->texture->release();
  buffer->texture = NULL;
  buffer->screenVertexBuffer->release();
  buffer->screenVertexBuffer = NULL;
}

// replaces the graphics buffer with a gradient pattern, to debug graphics
// issues.
void debugDrawGradient(macos_graphics_buffer *buffer, uint8_t t) {
  uint8_t *row = (uint8_t *)buffer->memory;
  for (int y = 0; y < buffer->height; y++) {
    uint32_t *pixel = (uint32_t *)row;
    for (int x = 0; x < buffer->width; x++) {
      uint8_t r = (uint8_t)0;     // alpha,alpha?
      uint8_t g = (uint8_t)0;     // blue,red
      uint8_t b = (uint8_t)x + t; // green,green
      uint8_t a = (uint8_t)y;     // red,blue

      *pixel++ = (r << 24) | (g << 16) | (b << 8) | a;
    }

    row += buffer->pitch;
  }
}

void copyBufferToTexture(macos_graphics_buffer *buffer) {
  MTL::Region region = MTL::Region(0, 0, 0, buffer->width, buffer->height, 1);

  buffer->texture->replaceRegion(region, 0, buffer->memory, buffer->pitch);
}

read_file_result macos_readEntireFile(std::string filepath) {
  read_file_result result = {};

  FILE *fp = fopen(filepath.c_str(), "rb");
  fseek(fp, 0, SEEK_END);
  result.contentSize = ftell(fp);
  rewind(fp);

  result.content = mmap(0, result.contentSize, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  size_t bytesRead =
      fread(result.content, sizeof(uint8_t), result.contentSize, fp);
  if (bytesRead != result.contentSize) {
    macos_freeFileMemory(result);
    result.content = NULL;
    result.contentSize = 0;
  }

  return result;
}

void macos_freeFileMemory(read_file_result file) {
  munmap(file.content, file.contentSize);
}

int main(int argc, char **argv) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  ImGui::StyleColorsDark();

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) !=
      0) {
    printf("Error: %s\n", SDL_GetError());
    return -1;
  }

  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
  SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
  SDL_Window *sdlWindow = SDL_CreateWindow(
      "SDL Metal", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH,
      SCREEN_HEIGHT, SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
  assert(sdlWindow != NULL);
  SDL_Renderer *sdlRenderer = SDL_CreateRenderer(
      sdlWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  assert(sdlRenderer != NULL);

  CA::MetalLayer *metalLayer =
      (CA::MetalLayer *)SDL_RenderGetMetalLayer(sdlRenderer);
  metalLayer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);

  ImGui_ImplMetal_Init(metalLayer->device());
  ImGui_ImplSDL2_InitForMetal(sdlWindow);

  float clear_color[4] = {0.45f, 0.55f, 0.60f, 1.00f};

  MTL::Device *metalDevice = metalLayer->device();

  initGraphicsBuffer(&graphicsBuffer, EMULATOR_SCREEN_WIDTH,
                     EMULATOR_SCREEN_HEIGHT, metalDevice);

  auto library_data =
      dispatch_data_create(&obj_shaders_metallib[0], obj_shaders_metallib_len,
                           NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);

  NS::Error *err;
  MTL::Library *metalDefaultLibrary =
      metalDevice->newLibrary(library_data, &err);
  if (!metalDefaultLibrary) {
    fprintf(stderr, "Failed to load default library.\n");
    std::exit(-1);
  }

  MTL::CommandQueue *metalCommandQueue = metalDevice->newCommandQueue();

  MTL::Function *vertexShader = metalDefaultLibrary->newFunction(
      NS::String::string("rectVertexShader", NS::ASCIIStringEncoding));
  assert(vertexShader);
  MTL::Function *fragmentShader = metalDefaultLibrary->newFunction(
      NS::String::string("rectFragmentShader", NS::ASCIIStringEncoding));
  assert(fragmentShader);

  MTL::RenderPipelineDescriptor *renderPipelineDescriptor =
      MTL::RenderPipelineDescriptor::alloc()->init();
  renderPipelineDescriptor->setLabel(
      NS::String::string("Rect Rendering Pipeline", NS::ASCIIStringEncoding));
  renderPipelineDescriptor->setVertexFunction(vertexShader);
  renderPipelineDescriptor->setFragmentFunction(fragmentShader);
  assert(renderPipelineDescriptor);
  renderPipelineDescriptor->colorAttachments()->object(0)->setPixelFormat(
      metalLayer->pixelFormat());

  MTL::RenderPipelineState *metalRenderPSO =
      metalDevice->newRenderPipelineState(renderPipelineDescriptor, &err);
  renderPipelineDescriptor->release();

  // -----------------------------------------------------------------------------

  emulator_bridge bridge = {};
  bridge.emulatorState =
      (emulator_state *)mmap(0, sizeof(emulator_state), PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  bridge.graphicsBuffer = (uint32_t *)graphicsBuffer.memory;
  bridge.platformReadEntireFile = macos_readEntireFile;
  bridge.platformFreeFileMemory = macos_freeFileMemory;

  initEmulator(&bridge, "assets/Tetris.gb");
  initEmulatorImguiFrame(&bridge, io);

  // -----------------------------------------------------------------------------

  Viewport viewport = {SCREEN_WIDTH, SCREEN_HEIGHT};
  uint8_t renderScale = RENDER_SCALE;

  bool quit = false;
  uint8_t t = 0;

  SDL_Event e;
  while (!quit) {
    while (SDL_PollEvent(&e) != 0) {
      ImGui_ImplSDL2_ProcessEvent(&e);
      if (e.type == SDL_QUIT) {
        quit = true;
      }
      if (e.type == SDL_WINDOWEVENT &&
          e.window.event == SDL_WINDOWEVENT_CLOSE &&
          e.window.windowID == SDL_GetWindowID(sdlWindow)) {
        quit = true;
      }
    }

    NS::AutoreleasePool *ppool = NS::AutoreleasePool::alloc()->init();

    // Resize drawable if needed
    int width, height;
    SDL_GetRendererOutputSize(sdlRenderer, &width, &height);
    metalLayer->setDrawableSize(CGSizeMake(width, height));

    viewport = {(uint32_t)width, (uint32_t)height};

    CA::MetalDrawable *metalDrawable = metalLayer->nextDrawable();
    MTL::CommandBuffer *metalCommandBuffer = metalCommandQueue->commandBuffer();

    MTL::RenderPassDescriptor *renderPassDescriptor =
        MTL::RenderPassDescriptor::alloc()->init();
    MTL::RenderPassColorAttachmentDescriptor *cd =
        renderPassDescriptor->colorAttachments()->object(0);

    cd->setTexture(metalDrawable->texture());
    cd->setLoadAction(MTL::LoadActionClear);
    cd->setClearColor(MTL::ClearColor(
        clear_color[0] * clear_color[3], clear_color[1] * clear_color[3],
        clear_color[2] * clear_color[3], clear_color[3]));
    cd->setStoreAction(MTL::StoreActionStore);

    MTL::RenderCommandEncoder *renderCommandEncoder =
        metalCommandBuffer->renderCommandEncoder(renderPassDescriptor);

    renderCommandEncoder->pushDebugGroup(
        NS::String::string("Imgui", NS::ASCIIStringEncoding));

    ImGui_ImplMetal_NewFrame(renderPassDescriptor);
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    drawEmulatorImguiFrame(&bridge, io);

    // ------ emulator logic goes here
    if (bridge.emulatorState->isRunning) {
      stepFrame(&bridge);
    }
    /* debugDrawGradient(&graphicsBuffer, ++t); */
    // ------ emulator logic ends here
    copyBufferToTexture(&graphicsBuffer);

    renderCommandEncoder->setRenderPipelineState(metalRenderPSO);
    renderCommandEncoder->setVertexBuffer(graphicsBuffer.screenVertexBuffer, 0,
                                          0);
    renderCommandEncoder->setVertexBytes(&viewport, sizeof(Viewport), 1);
    renderCommandEncoder->setVertexBytes(&renderScale, sizeof(uint8_t), 2);

    MTL::PrimitiveType typeTriangle = MTL::PrimitiveTypeTriangle;
    NS::UInteger vertexStart = 0;
    NS::UInteger vertexCount = 6;
    renderCommandEncoder->setFragmentTexture(graphicsBuffer.texture, 0);
    renderCommandEncoder->drawPrimitives(typeTriangle, vertexStart,
                                         vertexCount);

    ImGui::Render();
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), metalCommandBuffer,
                                   renderCommandEncoder);

    renderCommandEncoder->popDebugGroup();

    renderCommandEncoder->endEncoding();

    metalCommandBuffer->presentDrawable(metalDrawable);
    metalCommandBuffer->commit();
    metalCommandBuffer->waitUntilCompleted();

    renderPassDescriptor->release();

    ppool->release();
  }

  releaseGraphicsBuffer(&graphicsBuffer);
  metalDevice->release();
  SDL_DestroyRenderer(sdlRenderer);
  SDL_DestroyWindow(sdlWindow);
  SDL_Quit();

  return 0;
}

#else
#include "metal_engine.h"
int main(int argc, char **argv) {
  MTLEngine engine;
  engine.init();
  engine.run();
  engine.cleanup();

  return 0;
}
#endif
