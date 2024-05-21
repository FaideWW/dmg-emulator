#include <metal_stdlib>
using namespace metal;

#include "../VertexData.h"

struct VertexOut {
  float4 position [[position]];
  float2 textureCoordinate;
};

vertex VertexOut rectVertexShader(uint vertexId [[vertex_id]],
                                  constant VertexData *vertexData,
                                  constant Viewport *viewportSizePointer,
                                  constant uint8_t *renderScale) {
  VertexOut out;

  float2 pixelSpacePosition = vertexData[vertexId].position.xy;
  float2 viewportSize = float2(*viewportSizePointer);

  out.position = vector_float4(0.0, 0.0, 0.0, 1.0);
  out.position.xy = pixelSpacePosition / (viewportSize / 2.0) * (*renderScale);

  out.textureCoordinate = vertexData[vertexId].textureCoordinate;
  out.textureCoordinate.y = 1 - out.textureCoordinate.y;

  return out;
}

fragment float4 rectFragmentShader(VertexOut in [[stage_in]],
                                   texture2d<float> colorTexture
                                   [[texture(0)]]) {
  constexpr sampler textureSampler(mag_filter::linear, min_filter::linear);
  const float4 colorSample =
      colorTexture.sample(textureSampler, in.textureCoordinate);
  return colorSample;
}
