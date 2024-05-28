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
  textureDescriptor->release();
}
