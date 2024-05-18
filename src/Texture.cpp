#include "Texture.h"

Texture::Texture(int width, int height, MTL::Device *metalDevice) {
  device = metalDevice;

  stbi_set_flip_vertically_on_load(true);
  /* unsigned char *image = */
  /*     stbi_load(filepath, &width, &height, &channels, STBI_rgb_alpha); */
  /* assert(image != NULL); */

  void *image = malloc(sizeof(uint8_t) * width * height * 4);

  int pitch = sizeof(uint8_t) * width * 4;

  uint8_t *row = (uint8_t *)image;
  for (int y = 0; y < height; y++) {
    uint32_t *pixel = (uint32_t *)row;
    for (int x = 0; x < width; x++) {
      uint8_t blue = (uint8_t)x;
      uint8_t green = (uint8_t)y;
      /* uint8_t red = 0; */
      /* uint8_t alpha = 1; */

      *pixel++ = ((green << 8) | blue);
    }

    row += pitch;
  }

  MTL::TextureDescriptor *textureDescriptor =
      MTL::TextureDescriptor::alloc()->init();

  textureDescriptor->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  textureDescriptor->setWidth(width);
  textureDescriptor->setHeight(height);

  texture = device->newTexture(textureDescriptor);

  MTL::Region region = MTL::Region(0, 0, 0, width, height, 1);
  NS::UInteger bytesPerRow = 4 * width;

  texture->replaceRegion(region, 0, image, bytesPerRow);

  textureDescriptor->release();
  free(image);
}

Texture::~Texture() { texture->release(); }
