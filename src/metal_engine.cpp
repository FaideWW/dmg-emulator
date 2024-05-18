#define CPP_METAL_INCLUDE

#include "metal_engine.h"
#include "glfw_bridge.h"
#include <simd/simd.h>

#include "generated/metal_shaders.generated.h"

void MTLEngine::init() {
  initDevice();
  initWindow();

  createSquare();
  createDefaultLibrary();
  createCommandQueue();
  createRenderPipeline();
}

void MTLEngine::run() {
  while (!glfwWindowShouldClose(glfwWindow)) {
    ppool = NS::AutoreleasePool::alloc()->init();

    metalDrawable = metalLayer->nextDrawable();
    draw();

    ppool->release();

    glfwPollEvents();
  }
}

void MTLEngine::cleanup() {
  glfwTerminate();
  metalLayer->release();
  metalDevice->release();
}

void MTLEngine::resizeFrameBuffer(int width, int height) {
  metalLayer->setDrawableSize(CGSizeMake(width, height));
}

void MTLEngine::frameBufferSizeCallback(GLFWwindow *window, int width,
                                        int height) {
  MTLEngine *engine = (MTLEngine *)glfwGetWindowUserPointer(window);
  engine->resizeFrameBuffer(width, height);
}

void MTLEngine::initDevice() { metalDevice = MTL::CreateSystemDefaultDevice(); }

void MTLEngine::initWindow() {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindow = glfwCreateWindow(800, 600, "Metal Engine", NULL, NULL);
  if (!glfwWindow) {
    glfwTerminate();
    exit(EXIT_FAILURE);
  }

  int width, height;
  glfwGetFramebufferSize(glfwWindow, &width, &height);
  glfwSetWindowUserPointer(glfwWindow, this);
  glfwSetFramebufferSizeCallback(glfwWindow, frameBufferSizeCallback);

  metalLayer = CA::MetalLayer::layer();
  metalLayer->setDevice(metalDevice);
  metalLayer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
  metalLayer->setDrawableSize(CGSizeMake(width, height));
  GLFWBridge::AddLayerToWindow(glfwWindow, metalLayer);
}

void MTLEngine::createSquare() {
  VertexData squareVertices[]{
      {{-0.5, -0.5, 0.5, 1.0f}, {0.0f, 0.0f}},
      {{-0.5, 0.5, 0.5, 1.0f}, {0.0f, 1.0f}},
      {{0.5, 0.5, 0.5, 1.0f}, {1.0f, 1.0f}},
      {{-0.5, -0.5, 0.5, 1.0f}, {0.0f, 0.0f}},
      {{0.5, 0.5, 0.5, 1.0f}, {1.0f, 1.0f}},
      {{0.5, -0.5, 0.5, 1.0f}, {1.0f, 0.0f}},
  };

  squareVertexBuffer = metalDevice->newBuffer(
      &squareVertices, sizeof(squareVertices), MTL::ResourceStorageModeShared);

  grassTexture = new Texture(800, 600, metalDevice);
}

void MTLEngine::createDefaultLibrary() {

  /* MTL::CompileOptions *opts = MTL::CompileOptions::alloc()->init(); */
  NS::Error *err;

  auto library_data =
      dispatch_data_create(&obj_shaders_metallib[0], obj_shaders_metallib_len,
                           NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);

  metalDefaultLibrary = metalDevice->newLibrary(library_data, &err);

  if (!metalDefaultLibrary) {
    fprintf(stderr, "Failed to load default library.\n");
    std::exit(-1);
  }

  // opts->release();
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
  renderPipelineDescriptor->setLabel(NS::String::string(
      "Triangle Rendering Pipeline", NS::ASCIIStringEncoding));
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
  renderCommandEncoder->setVertexBuffer(squareVertexBuffer, 0, 0);
  MTL::PrimitiveType typeTriangle = MTL::PrimitiveTypeTriangle;
  NS::UInteger vertexStart = 0;
  NS::UInteger vertexCount = 6;
  renderCommandEncoder->setFragmentTexture(grassTexture->texture, 0);
  renderCommandEncoder->drawPrimitives(typeTriangle, vertexStart, vertexCount);
}
