// A soft, alpha-blended puff for particles (stage 10: the black smokers' mineral plumes, geysers' steam and silt): a
// round falloff from the quad's middle, tinted by the particle's colour, dimmed by the fog like everything else.
// (Snow.shader is additive, which can't draw a dark cloud.)
Shader "Deep/Smoke"
{
    Properties
    {
        _Color ("Colour", Color) = (1, 1, 1, 1)
    }
    SubShader
    {
        Tags { "RenderType" = "Transparent" "Queue" = "Transparent" "RenderPipeline" = "UniversalPipeline" }
        Pass
        {
            Blend SrcAlpha OneMinusSrcAlpha
            ZWrite Off
            Cull Off
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma multi_compile_fog
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            CBUFFER_START(UnityPerMaterial)
                float4 _Color;
            CBUFFER_END
            struct A { float4 pos : POSITION; float4 col : COLOR; float2 uv : TEXCOORD0; };
            struct V { float4 cs : SV_POSITION; float4 col : COLOR; float2 uv : TEXCOORD0; float fog : TEXCOORD1; };
            V vert(A i)
            {
                V o; o.cs = TransformObjectToHClip(i.pos.xyz); o.col = i.col * _Color; o.uv = i.uv; o.fog = ComputeFogFactor(o.cs.z);
                return o;
            }
            float4 frag(V i) : SV_Target
            {
                float d = length(i.uv - 0.5) * 2;
                float a = saturate(1 - d); a = a * a * (3 - 2 * a);
                float3 c = MixFog(i.col.rgb, i.fog);
                return float4(c, a * i.col.a);
            }
            ENDHLSL
        }
    }
}
