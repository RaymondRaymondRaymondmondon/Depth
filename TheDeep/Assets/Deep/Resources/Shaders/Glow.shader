// A light seen from afar (the derelict's dying emergency lamps across the night sea): an additive disc that always
// faces the camera, soft at the edge, its size in metres; fog dims it but less than solid things (light carries).
Shader "Deep/Glow"
{
    Properties
    {
        _Color ("Colour", Color) = (1, 0.15, 0.08, 1)
        _Intensity ("Intensity", Float) = 3
        _Size ("Size (m)", Float) = 1.5
    }
    SubShader
    {
        Tags { "RenderType" = "Transparent" "Queue" = "Transparent+10" "RenderPipeline" = "UniversalPipeline" }
        Pass
        {
            Blend One One
            ZWrite Off
            ZTest LEqual
            Cull Off
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma multi_compile_fog
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            CBUFFER_START(UnityPerMaterial)
                float4 _Color; float _Intensity, _Size;
            CBUFFER_END
            struct A { float4 pos : POSITION; float2 uv : TEXCOORD0; };
            struct V { float4 cs : SV_POSITION; float2 uv : TEXCOORD0; float fog : TEXCOORD1; };
            V vert(A i)
            {
                V o;
                float3 c = TransformObjectToWorld(float3(0, 0, 0));
                float3 right = UNITY_MATRIX_V[0].xyz, up = UNITY_MATRIX_V[1].xyz;
                float size = max(_Size, length(c - _WorldSpaceCameraPos) * 0.07);   // never smaller than about four degrees (the core reads as a lamp, the rest as its halo)
                float3 ws = c + (right * (i.uv.x - 0.5) + up * (i.uv.y - 0.5)) * size;
                o.cs = TransformWorldToHClip(ws); o.uv = i.uv;
                o.fog = ComputeFogFactor(o.cs.z);
                return o;
            }
            float4 frag(V i) : SV_Target
            {
                float d = length(i.uv - 0.5) * 2;
                float a = saturate(1 - d); a = a * a * (0.4 + 0.6 * a);
                float fogKeep = saturate(sqrt(saturate(ComputeFogIntensity(i.fog))) * 1.2);   // light carries further than shapes
                return float4(_Color.rgb * _Intensity * a * fogKeep, 1);
            }
            ENDHLSL
        }
    }
}
