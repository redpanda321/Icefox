float4x4 mLayerQuadTransform;
float4x4 mLayerTransform;
float4 vRenderTargetOffset;
float4x4 mProjection;

texture tex0;
sampler s2D;
sampler s2DY;
sampler s2DCb;
sampler s2DCr;

float fLayerOpacity;
float4 fLayerColor;

struct VS_INPUT {
  float4 vPosition : POSITION;
};

struct VS_OUTPUT {
  float4 vPosition : POSITION;
  float2 vTexCoords : TEXCOORD0;
};

VS_OUTPUT LayerQuadVS(const VS_INPUT aVertex)
{
  VS_OUTPUT outp;
  outp.vPosition = aVertex.vPosition;
  outp.vPosition = mul(mLayerQuadTransform, outp.vPosition);
  outp.vPosition = mul(mLayerTransform, outp.vPosition);
  outp.vPosition = outp.vPosition - vRenderTargetOffset;
  outp.vPosition = mul(mProjection, outp.vPosition);
  outp.vTexCoords = aVertex.vPosition.xy;
  return outp;
}

float4 RGBShader(const VS_OUTPUT aVertex) : COLOR
{
  return tex2D(s2D, aVertex.vTexCoords) * fLayerOpacity;
}

float4 YCbCrShader(const VS_OUTPUT aVertex) : COLOR
{
  float4 yuv;
  float4 color;

  yuv.r = tex2D(s2DCr, aVertex.vTexCoords).r - 0.5;
  yuv.g = tex2D(s2DY, aVertex.vTexCoords).r - 0.0625;
  yuv.b = tex2D(s2DCb, aVertex.vTexCoords).r - 0.5;

  color.r = yuv.g * 1.164 + yuv.r * 1.596;
  color.g = yuv.g * 1.164 - 0.813 * yuv.r - 0.391 * yuv.b;
  color.b = yuv.g * 1.164 + yuv.b * 2.018;
  color.a = 1.0f;
 
  return color * fLayerOpacity;
}

float4 SolidColorShader(const VS_OUTPUT aVertex) : COLOR
{
  return fLayerColor;
}
