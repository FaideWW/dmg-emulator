#pragma once
#include <Metal/Metal.hpp>
#include <stb/stb_image.h>

class Texture {
public:
  Texture(int width, int height, MTL::Device *metalDevice);
  ~Texture();
  MTL::Texture *texture;
  int width, height, channels;

private:
  MTL::Device *device;
};
