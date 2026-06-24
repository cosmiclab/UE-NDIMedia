// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class NDIMedia : ModuleRules
{
    private string ModulePath
    {
        get { return ModuleDirectory; }
    }

    private string ThirdPartyPath
    {
        get { return Path.GetFullPath(Path.Combine(ModulePath, "../ThirdParty/")); }
    }

    public string GetPluginPath()
    {
        return Path.Combine(ModuleDirectory, "../../");
    }

    private string CopyToProjectBinaries(string Filepath, ReadOnlyTargetRules Target)
    {
        string BinariesDir = Path.Combine(GetPluginPath(), "Binaries", Target.Platform.ToString());
        string Filename = Path.GetFileName(Filepath);

        //convert relative path 
        string FullBinariesDir = Path.GetFullPath(BinariesDir);

        if (!Directory.Exists(FullBinariesDir))
        {
            Directory.CreateDirectory(FullBinariesDir);
        }

        string FullExistingPath = Path.Combine(FullBinariesDir, Filename);
        bool ValidFile = false;

        //File exists, check if they're the same
        if (File.Exists(FullExistingPath))
        {
            ValidFile = true;
        }

        //No valid existing file found, copy new dll
        if (!ValidFile)
        {
            File.Copy(Filepath, Path.Combine(FullBinariesDir, Filename), true);
        }

        return FullExistingPath;
    }

    public NDIMedia(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        string ndi_sdk_path = Path.GetFullPath(Path.GetFullPath(Path.Combine(ThirdPartyPath, "NDI")));
        string ndi_lib_path = Path.GetFullPath(Path.Combine(ndi_sdk_path, "Libraries/Win64"));

        PublicIncludePaths.AddRange(
            new string[] {
                Path.Combine(ndi_sdk_path, "Include"),
				// ... add public include paths required here ...
			}
            );

        PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"MediaAssets",
				"MediaIOCore"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"CoreUObject",
				"DeveloperSettings",
				"Engine",
				"MediaIOCore",
				"MediaUtils",
				"NDIMediaRendering",
				"NDISDK",
				"Projects",
				"RenderCore",
				"RHI",
				"Slate",
				"SlateCore",
				"TimeManagement",
			});

        PublicAdditionalLibraries.AddRange(
			new string[]
			{
				Path.Combine(ndi_lib_path, "Processing.NDI.Lib.x64.lib"),
			});

        string ndi_dll_path = Path.Combine(ThirdPartyPath, "NDI", "Libraries", "Win64", "Processing.NDI.Lib.x64.dll");
        CopyToProjectBinaries(ndi_dll_path, Target);

        PublicDelayLoadDLLs.Add(ndi_dll_path);
        RuntimeDependencies.Add("$(TargetOutputDir)/Processing.NDI.Lib.x64.dll", Path.Combine(GetPluginPath(), "Binaries", "Win64", "Processing.NDI.Lib.x64.dll"));
        RuntimeDependencies.Add("$(TargetOutputDir)/UnrealEditor-NDIMedia.dll",		  Path.Combine(GetPluginPath(), "Binaries", "Win64", "UnrealEditor-NDIMedia.dll"));
        RuntimeDependencies.Add("$(TargetOutputDir)/UnrealEditor-NDIMediaEditor.dll", Path.Combine(GetPluginPath(), "Binaries", "Win64", "UnrealEditor-NDIMediaEditor.dll"));

        if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"UnrealEd",
			});
		}
	}
}
