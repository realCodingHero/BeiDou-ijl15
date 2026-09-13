// CuNNy subpixel reconstruction; the residual channel order and YUV matrices
// match the pinned upstream model. The source is in its original display space.
float4 inputSize : register(c0);
sampler2D color : register(s0);
sampler2D residual : register(s1);
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float2 pixel = floor(uv * inputSize.zw * 2.0);
    float2 parity = pixel - floor(pixel * .5) * 2.0;
    float4 r = tex2D(residual, (floor(pixel * .5) + .5) * inputSize.xy);
    float correction = lerp(lerp(r.x, r.y, parity.x), lerp(r.z, r.w, parity.x), parity.y);
    float3 rgb = tex2D(color, uv).rgb;
    float3 yuv = mul(float3x3(.299,.587,.114,-.169,-.331,.5,.5,-.419,-.081), rgb);
    yuv.x = saturate(yuv.x + correction);
    return float4(mul(float3x3(1,-.00093,1.401687,1,-.3437,-.71417,1,1.77216,.00099), yuv), 1);
}
