#pragma once

#include "Texture.h"
#include "VertexData.h"
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <SDL2/SDL.h>
#include <filesystem>

class MTLEngine {
public:
  void init();
  void run();
  void cleanup();
  void resizeFrameBuffer(int width, int height);

private:
  // ----- PLATFORM LAYER -----
  void initSDLWindow();

  void createFrameBuffer();
  void createShaderLibrary();
  void createCommandQueue();
  void createRenderPipeline();

  void encodeRenderCommand(MTL::RenderCommandEncoder *renderEncoder);
  void sendRenderCommand();
  void draw();

  bool quit;
  NS::AutoreleasePool *ppool;
  MTL::Device *metalDevice;
  SDL_Window *sdlWindow;
  SDL_Renderer *sdlRenderer;
  CA::MetalLayer *metalLayer;
  CA::MetalDrawable *metalDrawable;

  MTL::Library *metalDefaultLibrary;
  MTL::CommandQueue *metalCommandQueue;
  MTL::CommandBuffer *metalCommandBuffer;
  MTL::RenderPipelineState *metalRenderPSO;
  MTL::Buffer *screenVertexBuffer;

  Texture *frameBuffer;

  // ----- EMULATION LAYER -----
};
