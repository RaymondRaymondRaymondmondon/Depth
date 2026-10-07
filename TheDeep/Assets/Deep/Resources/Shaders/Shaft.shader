// A light shaft: additive, soft at the sides, bright under the surface and fading with length, rippling slowly.
Shader "Deep/Shaft"
{
    SubShader
    {
        Tags { "RenderType" = "Transparent" "Queue" = "Transparent+20" "RenderPipeline" = "UniversalPipeline" }
        Pass
        {
            Blend One One
            ZWrite Off
            Cull Off
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma multi_compile_instancing
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            UNITY_INSTANCING_BUFFER_START(Props)
                UNITY_DEFINE_INSTANCED_PROP(float, _Fade)
            UNITY_INSTANCING_BUFFER_END(Props)
            float4 _DeepSunColor; float4 _DeepWaterColor; float _DeepTime;
            struct A { float4 pos : POSITION; float2 uv : TEXCOORD0; UNITY_VERTEX_INPUT_INSTANCE_ID };
            struct V { float4 cs : SV_POSITION; float2 uv : TEXCOORD0; float3 ws : TEXCOORD1; float fade : TEXCOORD2; };
            V vert(A i)
            {
                UNITY_SETUP_INSTANCE_ID(i);
                V o; o.ws = TransformObjectToWorld(i.pos.xyz); o.cs = TransformWorldToHClip(o.ws); o.uv = i.uv;
                o.fade = UNITY_ACCESS_INSTANCED_PROP(Props, _Fade);
                return o;
            }
            float4 frag(V i) : SV_Target
            {
                float side = sin(i.uv.x * 3.14159);
                side *= side;
                float along = (1 - i.uv.y);
                along = along * along * smoothstep(0.0, 0.08, i.uv.y);
                float ripple = 0.65 + 0.35 * sin(i.uv.x * 9 + _DeepTime * 0.8 + i.ws.x * 0.2) * sin(i.uv.y * 5 - _DeepTime * 0.4);
                float dist = length(GetCameraPositionWS() - i.ws);
                float near = smoothstep(1.5, 6, dist) * (1 - smoothstep(30, 60, dist));
                float3 c = lerp(_DeepWaterColor.rgb * 3, _DeepSunColor.rgb, 0.5) * side * along * ripple * near * i.fade * 0.09;
                return float4(c, 1);
            }
            ENDHLSL
        }
    }
}
