#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 resolution;
    float strength;
    float cornerRadius;
};

float roundedRectangleMask(vec2 uv, vec2 size, float radius)
{
    float safeRadius = clamp(radius, 0.0, min(size.x, size.y) * 0.5);
    vec2 centered = abs((uv * size) - (size * 0.5));
    vec2 distanceToCorner = centered - ((size * 0.5) - vec2(safeRadius));
    float distance = length(max(distanceToCorner, vec2(0.0)))
                   + min(max(distanceToCorner.x, distanceToCorner.y), 0.0)
                   - safeRadius;
    return 1.0 - smoothstep(-1.0, 1.0, distance);
}

void main()
{
    vec2 uv = qt_TexCoord0;
    float diagonal = 1.0 - smoothstep(0.15, 0.92, uv.x + uv.y);
    float upperEdge = 1.0 - smoothstep(0.0, 0.16, uv.y);
    float sideEdge = 1.0 - smoothstep(0.0, 0.08, uv.x)
                   + smoothstep(0.92, 1.0, uv.x);
    float luminance = (diagonal * 0.10 + upperEdge * 0.18 + sideEdge * 0.04)
                    * strength;
    float alpha = luminance * qt_Opacity
                * roundedRectangleMask(uv, resolution, cornerRadius);
    vec3 tint = vec3(0.72, 0.92, 1.0);

    // Qt Quick blends ShaderEffect output as premultiplied alpha. Keeping the
    // tint unscaled causes GPU backends to render an opaque cyan rectangle.
    fragColor = vec4(tint * alpha, alpha);
}
