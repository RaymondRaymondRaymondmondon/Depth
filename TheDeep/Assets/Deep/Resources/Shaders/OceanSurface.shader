// The sea surface, seen from both sides. Above: the sky's reflection by Fresnel, a sun glint, and a green-blue body
// with lighter wave crests. Below: Snell's window (the bright disc of sky straight overhead) inside the critical angle,
// and outside it the mirror of the water itself. The four Gerstner waves are the same as Waves.cs.
Shader "Deep/OceanSurface"
{
    SubShader
    {
        Tags { "RenderType" = "Opaque" "RenderPipeline" = "UniversalPipeline" "Queue" = "Geometry+10" }
        Pass
        {
            Name "Ocean"
            Tags { "LightMode" = "UniversalForward" }
            Cull Off
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma multi_compile_fog
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"

            float4 _DeepWave0, _DeepWave1, _DeepWave2, _DeepWave3;
            float _DeepTime;
            float4 _DeepSunDir;      // xyz: the light's travel direction
            float4 _DeepSunColor;    // rgb, a = daylight 0..1
            float4 _DeepSkyTop, _DeepSkyHorizon, _DeepWaterColor;

            struct A { float4 pos : POSITION; };
            struct V { float4 cs : SV_POSITION; float3 ws : TEXCOORD0; float3 n : TEXCOORD1; float crest : TEXCOORD2; float fog : TEXCOORD3; };

            void Wave(float4 w, float2 xz, inout float3 p, inout float3 tangent, inout float3 binormal, inout float crest)
            {
                float2 d = normalize(w.xy);
                float k = 6.28318 / w.z, c = sqrt(9.81 / k), a = w.w / k;
                float f = k * (dot(d, xz) - c * _DeepTime);
                float s = sin(f), co = cos(f);
                p += float3(d.x * a * co, a * s, d.y * a * co);
                tangent += float3(-d.x * d.x * w.w * s, d.x * w.w * co, -d.x * d.y * w.w * s);
                binormal += float3(-d.x * d.y * w.w * s, d.y * w.w * co, -d.y * d.y * w.w * s);
                crest += saturate(s) * w.w;
            }

            V vert(A i)
            {
                V o;
                float3 ws = TransformObjectToWorld(i.pos.xyz);
                float2 xz = ws.xz;
                float3 p = float3(xz.x, 0, xz.y), t = float3(1, 0, 0), b = float3(0, 0, 1);
                float crest = 0;
                Wave(_DeepWave0, xz, p, t, b, crest); Wave(_DeepWave1, xz, p, t, b, crest);
                Wave(_DeepWave2, xz, p, t, b, crest); Wave(_DeepWave3, xz, p, t, b, crest);
                o.ws = p;
                o.n = normalize(cross(b, t));
                o.crest = crest;
                o.cs = TransformWorldToHClip(p);
                o.fog = ComputeFogFactor(o.cs.z);
                return o;
            }

            float4 frag(V i, bool front : SV_IsFrontFace) : SV_Target
            {
                float3 n = normalize(i.n);
                float3 v = normalize(GetCameraPositionWS() - i.ws);   // toward the eye
                float3 toSun = -normalize(_DeepSunDir.xyz);
                float day = _DeepSunColor.a;
                // small ripples on top of the big waves (two scrolling sine fields)
                float2 q = i.ws.xz;
                float3 rip = float3(sin(q.x * 1.7 + _DeepTime * 1.3) * 0.5 + sin(q.y * 2.3 - _DeepTime * 1.1) * 0.5, 0,
                                    cos(q.y * 1.9 + _DeepTime * 0.9) * 0.5 + cos((q.x + q.y) * 1.3 + _DeepTime) * 0.5) * 0.06;
                n = normalize(n + rip);
                float3 col;
                if (front)
                {
                    float fres = 0.02 + 0.98 * pow(1 - saturate(dot(n, v)), 5);
                    float3 r = reflect(-v, n);
                    float3 sky = lerp(_DeepSkyHorizon.rgb, _DeepSkyTop.rgb, saturate(r.y * 1.5));
                    float3 body = lerp(float3(0.01, 0.06, 0.08), float3(0.04, 0.22, 0.24), saturate(dot(n, toSun) * 0.5 + 0.3)) * (0.25 + 0.75 * day);
                    body += float3(0.05, 0.12, 0.10) * saturate(i.crest * 2.5) * day;   // light through the crests
                    col = lerp(body, sky, fres);
                    float spec = pow(saturate(dot(r, toSun)), 600) * 30 + pow(saturate(dot(r, toSun)), 60) * 0.6;
                    col += _DeepSunColor.rgb * spec * day;
                }
                else
                {
                    n = -n;                                            // (seen from below)
                    float cosv = saturate(dot(n, v));
                    // Snell's window: rays within ~48.6 degrees of the normal reach the sky
                    float window = smoothstep(0.62, 0.70, cosv);
                    float3 sky = lerp(_DeepSkyHorizon.rgb, _DeepSkyTop.rgb, saturate((cosv - 0.66) * 3)) * 1.3;
                    float3 water = _DeepWaterColor.rgb * 1.6;
                    col = lerp(water, sky, window);
                    float3 refr = refract(-v, n, 1.33);
                    float sun = pow(saturate(dot(-refr, toSun)), 80) * 6 * day;   // the sun's blurred disc through the surface
                    col += _DeepSunColor.rgb * sun * window;
                }
                col = MixFog(col, i.fog);
                return float4(col, 1);
            }
            ENDHLSL
        }
    }
}
