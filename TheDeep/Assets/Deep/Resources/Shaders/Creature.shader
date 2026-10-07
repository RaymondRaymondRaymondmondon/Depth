// The sea's animals (CreatureMeshes.cs bodies, drawn instanced by Life.cs): the swim is done here, from UV0
// (u = flap weight, v = tailness) by mode: 0 a fish's tail sweep, 1 wings or flippers flapping, 2 an eel's
// undulation, 3 a mantle or bell pulsing, 4 legs scuttling, 5 still, 6 a fluke beating up and down. The beat's phase
// differs per animal (a hash of where it is). Lit by DeepWater.hlsl like the rest of the sea; fins (vertex alpha)
// let the light through.
Shader "Deep/Creature"
{
    Properties
    {
        _Mode ("Swim mode", Float) = 0
        _Freq ("Beats per second (x 2 pi)", Float) = 6
        _Amp ("Amplitude", Float) = 0.08
        _Gloss ("Gloss", Float) = 0.35
        _Glow ("Bioluminescence", Float) = 0
    }
    SubShader
    {
        Tags { "RenderType" = "Opaque" "RenderPipeline" = "UniversalPipeline" }
        HLSLINCLUDE
        #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
        #include "DeepWater.hlsl"
        CBUFFER_START(UnityPerMaterial)
            float _Mode, _Freq, _Amp, _Gloss, _Glow;
        CBUFFER_END
        float3 Swim(float3 p, float2 uv)
        {
            float3 root = TransformObjectToWorld(float3(0, 0, 0));
            float ph = _DeepTime * _Freq + frac(sin(dot(root.xz, float2(12.9898, 78.233))) * 43758.5453) * 6.2831;
            float u = uv.x, v = uv.y;
            int m = (int)(_Mode + 0.5);
            if (m == 0) p.x += sin(ph - p.z * 6.0) * _Amp * v * v;
            else if (m == 1) { p.y += sin(ph) * _Amp * u * u; p.y += sin(ph - 1.2) * _Amp * 0.15 * v; }
            else if (m == 2) p.x += sin(ph - p.z * 14.0) * _Amp * (0.35 + v);
            else if (m == 3) p.xy *= 1.0 + sin(ph) * _Amp * u;
            else if (m == 4) p.y += max(0, sin(ph * 2.0 + p.z * 20.0 + sign(p.x) * 3.0)) * _Amp * u;
            else if (m == 6) p.y += sin(ph - p.z * 5.0) * _Amp * v * v;
            return p;
        }
        ENDHLSL
        Pass
        {
            Name "Creature"
            Tags { "LightMode" = "UniversalForward" }
            Cull Off
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma multi_compile_instancing
            #pragma multi_compile_fog
            #pragma multi_compile _ _MAIN_LIGHT_SHADOWS _MAIN_LIGHT_SHADOWS_CASCADE
            struct A { float4 pos : POSITION; float3 n : NORMAL; float4 col : COLOR; float2 uv : TEXCOORD0; UNITY_VERTEX_INPUT_INSTANCE_ID };
            struct V { float4 cs : SV_POSITION; float3 ws : TEXCOORD0; float3 n : TEXCOORD1; float4 col : TEXCOORD2; float fog : TEXCOORD3; };
            V vert(A i)
            {
                UNITY_SETUP_INSTANCE_ID(i);
                V o;
                float3 p = Swim(i.pos.xyz, i.uv);
                o.ws = TransformObjectToWorld(p); o.cs = TransformWorldToHClip(o.ws);
                o.n = TransformObjectToWorldNormal(i.n); o.col = i.col; o.fog = ComputeFogFactor(o.cs.z);
                return o;
            }
            float4 frag(V i, bool front : SV_IsFrontFace) : SV_Target
            {
                float3 n = normalize(i.n) * (front ? 1 : -1);
                float3 c = DeepLight(i.ws, n, i.col.rgb, i.col.a * 0.6, _Gloss, 1);
                c += i.col.rgb * _Glow;
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
            float3 _LightDirection;
            struct A { float4 pos : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; UNITY_VERTEX_INPUT_INSTANCE_ID };
            float4 vert(A i) : SV_POSITION
            {
                UNITY_SETUP_INSTANCE_ID(i);
                float3 ws = TransformObjectToWorld(Swim(i.pos.xyz, i.uv));
                return TransformWorldToHClip(ApplyShadowBias(ws, TransformObjectToWorldNormal(i.n), _LightDirection));
            }
            float4 frag() : SV_Target { return 0; }
            ENDHLSL
        }
    }
}
