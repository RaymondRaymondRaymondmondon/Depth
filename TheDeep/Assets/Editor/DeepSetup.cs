// One-off project setup run headless: Unity.exe -batchmode -quit -projectPath TheDeep -executeMethod DeepSetup.AddPackages
using UnityEditor;
using UnityEditor.PackageManager;
using UnityEngine;

public static class DeepSetup
{
    public static void AddPackages()
    {
        var req = Client.AddAndRemove(new[] {
            "com.unity.render-pipelines.universal",
            "com.unity.netcode.gameobjects",
            "com.unity.transport",
            "com.unity.test-framework",
            "com.unity.cloud.gltfast",
        }, null);
        while (!req.IsCompleted) System.Threading.Thread.Sleep(200);
        if (req.Status == StatusCode.Success)
            foreach (var p in req.Result) Debug.Log("DEEP PKG " + p.name + " " + p.version);
        else Debug.LogError("DEEP PKG FAILED " + req.Error?.message);
    }
}
