#version 330 core

// Fragment shader — receives interpolated lit color from vertex shader

in vec4 fragColor;    // input: interpolated lit color as rgba-value
out vec4 finalColor;  // output: final color value as rgba-value

void main() {
  finalColor = fragColor;
}
