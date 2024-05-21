#pragma once
#include <simd/simd.h>

using namespace simd;

struct VertexData {
  vector_float2 position;
  vector_float2 textureCoordinate;
};

typedef vector_uint2 Viewport;
