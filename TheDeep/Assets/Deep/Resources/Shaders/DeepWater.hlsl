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

// The ship's lamps and floodlights and the diver's helmet lamp, set by Nautilus.cs / Diver.cs every frame (only those
// near the camera). Lamp i: pos xyz + range w; colour rgb + w = 1 if it shines through water (absorbed on the way).
#define DEEP_MAX_LAMPS 24
float4 _DeepLampPos[DEEP_MAX_LAMPS];
float4 _DeepLampCol[DEEP_MAX_LAMPS];
float _DeepLampCount;
float4 _DeepHeadPos;   // the helmet lamp: xyz, w = range (0 = off)
float4 _DeepHeadDir;   // xyz the beam, w = cos of the cone's edge
float4 _DeepHeadCol;   // rgb, w = 1 under water

float DeepFalloff(float d2, float r)
{
    float a = saturate(1 - d2 / (r * r));
    return a * a / (1 + d2 * 0.6);
}

float3 DeepLamps(float3 ws, float3 n, float gloss)
{
    float3 sum = 0;
    float3 v = normalize(GetCameraPositionWS() - ws);
    int cnt = (int)_DeepLampCount;
    for (int i = 0; i < cnt; i++)
    {
        float3 L = _DeepLampPos[i].xyz - ws;
        float d2 = max(dot(L, L), 1e-4);
        float r = _DeepLampPos[i].w;
        if (d2 > r * r) continue;
        float3 l = L * rsqrt(d2);
        float ndl = saturate(dot(n, l)) * 0.85 + 0.15;
        float3 c = _DeepLampCol[i].rgb * DeepFalloff(d2, r);
        if (_DeepLampCol[i].w > 0.5) c *= exp(-sqrt(d2) * _DeepAbsorb.rgb * 2.0);
        float sp = pow(saturate(dot(reflect(-l, n), v)), 24) * gloss * 2;
        sum += c * (ndl + sp);
    }
    if (_DeepHeadPos.w > 0)
    {
        float3 L = _DeepHeadPos.xyz - ws;
        float d2 = max(dot(L, L), 1e-4);
        float3 l = L * rsqrt(d2);
        float cone = smoothstep(_DeepHeadDir.w, _DeepHeadDir.w + 0.12, dot(-l, _DeepHeadDir.xyz));
        float3 c = _DeepHeadCol.rgb * cone * DeepFalloff(d2, _DeepHeadPos.w) * 3.0;
        if (_DeepHeadCol.w > 0.5) c *= exp(-sqrt(d2) * _DeepAbsorb.rgb * 2.0);
        float sp = pow(saturate(dot(reflect(-l, n), v)), 24) * gloss * 2;
        sum += c * (saturate(dot(n, l)) + sp);
    }
    return sum;
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
    return albedo * (sun + amb + DeepLamps(ws, n, gloss) * ao) + L.color * trans * spec * L.shadowAttenuation;
}

// inside the Nautilus: no sun and no caustics, only a little of the sea's light through the windows and hatches, and
// her lamps (and the helmet lamp)
float3 DeepLightInside(float3 ws, float3 n, float3 albedo, float gloss, float ao)
{
    float depth = -ws.y;
    float3 amb = lerp(_DeepAmbTop.rgb, _DeepAmbDeep.rgb, saturate(depth / 150.0)) * 0.22 * (0.6 + 0.4 * saturate(n.y * 0.5 + 0.5)) * ao;
    return albedo * (amb + DeepLamps(ws, n, gloss) * ao);
}
#endif
