#ifndef NEWMAIN
#define CPP_METAL_INCLUDE

#include "metal_engine.h"
#include <simd/simd.h>

#include "generated/metal_shaders.generated.h"

int SCREEN_WIDTH = 1280;
int SCREEN_HEIGHT = 720;

void MTLEngine::init() {
  initSDLWindow();

  quit = false;

  createFrameBuffer();
  createShaderLibrary();
  createCommandQueue();
  createRenderPipeline();
}

void MTLEngine::run() {
  SDL_Event e;
  while (!quit) {
    while (SDL_PollEvent(&e) != 0) {
      if (e.type == SDL_QUIT) {
        quit = true;
      }
      if (e.type == SDL_WINDOWEVENT &&
          e.window.event == SDL_WINDOWEVENT_CLOSE &&
          e.window.windowID == SDL_GetWindowID(sdlWindow)) {
        quit = true;
      }
    }

    ppool = NS::AutoreleasePool::alloc()->init();

    // Resize drawable if needed
    int width, height;
    SDL_GetRendererOutputSize(sdlRenderer, &width, &height);
    metalLayer->setDrawableSize(CGSizeMake(width, height));
    frameBuffer->resize(width, height);

    metalDrawable = metalLayer->nextDrawable();
    draw();

    ppool->release();
  }
}

void MTLEngine::cleanup() {
  metalDevice->release();
  SDL_DestroyRenderer(sdlRenderer);
  SDL_DestroyWindow(sdlWindow);
  SDL_Quit();
}

void MTLEngine::resizeFrameBuffer(int width, int height) {
  metalLayer->setDrawableSize(CGSizeMake(width, height));
}

void MTLEngine::initSDLWindow() {
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
  SDL_Init(SDL_INIT_VIDEO);

  SDL_Window *window = SDL_CreateWindow(
      "SDL Metal", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH,
      SCREEN_HEIGHT, SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
  assert(window != NULL);
  SDL_Renderer *renderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  assert(renderer != NULL);

  sdlWindow = window;
  sdlRenderer = renderer;

  metalLayer = (CA::MetalLayer *)SDL_RenderGetMetalLayer(renderer);
  metalLayer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
  metalDevice = metalLayer->device();
}

void MTLEngine::createFrameBuffer() {
  VertexData squareVertices[]{
      {{-1, -1, 1, 1.0f}, {0.0f, 0.0f}}, // top left
      {{-1, 1, 1, 1.0f}, {0.0f, 1.0f}},  // bottom left
      {{1, 1, 1, 1.0f}, {1.0f, 1.0f}},   // bottom right
      {{-1, -1, 1, 1.0f}, {0.0f, 0.0f}}, // top left
      {{1, 1, 1, 1.0f}, {1.0f, 1.0f}},   // bottom right
      {{1, -1, 1, 1.0f}, {1.0f, 0.0f}},  // top right
  };

  screenVertexBuffer = metalDevice->newBuffer(
      &squareVertices, sizeof(squareVertices), MTL::ResourceStorageModeShared);

  frameBuffer = new Texture(SCREEN_WIDTH, SCREEN_HEIGHT, metalDevice);
}

void MTLEngine::createShaderLibrary() {

  auto library_data =
      dispatch_data_create(&obj_shaders_metallib[0], obj_shaders_metallib_len,
                           NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);

  NS::Error *err;
  metalDefaultLibrary = metalDevice->newLibrary(library_data, &err);
  if (!metalDefaultLibrary) {
    fprintf(stderr, "Failed to load default library.\n");
    std::exit(-1);
  }
}

void MTLEngine::createCommandQueue() {
  metalCommandQueue = metalDevice->newCommandQueue();
}

void MTLEngine::createRenderPipeline() {
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

  NS::Error *err;
  metalRenderPSO =
      metalDevice->newRenderPipelineState(renderPipelineDescriptor, &err);
  renderPipelineDescriptor->release();
}

void MTLEngine::draw() { sendRenderCommand(); }

void MTLEngine::sendRenderCommand() {
  metalCommandBuffer = metalCommandQueue->commandBuffer();

  MTL::RenderPassDescriptor *renderPassDescriptor =
      MTL::RenderPassDescriptor::alloc()->init();
  MTL::RenderPassColorAttachmentDescriptor *cd =
      renderPassDescriptor->colorAttachments()->object(0);

  cd->setTexture(metalDrawable->texture());
  cd->setLoadAction(MTL::LoadActionClear);
  cd->setClearColor(
      MTL::ClearColor(41.0f / 255.0f, 42.0 / 255.0f, 48.0f / 255.0f, 1.0f));
  cd->setStoreAction(MTL::StoreActionStore);

  MTL::RenderCommandEncoder *renderCommandEncoder =
      metalCommandBuffer->renderCommandEncoder(renderPassDescriptor);
  encodeRenderCommand(renderCommandEncoder);
  renderCommandEncoder->endEncoding();

  metalCommandBuffer->presentDrawable(metalDrawable);
  metalCommandBuffer->commit();
  metalCommandBuffer->waitUntilCompleted();

  renderPassDescriptor->release();
}

void MTLEngine::encodeRenderCommand(
    MTL::RenderCommandEncoder *renderCommandEncoder) {
  renderCommandEncoder->setRenderPipelineState(metalRenderPSO);
  renderCommandEncoder->setVertexBuffer(screenVertexBuffer, 0, 0);
  MTL::PrimitiveType typeTriangle = MTL::PrimitiveTypeTriangle;
  NS::UInteger vertexStart = 0;
  NS::UInteger vertexCount = 6;
  renderCommandEncoder->setFragmentTexture(frameBuffer->texture, 0);
  renderCommandEncoder->drawPrimitives(typeTriangle, vertexStart, vertexCount);
}
#endif
