#ifndef MACOS_MAIN_H_
#define MACOS_MAIN_H_
#include "../../emulation/emulator.h"
#include "../../emulation/ui.h"
#include "VertexData.h"
#include <Metal/Metal.hpp>

struct macos_graphics_buffer {
  int width;         // width in pixels
  int height;        // height in pixels
  int pitch;         // size (in bytes) of a row of pixels
  int bytesPerPixel; // Always 4 (rgba)

  size_t memSize; // size of the memory region
  void *memory;   // pixel data

  // metal resources
  MTL::Buffer *screenVertexBuffer;
  MTL::Texture *texture;
};

void initGraphicsBuffer(macos_graphics_buffer *buffer, int width, int height,
                        MTL::Device *metalDevice);
void resizeGraphicsBuffer(macos_graphics_buffer *buffer, int width, int height,
                          MTL::Device *metalDevice);
void releaseGraphicsBuffer(macos_graphics_buffer *buffer);

read_file_result macos_readEntireFile(std::string fp);
void macos_freeFileMemory(read_file_result file);
#endif
