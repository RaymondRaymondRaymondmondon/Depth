// The seabed (a Terrain material): four layers from the terrain's splat map (sand, rock, reef rubble, mud), world-space
// UVs, the rock triplanar so the canyon walls don't stretch, normal maps, and The Deep's water lighting.
Shader "Deep/Seabed"
{
    Properties
    {
        [HideInInspector] _TerrainHolesTexture ("Holes", 2D) = "white" {}
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
                // normals: blend the layers' tangent-space normals (UDN) around the up-facing frame; walls keep the mesh normal
                float3 tn = Nrm(TEXTURE2D_ARGS(_Normal0, sampler_Normal0), p / _Tiles.x) * w.r + Nrm(TEXTURE2D_ARGS(_Normal2, sampler_Normal0), p / _Tiles.z) * w.b
                          + Nrm(TEXTURE2D_ARGS(_Normal3, sampler_Normal0), p / _Tiles.w) * w.a + float3(0, 0, 1) * w.g;
                float3 nn = normalize(n + float3(tn.x, 0, tn.y) * saturate(n.y) * 0.8);
                // a little cavity darkening from the normal map's slope (cheap ambient occlusion)
                float ao = saturate(0.75 + tn.z * 0.25);
                float3 col = DeepLight(i.ws, nn, albedo, 0, 0.08, ao);
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
