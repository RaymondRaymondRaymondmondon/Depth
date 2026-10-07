// Kelp, sea-grass and coral: lit by the sun and the water's ambient light, coloured by vertex colour times the
// material tint, swaying in the current by how far up the plant a vertex sits (vertex colour alpha 0 at the root,
// 1 at the tip). Instanced; two-sided for blades and fronds.
Shader "Deep/Flora"
{
    Properties
    {
        _Tint ("Tint", Color) = (1,1,1,1)
        _Sway ("Sway (m)", Float) = 0.6
        _Translucency ("Translucency", Float) = 0.4
        _Gloss ("Gloss", Float) = 0.1
        _Glow ("Bioluminescence", Float) = 0
    }
    SubShader
    {
        Tags { "RenderType" = "Opaque" "RenderPipeline" = "UniversalPipeline" }
        Pass
        {
            Name "Flora"
            Tags { "LightMode" = "UniversalForward" }
            Cull Off
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma multi_compile_instancing
            #pragma multi_compile_fog
            #pragma multi_compile _ _MAIN_LIGHT_SHADOWS _MAIN_LIGHT_SHADOWS_CASCADE
            #pragma multi_compile _ _LIGHT_COOKIES
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            #include "DeepWater.hlsl"
            CBUFFER_START(UnityPerMaterial)
                float4 _Tint; float _Sway; float _Translucency; float _Gloss; float _Glow;
            CBUFFER_END
            struct A { float4 pos : POSITION; float3 n : NORMAL; float4 col : COLOR; UNITY_VERTEX_INPUT_INSTANCE_ID };
            struct V { float4 cs : SV_POSITION; float3 ws : TEXCOORD0; float3 n : TEXCOORD1; float4 col : TEXCOORD2; float fog : TEXCOORD3; };
            V vert(A i)
            {
                UNITY_SETUP_INSTANCE_ID(i);
                V o;
                float3 ws = TransformObjectToWorld(i.pos.xyz);
                float3 root = TransformObjectToWorld(float3(0, 0, 0));
                float h = i.col.a;
                float ph = root.x * 0.13 + root.z * 0.11;
                float2 sway = float2(sin(_DeepTime * 0.55 + ph + h * 1.3), cos(_DeepTime * 0.43 + ph * 1.3 + h)) * _Sway * h * h;
                sway += float2(sin(_DeepTime * 1.7 + ph * 3 + ws.y * 0.6), cos(_DeepTime * 1.4 + ph * 2)) * _Sway * 0.12 * h;
                ws.xz += sway;
                o.ws = ws; o.cs = TransformWorldToHClip(ws);
                o.n = TransformObjectToWorldNormal(i.n);
                o.col = i.col; o.fog = ComputeFogFactor(o.cs.z);
                return o;
            }
            float4 frag(V i, bool front : SV_IsFrontFace) : SV_Target
            {
                float3 n = normalize(i.n) * (front ? 1 : -1);
                float3 albedo = i.col.rgb * _Tint.rgb;
                // light through thin fronds; roots sit in their own shade
                float3 c = DeepLight(i.ws, n, albedo, _Translucency, _Gloss, 0.55 + 0.45 * i.col.a);
                c += albedo * _Glow;
                c = MixFog(c, i.fog);
                return float4(c, 1);
            }
            ENDHLSL
        }
        Pass
        {
            Name "ShadowCaster"
            Tags { "LightMode" = "ShadowCaster" }
            Cull Off
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma multi_compile_instancing
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Shadows.hlsl"
            CBUFFER_START(UnityPerMaterial)
                float4 _Tint; float _Sway; float _Translucency; float _Gloss; float _Glow;
            CBUFFER_END
            float _DeepTime; float3 _LightDirection;
            struct A { float4 pos : POSITION; float3 n : NORMAL; float4 col : COLOR; UNITY_VERTEX_INPUT_INSTANCE_ID };
            float4 vert(A i) : SV_POSITION
            {
                UNITY_SETUP_INSTANCE_ID(i);
                float3 ws = TransformObjectToWorld(i.pos.xyz);
                float3 root = TransformObjectToWorld(float3(0, 0, 0));
                float h = i.col.a; float ph = root.x * 0.13 + root.z * 0.11;
                ws.xz += float2(sin(_DeepTime * 0.55 + ph + h * 1.3), cos(_DeepTime * 0.43 + ph * 1.3 + h)) * _Sway * h * h;
                float3 n = TransformObjectToWorldNormal(i.n);
                return TransformWorldToHClip(ApplyShadowBias(ws, n, _LightDirection));
            }
            float4 frag() : SV_Target { return 0; }
            ENDHLSL
        }
    }
}
