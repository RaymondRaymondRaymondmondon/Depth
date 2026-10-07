// The Nautilus's own materials (and later any Blender-made model): the textures the artgen tools bake (base colour,
// a tangent-space normal map, glTF's metal/rough map: G roughness, B metal) with the ambient occlusion baked into
// vertex colours, lit by DeepWater.hlsl: absorbed sunlight and caustics outside, and only the ship's lamps inside
// (_Interior). Lamp globes glow with the ship's power (_Emit x _DeepPower). There are no mesh tangents: the normal
// map's frame comes from screen derivatives.
Shader "Deep/Lit"
{
    Properties
    {
        _BaseMap ("Base", 2D) = "white" {}
        _BaseColor ("Base colour", Color) = (1,1,1,1)
        _BumpMap ("Normal", 2D) = "bump" {}
        _MRMap ("Metal/rough (G rough, B metal)", 2D) = "white" {}
        _Metallic ("Metallic", Float) = 0
        _Roughness ("Roughness", Float) = 1
        _NormalScale ("Normal strength", Float) = 1
        _HasBump ("Has normal map", Float) = 0
        _UseVC ("Vertex colour is AO", Float) = 1
        _Interior ("Interior", Float) = 0
        _Emit ("Glow with power", Float) = 0
        _Cull ("Cull (0 off, 2 back)", Float) = 2
    }
    SubShader
    {
        Tags { "RenderType" = "Opaque" "RenderPipeline" = "UniversalPipeline" }
        Pass
        {
            Name "DeepLit"
            Tags { "LightMode" = "UniversalForward" }
            Cull [_Cull]
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma multi_compile_instancing
            #pragma multi_compile_fog
            #pragma multi_compile _ _MAIN_LIGHT_SHADOWS _MAIN_LIGHT_SHADOWS_CASCADE
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            #include "DeepWater.hlsl"
            TEXTURE2D(_BaseMap); SAMPLER(sampler_BaseMap);
            TEXTURE2D(_BumpMap); SAMPLER(sampler_BumpMap);
            TEXTURE2D(_MRMap); SAMPLER(sampler_MRMap);
            CBUFFER_START(UnityPerMaterial)
                float4 _BaseMap_ST; float4 _BaseColor; float _Metallic, _Roughness, _NormalScale, _HasBump, _UseVC, _Interior, _Emit;
            CBUFFER_END
            float _DeepPower;
            struct A { float4 pos : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; float4 col : COLOR; UNITY_VERTEX_INPUT_INSTANCE_ID };
            struct V { float4 cs : SV_POSITION; float3 ws : TEXCOORD0; float3 n : TEXCOORD1; float2 uv : TEXCOORD2; float ao : TEXCOORD3; float fog : TEXCOORD4; };
            V vert(A i)
            {
                UNITY_SETUP_INSTANCE_ID(i);
                V o;
                o.ws = TransformObjectToWorld(i.pos.xyz);
                o.cs = TransformWorldToHClip(o.ws);
                o.n = TransformObjectToWorldNormal(i.n);
                o.uv = TRANSFORM_TEX(i.uv, _BaseMap);
                o.ao = _UseVC > 0.5 ? saturate(dot(i.col.rgb, float3(0.333, 0.334, 0.333))) : 1;
                o.fog = ComputeFogFactor(o.cs.z);
                return o;
            }
            float4 frag(V i, bool front : SV_IsFrontFace) : SV_Target
            {
                float3 n = normalize(i.n) * (front ? 1 : -1);
                if (_HasBump > 0.5)
                {
                    float3 tn = UnpackNormalScale(SAMPLE_TEXTURE2D(_BumpMap, sampler_BumpMap, i.uv), _NormalScale);
                    float3 dp1 = ddx(i.ws), dp2 = ddy(i.ws);
                    float2 du1 = ddx(i.uv), du2 = ddy(i.uv);
                    float3 p2 = cross(dp2, n), p1 = cross(n, dp1);
                    float3 t = p2 * du1.x + p1 * du2.x;
                    float3 b = p2 * du1.y + p1 * du2.y;
                    float im = rsqrt(max(max(dot(t, t), dot(b, b)), 1e-12));
                    n = normalize(t * im * tn.x + b * im * tn.y + n * tn.z);
                }
                float4 base = SAMPLE_TEXTURE2D(_BaseMap, sampler_BaseMap, i.uv) * _BaseColor;
                float4 mr = SAMPLE_TEXTURE2D(_MRMap, sampler_MRMap, i.uv);
                float rough = saturate(mr.g * _Roughness), metal = saturate(mr.b * _Metallic);
                float gloss = (1 - rough) * (1 - rough) * lerp(0.5, 1.6, metal);
                float3 albedo = base.rgb * lerp(1, 0.8, metal);
                float3 c = _Interior > 0.5 ? DeepLightInside(i.ws, n, albedo, gloss, i.ao) : DeepLight(i.ws, n, albedo, 0, gloss, i.ao);
                c += base.rgb * _Emit * _DeepPower * 3.0;
                c = MixFog(c, i.fog);
                return float4(c, 1);
            }
            ENDHLSL
        }
        Pass
        {
            Name "ShadowCaster"
            Tags { "LightMode" = "ShadowCaster" }
            Cull [_Cull]
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma multi_compile_instancing
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Shadows.hlsl"
            float3 _LightDirection;
            struct A { float4 pos : POSITION; float3 n : NORMAL; UNITY_VERTEX_INPUT_INSTANCE_ID };
            float4 vert(A i) : SV_POSITION
            {
                UNITY_SETUP_INSTANCE_ID(i);
                float3 ws = TransformObjectToWorld(i.pos.xyz);
                return TransformWorldToHClip(ApplyShadowBias(ws, TransformObjectToWorldNormal(i.n), _LightDirection));
            }
            float4 frag() : SV_Target { return 0; }
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
            #pragma multi_compile_instancing
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            struct A { float4 pos : POSITION; UNITY_VERTEX_INPUT_INSTANCE_ID };
            float4 vert(A i) : SV_POSITION { UNITY_SETUP_INSTANCE_ID(i); return TransformObjectToHClip(i.pos.xyz); }
            float4 frag() : SV_Target { return 0; }
            ENDHLSL
        }
    }
}
