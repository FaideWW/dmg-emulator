#include <metal_stdlib>
using namespace metal;

#include "../VertexData.h"

struct VertexOut {
  float4 position [[position]];
  float2 textureCoordinate;
};

vertex VertexOut rectVertexShader(uint vertexId [[vertex_id]],
                                  constant VertexData *vertexData) {
  VertexOut out;
  out.position = vertexData[vertexId].position;
  out.textureCoordinate = vertexData[vertexId].textureCoordinate;

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
