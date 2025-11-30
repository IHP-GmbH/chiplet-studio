#version 330 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

uniform mat4 modelViewProjection;
uniform mat4 modelView;
uniform mat3 normalMatrix;

out vec3 fragNormal;
out vec3 fragPosition;

void main() {
    fragNormal = normalMatrix * normal;
    fragPosition = vec3(modelView * vec4(position, 1.0));
    gl_Position = modelViewProjection * vec4(position, 1.0);
}
