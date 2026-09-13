// Separable Lanczos2 with a widening low-pass kernel when reducing resolution.
float4 inputSize : register(c0);
float4 axisScale : register(c1); // x/y axis, input extent, output extent
sampler2D image : register(s0);
float kernel(float x) {
    x = abs(x);
    if (x < .00001) return 1;
    if (x >= 2) return 0;
    return sin(3.14159265359*x)*sin(1.57079632679*x)/(4.93480220054*x*x);
}
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float p = dot(uv, axisScale.xy) * axisScale.z - .5;
    float scale = max(1, axisScale.z / axisScale.w);
    float stride = max(1, ceil(scale / 4));
    float4 sum = 0;
    float weights = 0;
    [unroll] for (int k = -8; k <= 8; ++k) {
        float distance = floor(p) + k*stride - p;
        float weight = kernel(distance / scale);
        sum += tex2D(image, uv + distance * inputSize.xy * axisScale.xy) * weight;
        weights += weight;
    }
    // Limit ringing to the two adjacent samples rather than adding sharpening.
    float4 a = tex2D(image, uv + (floor(p)-p) * inputSize.xy * axisScale.xy);
    float4 b = tex2D(image, uv + (floor(p)+1-p) * inputSize.xy * axisScale.xy);
    return clamp(sum / max(weights, .0001), min(a,b), max(a,b));
}
