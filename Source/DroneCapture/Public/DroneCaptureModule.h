#pragma once

#include "Modules/ModuleManager.h"

class FDroneCaptureModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};
