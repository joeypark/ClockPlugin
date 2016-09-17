// Copyright 2016 Moai Games, Inc. All Rights Reserved.

#include "VerletClothComponentPluginPrivatePCH.h"




class FVerletClothComponentPlugin : public IModuleInterface
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE(FVerletClothComponentPlugin, VerletClothComponent )



void FVerletClothComponentPlugin::StartupModule()
{
	
}


void FVerletClothComponentPlugin::ShutdownModule()
{
	
}



