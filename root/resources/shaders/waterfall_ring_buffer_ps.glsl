#version 330 core

out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D rawDataTex;
uniform sampler2D paletteTex;

uniform float uOffset;

void main() {
    // Snap the X coord to the exact center of the nearest texel
    ivec2 texSize = textureSize(rawDataTex, 0);
    float centeredX = (floor(TexCoord.x * float(texSize.x)) + 0.5) / float(texSize.x);

    float y = uOffset + TexCoord.y;
    if (y >= 1.0) y -= 1.0;

    float norm = texture(rawDataTex, vec2(centeredX, y)).r;
    FragColor = texture(paletteTex, vec2(norm, 0.5));
}