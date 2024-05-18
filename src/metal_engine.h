#pragma once

#include "Texture.h"
#include "VertexData.h"
#include "glfw_bridge.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <filesystem>
#include <stb/stb_image.h>

class MTLEngine {
public:
  void init();
  void run();
  void cleanup();
  void resizeFrameBuffer(int width, int height);

  static void frameBufferSizeCallback(GLFWwindow *window, int width,
                                      int height);

private:
  void initDevice();
  void initWindow();

  void createSquare();
  void createDefaultLibrary();
  void createCommandQueue();
  void createRenderPipeline();

  void encodeRenderCommand(MTL::RenderCommandEncoder *renderEncoder);
  void sendRenderCommand();
  void draw();

  NS::AutoreleasePool *ppool;
  MTL::Device *metalDevice;
  GLFWwindow *glfwWindow;
  CA::MetalLayer *metalLayer;
  CA::MetalDrawable *metalDrawable;

  MTL::Library *metalDefaultLibrary;
  MTL::CommandQueue *metalCommandQueue;
  MTL::CommandBuffer *metalCommandBuffer;
  MTL::RenderPipelineState *metalRenderPSO;
  MTL::Buffer *squareVertexBuffer;

  Texture *grassTexture;
};
