// Marine snow: a soft round speck, additive, tinted by the particle colour and the water's light.
Shader "Deep/Snow"
{
    SubShader
    {
        Tags { "RenderType" = "Transparent" "Queue" = "Transparent+10" "RenderPipeline" = "UniversalPipeline" }
        Pass
        {
            Blend SrcAlpha One
            ZWrite Off
            Cull Off
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            float4 _DeepWaterColor;
            struct A { float4 pos : POSITION; float4 col : COLOR; float2 uv : TEXCOORD0; };
            struct V { float4 cs : SV_POSITION; float4 col : COLOR; float2 uv : TEXCOORD0; float d : TEXCOORD1; };
            V vert(A i)
            {
                V o; float3 ws = TransformObjectToWorld(i.pos.xyz); o.cs = TransformWorldToHClip(ws); o.col = i.col; o.uv = i.uv;
                o.d = length(GetCameraPositionWS() - ws);
                return o;
            }
            float4 frag(V i) : SV_Target
            {
                float r = length(i.uv - 0.5) * 2;
                float a = saturate(1 - r * r) * i.col.a * (1 - smoothstep(6, 13, i.d)) * 0.55;
                return float4(i.col.rgb * (0.35 + _DeepWaterColor.rgb * 2.5), a);
            }
            ENDHLSL
        }
    }
}
