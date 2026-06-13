#pragma once

#include "Modules/ModuleManager.h"


JMARKOVJUNIOR_API   DECLARE_LOG_CATEGORY_EXTERN(LogJMarkovJunior, Log, All);

class FJMarkovJuniorModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:

	IConsoleObject* cmdSuppressStderr;
};