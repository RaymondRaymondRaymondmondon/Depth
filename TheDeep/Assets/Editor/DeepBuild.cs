// The Deep's headless project setup and build (no Editor window is ever opened):
//   Unity.exe -batchmode -quit -projectPath TheDeep -executeMethod DeepBuild.Configure   (URP, player settings, the boot scene)
//   Unity.exe -batchmode -quit -projectPath TheDeep -executeMethod DeepBuild.BuildWindows (configure, then ../games/TheDeep/TheDeep.exe)
// Everything else in the game is built at runtime from code and data, so the one scene holds a single boot object.
using System.IO;
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;

public static class DeepBuild
{
    const string SettingsDir = "Assets/Deep/Settings";
    const string ScenePath = "Assets/Deep/Main.unity";

    public static void Configure()
    {
        Directory.CreateDirectory(SettingsDir);
        // URP: one renderer, tuned for an integrated GPU (no MSAA, soft shadows off, short shadow distance; the water's
        // fog hides the distance anyway)
        var asset = AssetDatabase.LoadAssetAtPath<UniversalRenderPipelineAsset>(SettingsDir + "/DeepURP.asset");
        if (asset == null)
        {
            var renderer = ScriptableObject.CreateInstance<UniversalRendererData>();
            AssetDatabase.CreateAsset(renderer, SettingsDir + "/DeepRenderer.asset");
            asset = UniversalRenderPipelineAsset.Create(renderer);
            AssetDatabase.CreateAsset(asset, SettingsDir + "/DeepURP.asset");
        }
        asset.msaaSampleCount = 1;
        asset.renderScale = 1f;
        asset.shadowDistance = 60f;
        asset.shadowCascadeCount = 1;
        asset.supportsHDR = true;
        asset.supportsCameraDepthTexture = true;
        asset.supportsCameraOpaqueTexture = false;
        var so = new SerializedObject(asset);
        SetProp(so, "m_SoftShadowsSupported", false);
        SetProp(so, "m_MainLightShadowmapResolution", 1024);
        SetProp(so, "m_AdditionalLightsRenderingMode", 1);   // per vertex: the dive lamps are cheap
        SetProp(so, "m_SupportsLightCookies", true);          // (the caustics are a cookie on the sun)
        so.ApplyModifiedPropertiesWithoutUndo();
        EditorUtility.SetDirty(asset);
        GraphicsSettings.defaultRenderPipeline = asset;
        for (int i = 0; i < QualitySettings.names.Length; i++) { QualitySettings.SetQualityLevel(i, false); QualitySettings.renderPipeline = asset; }

        // keep every fog mode in builds (the scene is built at runtime, so nothing at build time uses fog)
        var gs = new SerializedObject(AssetDatabase.LoadAllAssetsAtPath("ProjectSettings/GraphicsSettings.asset")[0]);
        SetProp(gs, "m_FogStripping", 1); SetProp(gs, "m_FogKeepLinear", true); SetProp(gs, "m_FogKeepExp", true); SetProp(gs, "m_FogKeepExp2", true);
        // and every instancing variant: the seabed and the plants are drawn instanced, and nothing in the (empty) boot
        // scene uses instancing at build time, so "strip unused" removed them and nothing was drawn
        SetProp(gs, "m_InstancingStripping", 2);
        gs.ApplyModifiedPropertiesWithoutUndo();

        PlayerSettings.companyName = "Depth";
        PlayerSettings.productName = "The Deep";
        PlayerSettings.colorSpace = ColorSpace.Linear;
        PlayerSettings.fullScreenMode = FullScreenMode.Windowed;
        PlayerSettings.defaultScreenWidth = 1280;
        PlayerSettings.defaultScreenHeight = 720;
        PlayerSettings.resizableWindow = true;
        PlayerSettings.runInBackground = true;   // (a host must keep simulating when its window isn't focused)
        PlayerSettings.usePlayerLog = true;
        PlayerSettings.SetScriptingBackend(UnityEditor.Build.NamedBuildTarget.Standalone, ScriptingImplementation.Mono2x);

        // materials whose shaders must ship in the build (shaders found only at runtime would be stripped)
        Directory.CreateDirectory("Assets/Deep/Resources");
        MakeMat("Assets/Deep/Resources/DeepTerrain.mat", "Universal Render Pipeline/Terrain/Lit", null);
        MakeMat("Assets/Deep/Resources/DeepSky.mat", "Skybox/Procedural", m => { m.SetFloat("_SunSize", 0.03f); m.SetFloat("_AtmosphereThickness", 1.1f); m.SetColor("_GroundColor", new Color(0.12f, 0.2f, 0.25f)); });

        // the boot scene: one object with DeepBoot; the world is built at runtime
        if (!File.Exists(ScenePath))
        {
            var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            var boot = new GameObject("DeepBoot");
            boot.AddComponent(System.Type.GetType("Deep.DeepBoot, Deep"));
            EditorSceneManager.SaveScene(scene, ScenePath);
        }
        EditorBuildSettings.scenes = new[] { new EditorBuildSettingsScene(ScenePath, true) };
        AssetDatabase.SaveAssets();
        Debug.Log("DEEP CONFIGURE: ok");
    }

    public static void BuildWindows()
    {
        Configure();
        string outDir = Path.GetFullPath(Path.Combine(Application.dataPath, "../../games/TheDeep"));
        Directory.CreateDirectory(outDir);
        var r = BuildPipeline.BuildPlayer(new BuildPlayerOptions
        {
            scenes = new[] { ScenePath },
            locationPathName = Path.Combine(outDir, "TheDeep.exe"),
            target = BuildTarget.StandaloneWindows64,
            options = BuildOptions.None,
        });
        Debug.Log("DEEP BUILD: " + r.summary.result + " " + r.summary.totalSize / (1024 * 1024) + " MB, " + r.summary.totalErrors + " errors -> " + outDir);
        if (r.summary.result != BuildResult.Succeeded) EditorApplication.Exit(1);
    }

    static void MakeMat(string path, string shader, System.Action<Material> init)
    {
        if (AssetDatabase.LoadAssetAtPath<Material>(path) != null) return;
        var sh = Shader.Find(shader);
        if (sh == null) { Debug.LogError("DEEP: shader not found: " + shader); return; }
        var m = new Material(sh);
        init?.Invoke(m);
        AssetDatabase.CreateAsset(m, path);
    }

    static void SetProp(SerializedObject so, string name, bool v) { var p = so.FindProperty(name); if (p != null) p.boolValue = v; else Debug.LogWarning("DEEP: no property " + name); }
    static void SetProp(SerializedObject so, string name, int v) { var p = so.FindProperty(name); if (p != null) p.intValue = v; else Debug.LogWarning("DEEP: no property " + name); }
}
