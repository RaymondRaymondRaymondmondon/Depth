// The Nautilus's window glass: thick, faintly green, mostly clear head on and mirror-like at a glancing angle, with
// the sun's and the lamps' glints. Drawn after the opaque world, both sides.
Shader "Deep/Glass"
{
    Properties
    {
        _Tint ("Tint", Color) = (0.55, 0.7, 0.65, 1)
        _Clear ("Clarity head on", Float) = 0.88
    }
    SubShader
    {
        Tags { "RenderType" = "Transparent" "Queue" = "Transparent" "RenderPipeline" = "UniversalPipeline" }
        Pass
        {
            Name "Glass"
            Tags { "LightMode" = "UniversalForward" }
            Blend SrcAlpha OneMinusSrcAlpha
            ZWrite Off
            Cull Off
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma multi_compile_fog
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            #include "DeepWater.hlsl"
            CBUFFER_START(UnityPerMaterial)
                float4 _Tint; float _Clear;
            CBUFFER_END
            struct A { float4 pos : POSITION; float3 n : NORMAL; };
            struct V { float4 cs : SV_POSITION; float3 ws : TEXCOORD0; float3 n : TEXCOORD1; float fog : TEXCOORD2; };
            V vert(A i)
            {
                V o; o.ws = TransformObjectToWorld(i.pos.xyz); o.cs = TransformWorldToHClip(o.ws);
                o.n = TransformObjectToWorldNormal(i.n); o.fog = ComputeFogFactor(o.cs.z); return o;
            }
            float4 frag(V i, bool front : SV_IsFrontFace) : SV_Target
            {
                float3 n = normalize(i.n) * (front ? 1 : -1);
                float3 v = normalize(GetCameraPositionWS() - i.ws);
                float fres = pow(1 - saturate(dot(n, v)), 4);
                Light L = GetMainLight();
                float3 trans = DeepTransmit(-i.ws.y);
                float sp = pow(saturate(dot(reflect(-L.direction, n), v)), 120) * 4;
                float3 refl = lerp(_DeepAmbDeep.rgb, _DeepAmbTop.rgb, 0.6) * 1.5;
                float3 c = _Tint.rgb * 0.08 + refl * fres + L.color * trans * sp + DeepLamps(i.ws, n, 1.5) * 0.15;
                float a = saturate(1 - _Clear + fres * 0.8 + sp);
                c = MixFog(c, i.fog);
                return float4(c, a);
            }
            ENDHLSL
        }
    }
}
