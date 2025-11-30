#version 330 core

in vec3 fragPosition;

uniform vec4 gridColor;
uniform float fadeDistance;

out vec4 FragColor;

void main() {
    // Fade grid based on distance from origin
    float dist = length(fragPosition.xz);
    float fade = 1.0 - smoothstep(fadeDistance * 0.5, fadeDistance, dist);

    FragColor = vec4(gridColor.rgb, gridColor.a * fade);
}
