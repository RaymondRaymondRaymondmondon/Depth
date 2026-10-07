// The sea surface mesh: a grid that follows the camera (snapped so the waves don't swim), dense near the eye and
// stretching to the horizon. The waves themselves are moved in the shader; this uploads their parameters.
using UnityEngine;

namespace Deep
{
    public class OceanSurface : MonoBehaviour
    {
        const int N = 160;            // vertices per side
        const float Reach = 900f;     // metres from the eye to the grid's edge
        Mesh mesh; Material mat;

        void Start()
        {
            mat = new Material(Resources.Load<Shader>("Shaders/OceanSurface"));
            mesh = new Mesh { name = "OceanGrid", indexFormat = UnityEngine.Rendering.IndexFormat.UInt32 };
            var v = new Vector3[N * N];
            for (int j = 0; j < N; j++)
                for (int i = 0; i < N; i++)
                {
                    float u = i / (N - 1f) * 2 - 1, w = j / (N - 1f) * 2 - 1;
                    // squash the spacing toward the centre: about 1 m apart near the eye, tens of metres at the edge
                    float x = Mathf.Sign(u) * Mathf.Pow(Mathf.Abs(u), 2.2f) * Reach, z = Mathf.Sign(w) * Mathf.Pow(Mathf.Abs(w), 2.2f) * Reach;
                    v[j * N + i] = new Vector3(x, 0, z);
                }
            var t = new int[(N - 1) * (N - 1) * 6]; int k = 0;
            for (int j = 0; j < N - 1; j++)
                for (int i = 0; i < N - 1; i++)
                {
                    int a = j * N + i;
                    t[k++] = a; t[k++] = a + N; t[k++] = a + 1;
                    t[k++] = a + 1; t[k++] = a + N; t[k++] = a + N + 1;
                }
            mesh.vertices = v; mesh.triangles = t;
            mesh.bounds = new Bounds(Vector3.zero, new Vector3(Reach * 2, 40, Reach * 2));
            gameObject.AddComponent<MeshFilter>().sharedMesh = mesh;
            var mr = gameObject.AddComponent<MeshRenderer>();
            mr.sharedMaterial = mat;
            mr.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            mr.receiveShadows = false;
        }

        void LateUpdate()
        {
            Waves.Upload(Time.time);
            GetComponent<MeshRenderer>().enabled = !Shots.HideOcean;
            var cam = Camera.main; if (!cam) return;
            var p = cam.transform.position;
            transform.position = new Vector3(Mathf.Round(p.x / 8f) * 8f, 0, Mathf.Round(p.z / 8f) * 8f);
        }
    }
}
