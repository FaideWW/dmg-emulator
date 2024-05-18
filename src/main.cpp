#include "metal_engine.h"
#include <Metal/Metal.hpp>
#include <stdio.h>

int main(int argc, char **argv) {
  MTLEngine engine;
  engine.init();
  engine.run();
  engine.cleanup();

  return 0;
}
