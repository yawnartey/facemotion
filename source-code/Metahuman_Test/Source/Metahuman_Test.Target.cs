// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;
using System.Collections.Generic;

public class Metahuman_TestTarget : TargetRules
{
	public Metahuman_TestTarget(TargetInfo Target) : base(Target)
	{
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V6;
       // bOverrideBuildEnvironment = true; 

        ExtraModuleNames.AddRange( new string[] { "Metahuman_Test" } );
	}
}
