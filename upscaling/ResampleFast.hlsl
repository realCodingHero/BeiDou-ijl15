// Same Lanczos2 kernel for scale <= 1.5. Weights are precomputed on resize.
float4 inputSize : register(c0);
float4 axisScale : register(c1);
sampler2D image : register(s0);
sampler2D weights : register(s1);
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float coordinate = dot(uv, axisScale.xy);
    float p = coordinate * axisScale.z - .5;
    float2 step = inputSize.xy * axisScale.xy;
    float2 base = uv + (floor(p) - p) * step;
    float4 w0 = tex2D(weights, float2(coordinate, .25));
    float4 w1 = tex2D(weights, float2(coordinate, .75));
    float4 a = tex2D(image, base);
    float4 b = tex2D(image, base + step);
    float4 sum = tex2D(image, base - 2*step)*w0.x + tex2D(image, base-step)*w0.y
        + a*w0.z + b*w0.w + tex2D(image, base+2*step)*w1.x + tex2D(image, base+3*step)*w1.y;
    return clamp(sum, min(a,b), max(a,b));
}
