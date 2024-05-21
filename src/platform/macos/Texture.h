#pragma once
#include <Metal/Metal.hpp>

class Texture {
public:
  Texture(int width, int height, MTL::Device *metalDevice);
  ~Texture();
  void resize(int width, int height);
  MTL::Texture *texture;

private:
  void drawGradient(int width, int height);
  MTL::Device *device;
};
