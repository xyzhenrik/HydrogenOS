#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 resolution;
    float strength;
};

void main()
{
    vec2 uv = qt_TexCoord0;
    float diagonal = smoothstep(0.92, 0.15, uv.x + uv.y);
    float upperEdge = smoothstep(0.16, 0.0, uv.y);
    float sideEdge = smoothstep(0.08, 0.0, uv.x)
                   + smoothstep(0.92, 1.0, uv.x);
    float luminance = (diagonal * 0.10 + upperEdge * 0.18 + sideEdge * 0.04)
                    * strength;
    fragColor = vec4(0.72, 0.92, 1.0, luminance) * qt_Opacity;
}

