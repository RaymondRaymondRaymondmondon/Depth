// The seabed (a Terrain material): four layers from the terrain's splat map (sand, rock, reef rubble, mud), world-space
// UVs, the rock triplanar so the canyon walls don't stretch, normal maps, and The Deep's water lighting.
Shader "Deep/Seabed"
{
    Properties
    {
        [HideInInspector] _TerrainHolesTexture ("Holes", 2D) = "white" {}
        _Basalt ("Basalt and magma (the vents)", Float) = 0
        [HideInInspector] _Control ("Control", 2D) = "red" {}
        [HideInInspector] _Splat0 ("L0", 2D) = "white" {}
        [HideInInspector] _Splat1 ("L1", 2D) = "white" {}
        [HideInInspector] _Splat2 ("L2", 2D) = "white" {}
        [HideInInspector] _Splat3 ("L3", 2D) = "white" {}
        [HideInInspector] _Normal0 ("N0", 2D) = "bump" {}
        [HideInInspector] _Normal1 ("N1", 2D) = "bump" {}
        [HideInInspector] _Normal2 ("N2", 2D) = "bump" {}
        [HideInInspector] _Normal3 ("N3", 2D) = "bump" {}
        _Tiles ("Tile sizes (m)", Vector) = (7, 9, 5, 8)
    }
    SubShader
    {
        Tags { "RenderType" = "Opaque" "RenderPipeline" = "UniversalPipeline" "Queue" = "Geometry-100" }
        Pass
        {
            Name "Seabed"
            Tags { "LightMode" = "UniversalForward" }
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma multi_compile_fog
            #pragma multi_compile _ _MAIN_LIGHT_SHADOWS _MAIN_LIGHT_SHADOWS_CASCADE
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            #include "DeepWater.hlsl"
            TEXTURE2D(_Control); SAMPLER(sampler_Control); float4 _Control_TexelSize;
            TEXTURE2D(_Splat0); TEXTURE2D(_Splat1); TEXTURE2D(_Splat2); TEXTURE2D(_Splat3); SAMPLER(sampler_Splat0);
            TEXTURE2D(_Normal0); TEXTURE2D(_Normal1); TEXTURE2D(_Normal2); TEXTURE2D(_Normal3); SAMPLER(sampler_Normal0);
            float4 _Tiles;
            float4 _TerrainSize;   // set by Seabed.cs: x size, z size
            TEXTURE2D(_TerrainHolesTexture); SAMPLER(sampler_TerrainHolesTexture);   // the caves' mouths (Caverns.cs cuts them)
            float _Basalt;
            // the vents' cracks: the edges of a Voronoi pattern (distance to the nearest edge, roughly)
            float2 Hash2(float2 p) { p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3))); return frac(sin(p) * 43758.5453); }
            float Cracks(float2 x)
            {
                float2 n = floor(x), f = frac(x); float d1 = 8, d2 = 8;
                for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++)
                {
                    float2 g = float2(i, j); float2 o = Hash2(n + g); float2 r = g + o - f; float d = dot(r, r);
                    if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d;
                }
                return sqrt(d2) - sqrt(d1);
            }
            struct A { float4 pos : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };
            struct V { float4 cs : SV_POSITION; float3 ws : TEXCOORD0; float3 n : TEXCOORD1; float2 uv : TEXCOORD2; float fog : TEXCOORD3; };
            V vert(A i)
            {
                V o; o.ws = TransformObjectToWorld(i.pos.xyz); o.n = TransformObjectToWorldNormal(i.n);
                o.cs = TransformWorldToHClip(o.ws); o.uv = i.uv; o.fog = ComputeFogFactor(o.cs.z);
                return o;
            }
            float3 Nrm(TEXTURE2D_PARAM(t, s), float2 uv) { return UnpackNormal(SAMPLE_TEXTURE2D(t, s, uv)); }
            float4 frag(V i) : SV_Target
            {
                clip(SAMPLE_TEXTURE2D(_TerrainHolesTexture, sampler_TerrainHolesTexture, i.uv).r - 0.5);
                float4 w = SAMPLE_TEXTURE2D(_Control, sampler_Control, i.uv);
                float3 n = normalize(i.n);
                float2 p = i.ws.xz;
                // layers 0, 2, 3 from above; the rock triplanar
                float4 c0 = SAMPLE_TEXTURE2D(_Splat0, sampler_Splat0, p / _Tiles.x);
                float4 c2 = SAMPLE_TEXTURE2D(_Splat2, sampler_Splat0, p / _Tiles.z);
                float4 c3 = SAMPLE_TEXTURE2D(_Splat3, sampler_Splat0, p / _Tiles.w);
                float3 bw = pow(abs(n), 4); bw /= (bw.x + bw.y + bw.z);
                float4 c1 = SAMPLE_TEXTURE2D(_Splat1, sampler_Splat0, i.ws.zy / _Tiles.y) * bw.x
                          + SAMPLE_TEXTURE2D(_Splat1, sampler_Splat0, i.ws.xz / _Tiles.y) * bw.y
                          + SAMPLE_TEXTURE2D(_Splat1, sampler_Splat0, i.ws.xy / _Tiles.y) * bw.z;
                float3 albedo = c0.rgb * w.r + c1.rgb * w.g + c2.rgb * w.b + c3.rgb * w.a;
                if (_Basalt > 0.5)
                {
                    // basalt: the same surfaces turned black and grey, the plateaus' ash pale with sulphur in it
                    float g = dot(albedo, float3(0.3, 0.59, 0.11));
                    float3 basalt = float3(0.07, 0.068, 0.065) + g * float3(0.22, 0.2, 0.19);
                    float3 ash = float3(0.42, 0.38, 0.3) * (0.6 + g) + float3(0.15, 0.12, 0) * saturate(g * 2 - 0.6);
                    albedo = basalt * (w.r + w.g + w.a) + ash * w.b;
                }
                // normals: blend the layers' tangent-space normals (UDN) around the up-facing frame; walls keep the mesh normal
                float3 tn = Nrm(TEXTURE2D_ARGS(_Normal0, sampler_Normal0), p / _Tiles.x) * w.r + Nrm(TEXTURE2D_ARGS(_Normal2, sampler_Normal0), p / _Tiles.z) * w.b
                          + Nrm(TEXTURE2D_ARGS(_Normal3, sampler_Normal0), p / _Tiles.w) * w.a + float3(0, 0, 1) * w.g;
                float3 nn = normalize(n + float3(tn.x, 0, tn.y) * saturate(n.y) * 0.8);
                // a little cavity darkening from the normal map's slope (cheap ambient occlusion)
                float ao = saturate(0.75 + tn.z * 0.25);
                float3 col = DeepLight(i.ws, nn, albedo, 0, 0.08, ao);
                if (_Basalt > 0.5)
                {
                    // magma glowing up through the cracks where the ground is hot (the splat's fourth weight), breathing slowly
                    float e = Cracks(i.ws.xz * 0.16) , e2 = Cracks(i.ws.xz * 0.55 + 7.3);
                    float crack = saturate(1 - e / 0.07) + 0.5 * saturate(1 - e2 / 0.05);
                    float heat = saturate(w.a * 1.4 - 0.15);
                    float breathe = 0.75 + 0.25 * sin(_DeepTime * 0.9 + i.ws.x * 0.05 + i.ws.z * 0.07);
                    col += float3(1.0, 0.3, 0.05) * crack * heat * breathe * 2.2;
                }
                col = MixFog(col, i.fog);
                return float4(col, 1);
            }
            ENDHLSL
        }
        Pass
        {
            Name "ShadowCaster"
            Tags { "LightMode" = "ShadowCaster" }
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Shadows.hlsl"
            float3 _LightDirection;
            TEXTURE2D(_TerrainHolesTexture); SAMPLER(sampler_TerrainHolesTexture);
            struct A { float4 pos : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };
            struct V { float4 cs : SV_POSITION; float2 uv : TEXCOORD0; };
            V vert(A i) { V o; float3 ws = TransformObjectToWorld(i.pos.xyz); o.cs = TransformWorldToHClip(ApplyShadowBias(ws, TransformObjectToWorldNormal(i.n), _LightDirection)); o.uv = i.uv; return o; }
            float4 frag(V i) : SV_Target { clip(SAMPLE_TEXTURE2D(_TerrainHolesTexture, sampler_TerrainHolesTexture, i.uv).r - 0.5); return 0; }
            ENDHLSL
        }
        Pass
        {
            Name "DepthOnly"
            Tags { "LightMode" = "DepthOnly" }
            ColorMask R
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            TEXTURE2D(_TerrainHolesTexture); SAMPLER(sampler_TerrainHolesTexture);
            struct A { float4 pos : POSITION; float2 uv : TEXCOORD0; };
            struct V { float4 cs : SV_POSITION; float2 uv : TEXCOORD0; };
            V vert(A i) { V o; o.cs = TransformObjectToHClip(i.pos.xyz); o.uv = i.uv; return o; }
            float4 frag(V i) : SV_Target { clip(SAMPLE_TEXTURE2D(_TerrainHolesTexture, sampler_TerrainHolesTexture, i.uv).r - 0.5); return 0; }
            ENDHLSL
        }
    }
}
