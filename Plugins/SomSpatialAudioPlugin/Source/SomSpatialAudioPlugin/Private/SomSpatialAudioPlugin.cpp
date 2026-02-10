// Copyright Epic Games, Inc. All Rights Reserved.

#include "SomSpatialAudioPlugin.h"

#include "MetasoundFrontendModuleRegistrationMacros.h"

#define LOCTEXT_NAMESPACE "FSomSpatialAudioPluginModule"

void FSomSpatialAudioPluginModule::StartupModule()
{
	METASOUND_REGISTER_ITEMS_IN_MODULE
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FSomSpatialAudioPluginModule::ShutdownModule()
{
	METASOUND_UNREGISTER_ITEMS_IN_MODULE
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSomSpatialAudioPluginModule, SomSpatialAudioPlugin)