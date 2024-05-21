#include "Texture.h"

Texture::Texture(int width, int height, MTL::Device *metalDevice) {
  device = metalDevice;
  resize(width, height);
}

Texture::~Texture() {
  texture->release();
  texture = NULL;
}

void Texture::resize(int width, int height) {
  if (texture != NULL) {
    texture->release();
    texture = NULL;
  }

  MTL::TextureDescriptor *textureDescriptor =
      MTL::TextureDescriptor::alloc()->init();

  textureDescriptor->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
  textureDescriptor->setWidth(width);
  textureDescriptor->setHeight(height);

  texture = device->newTexture(textureDescriptor);
  drawGradient(width, height);
  textureDescriptor->release();
}

void Texture::drawGradient(int width, int height) {
  void *image = malloc(sizeof(uint8_t) * width * height * 4);

  int pitch = sizeof(uint8_t) * width * 4;

  uint8_t *row = (uint8_t *)image;
  for (int y = 0; y < height; y++) {
    uint32_t *pixel = (uint32_t *)row;
    for (int x = 0; x < width; x++) {
      uint8_t r = (uint8_t)0; // alpha,alpha?
      uint8_t g = (uint8_t)0; // blue,red
      uint8_t b = (uint8_t)x; // green,green
      uint8_t a = (uint8_t)y; // red,blue

      *pixel++ = (r << 24) | (g << 16) | (b << 8) | a;
    }

    row += pitch;
  }

  MTL::Region region = MTL::Region(0, 0, 0, width, height, 1);
  NS::UInteger bytesPerRow = 4 * width;

  texture->replaceRegion(region, 0, image, bytesPerRow);
  free(image);
}
