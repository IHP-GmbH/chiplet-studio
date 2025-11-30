#version 330 core

layout(location = 0) in vec3 position;

uniform mat4 modelViewProjection;

out vec3 fragPosition;

void main() {
    fragPosition = position;
    gl_Position = modelViewProjection * vec4(position, 1.0);
}
