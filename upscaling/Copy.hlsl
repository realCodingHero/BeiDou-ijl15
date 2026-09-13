sampler2D image : register(s0);
float4 main(float2 uv : TEXCOORD0) : COLOR0 { return float4(tex2D(image, uv).rgb, 1); }
