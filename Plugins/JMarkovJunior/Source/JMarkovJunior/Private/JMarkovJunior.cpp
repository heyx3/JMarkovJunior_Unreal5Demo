#include "JMarkovJunior.h"

DEFINE_LOG_CATEGORY(LogJMarkovJunior);

#define LOCTEXT_NAMESPACE "FJMarkovJuniorModule"

void FJMarkovJuniorModule::StartupModule()
{
	//Register a console command.
	//auto* libLoader2 = &(*libLoader);
	cmdSuppressStderr = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("jmj.SuppressStderr"),
		TEXT("Usage: `jmj.SuppressStderr 1|0` to change whether JMarkovJunior can output errors to stderr. "
		       "`jmj.SuppressStderr` prints the current value."),
		FConsoleCommandWithArgsDelegate::CreateLambda([/*libLoader2*/](const TArray<FString>& args)
		{
			if (/*!libLoader2->IsValid()*/ true)
			{
				UE_LOG(LogJMarkovJunior, Warning, TEXT("Ignoring `jmj.SuppressStderr` command because the library isn't loaded"));
			}
			else if (args.Num() > 1)
			{
				UE_LOG(LogJMarkovJunior, Error, TEXT("Expected 0 or 1 arguments to `jmj.SuppressStderr`; got %i"), args.Num());
			}
			else if (args.Num() == 0)
			{
				//UE_LOG(LogJMarkovJunior, Log, TEXT("`jmj.SuppressStderr is %i"), libLoader2->fnGetSuppressStderr());
			}
			else if (args.Num() == 1)
			{
				//libLoader2->fnSetSuppressStderr(FCString::Atoi(*args[0]));
			}
		})
	);
}
void FJMarkovJuniorModule::ShutdownModule()
{
	//libLoader.Reset();

	if (cmdSuppressStderr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(cmdSuppressStderr);
		cmdSuppressStderr = nullptr;
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FJMarkovJuniorModule, JMarkovJunior)