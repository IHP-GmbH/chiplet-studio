#version 330 core

in vec3 fragNormal;
in vec3 fragPosition;

uniform vec4 objectColor;
uniform vec3 lightDirection;
uniform bool selected;

out vec4 FragColor;

void main() {
    // Phong lighting
    vec3 norm = normalize(fragNormal);
    float ambient = 0.3;
    float diffuse = max(dot(norm, -lightDirection), 0.0) * 0.6;
    float lighting = ambient + diffuse;

    vec3 color = objectColor.rgb * lighting;

    // Selection highlight
    if (selected) {
        color = mix(color, vec3(1.0, 0.8, 0.0), 0.3);
    }

    FragColor = vec4(color, objectColor.a);
}
