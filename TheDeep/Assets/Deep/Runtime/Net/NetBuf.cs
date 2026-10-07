// The byte buffers every message is written into and read out of (little-endian, compact): a writer that grows, and a
// reader over a received payload. Messages are one type byte followed by the fields (Net.cs).
using System;
using System.Collections.Generic;
using System.Text;
using UnityEngine;

namespace Deep
{
    public class NetW
    {
        public byte[] b = new byte[256];
        public int n;
        void Room(int k) { if (n + k > b.Length) Array.Resize(ref b, Math.Max(b.Length * 2, n + k)); }
        public NetW U8(int v) { Room(1); b[n++] = (byte)v; return this; }
        public NetW Bool(bool v) => U8(v ? 1 : 0);
        public NetW I16(int v) { Room(2); b[n++] = (byte)v; b[n++] = (byte)(v >> 8); return this; }
        public NetW I32(int v) { Room(4); for (int i = 0; i < 4; i++) b[n++] = (byte)(v >> (8 * i)); return this; }
        public NetW L64(long v) { Room(8); for (int i = 0; i < 8; i++) b[n++] = (byte)(v >> (8 * i)); return this; }
        public NetW F(float v) => I32(BitConverter.SingleToInt32Bits(v));
        public NetW V3(Vector3 v) => F(v.x).F(v.y).F(v.z);
        public NetW Q(Quaternion q) => F(q.x).F(q.y).F(q.z).F(q.w);
        // a small vector to a hundredth (velocities: +-327 m/s)
        public NetW V3s(Vector3 v) => I16(Mathf.Clamp(Mathf.RoundToInt(v.x * 100), -32767, 32767)).I16(Mathf.Clamp(Mathf.RoundToInt(v.y * 100), -32767, 32767)).I16(Mathf.Clamp(Mathf.RoundToInt(v.z * 100), -32767, 32767));
        public NetW S(string s)
        {
            var bytes = Encoding.UTF8.GetBytes(s ?? "");
            int k = Math.Min(bytes.Length, 65535); I16(k); Room(k); Array.Copy(bytes, 0, b, n, k); n += k; return this;
        }
        public byte[] ToArray() { var a = new byte[n]; Array.Copy(b, a, n); return a; }
    }

    public class NetR
    {
        readonly byte[] b; int p;
        public NetR(byte[] data) { b = data; }
        public bool More => p < b.Length;
        public int U8() => p < b.Length ? b[p++] : 0;
        public bool Bool() => U8() != 0;
        public int I16() { if (p + 2 > b.Length) { p = b.Length; return 0; } int v = (short)(b[p] | b[p + 1] << 8); p += 2; return v; }
        public int I32() { if (p + 4 > b.Length) { p = b.Length; return 0; } int v = b[p] | b[p + 1] << 8 | b[p + 2] << 16 | b[p + 3] << 24; p += 4; return v; }
        public long L64() { long lo = (uint)I32(), hi = (uint)I32(); return lo | hi << 32; }
        public float F() => BitConverter.Int32BitsToSingle(I32());
        public Vector3 V3() => new Vector3(F(), F(), F());
        public Quaternion Q() => new Quaternion(F(), F(), F(), F());
        public Vector3 V3s() => new Vector3(I16() / 100f, I16() / 100f, I16() / 100f);
        public string S()
        {
            int k = I16() & 0xffff; if (p + k > b.Length) k = b.Length - p;
            var s = Encoding.UTF8.GetString(b, p, k); p += k; return s;
        }
    }

    // a stream of timed samples (a remote diver, the ship, the raft): drawn about a tenth of a second behind the
    // newest, between the two samples either side of that moment, so a late or lost packet doesn't jerk it
    public class Interp<T> where T : struct
    {
        readonly List<(float t, T v)> buf = new List<(float, T)>();
        public bool Any => buf.Count > 0;
        public T Latest => buf.Count > 0 ? buf[buf.Count - 1].v : default;
        public float LatestT => buf.Count > 0 ? buf[buf.Count - 1].t : -1;
        public void Add(float t, T v)
        {
            if (buf.Count > 0 && t <= buf[buf.Count - 1].t) return;   // old news
            buf.Add((t, v));
            while (buf.Count > 24) buf.RemoveAt(0);
        }
        // the value at time t: between samples a and b at k (0..1), or the newest (k = 1 on the newest)
        public bool At(float t, out T a, out T b, out float k)
        {
            a = b = default; k = 0;
            if (buf.Count == 0) return false;
            if (t <= buf[0].t) { a = b = buf[0].v; return true; }
            for (int i = buf.Count - 1; i > 0; i--)
                if (buf[i - 1].t <= t)
                {
                    if (t >= buf[i].t) { a = b = buf[i].v; k = 1; return true; }
                    a = buf[i - 1].v; b = buf[i].v; k = (t - buf[i - 1].t) / Mathf.Max(1e-4f, buf[i].t - buf[i - 1].t);
                    return true;
                }
            a = b = buf[buf.Count - 1].v; k = 1; return true;
        }
        public void Clear() => buf.Clear();
    }
}
