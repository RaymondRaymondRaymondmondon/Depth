// Shared underwater lighting for The Deep's own shaders (seabed, plants, creatures, the Nautilus's hull).
// Light is absorbed by the water above each surface: reds first, then greens, so deeper surfaces turn blue-green and
// dark; caustics ripple over anything facing up in the shallows; the water itself lights everything a little from
// all sides (the ambient). The camera's own depth only sets the fog (in UnderwaterLook.cs).
#ifndef DEEP_WATER_INCLUDED
#define DEEP_WATER_INCLUDED
#include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Lighting.hlsl"

TEXTURE2D(_DeepCausticTex); SAMPLER(sampler_DeepCausticTex);
float4 _DeepSunDir;       // xyz: the light's travel direction
float4 _DeepSunColor;     // rgb, a = daylight 0..1
float4 _DeepAbsorb;       // per metre, rgb extinction; w = overall falloff per metre
float4 _DeepAmbTop, _DeepAmbDeep;   // the water's own light near the surface and at 150 m
float _DeepTime, _DeepCaustics;     // caustic strength (0 at night)

// light left at a depth (positive metres below the surface)
float3 DeepTransmit(float depth)
{
    depth = max(0, depth);
    return exp(-depth * _DeepAbsorb.rgb) * exp(-depth * _DeepAbsorb.w);
}

// the caustic web at a world point: two scrolling, differently scaled lookups; their minimum makes the sharp lines
float DeepCaustic(float3 ws)
{
    float2 uv = ws.xz * 0.11;
    float a = SAMPLE_TEXTURE2D(_DeepCausticTex, sampler_DeepCausticTex, uv + float2(_DeepTime * 0.031, _DeepTime * 0.017)).r;
    float b = SAMPLE_TEXTURE2D(_DeepCausticTex, sampler_DeepCausticTex, uv * 1.37 + float2(-_DeepTime * 0.023, _DeepTime * 0.027)).r;
    return min(a, b);
}

// the colour of a lit surface under water (above water the transmit is 1 and there are no caustics)
float3 DeepLight(float3 ws, float3 n, float3 albedo, float translucency, float gloss, float ao)
{
    float depth = -ws.y;
    float4 sc = TransformWorldToShadowCoord(ws);
    Light L = GetMainLight(sc, ws, half4(1, 1, 1, 1));
    float3 trans = DeepTransmit(depth);
    float ndl = dot(n, L.direction);
    float diffuse = saturate(ndl) + saturate(-ndl) * translucency;
    float caust = 1;
    if (depth > 0)
    {
        float c = DeepCaustic(ws);
        float k = _DeepCaustics * saturate(1 - depth / 45.0) * saturate(n.y * 1.5);   // on surfaces facing up, fading with depth
        caust = lerp(1, 0.55 + c * 2.2, k);
    }
    float3 sun = L.color * trans * diffuse * caust * L.shadowAttenuation;
    float3 amb = lerp(_DeepAmbTop.rgb, _DeepAmbDeep.rgb, saturate(depth / 150.0)) * (0.55 + 0.45 * saturate(n.y * 0.5 + 0.5)) * ao;
    if (depth <= 0) amb = _DeepAmbTop.rgb * 2.2 * (0.6 + 0.4 * saturate(n.y)) * ao;
    float3 v = normalize(GetCameraPositionWS() - ws);
    float spec = pow(saturate(dot(reflect(-L.direction, n), v)), 32) * gloss;
    return albedo * (sun + amb) + L.color * trans * spec * L.shadowAttenuation;
}
#endif
