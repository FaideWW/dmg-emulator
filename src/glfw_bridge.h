#ifndef GLFW_BRIDGE_H_
#define GLFW_BRIDGE_H_

struct GLFWwindow;

namespace CA {
class MetalLayer;
}

namespace GLFWBridge {
void AddLayerToWindow(GLFWwindow *window, CA::MetalLayer *layer);
}

#endif
