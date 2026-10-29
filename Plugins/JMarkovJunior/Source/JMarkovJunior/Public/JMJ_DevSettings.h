#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "JMJ_DevSettings.generated.h"


UCLASS(Config=Engine, DefaultConfig, meta=(DisplayName="JMarkovJunior"))
class JMARKOVJUNIOR_API UJmjDevSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    
    //How long to wait for the IPC process to start accepting clients, before giving up and crashing.
    //Not including some extra time due to this being done lazily.
    //
    //Values <=0 will wait forever.
    UPROPERTY(Config, EditAnywhere, Category="IPC", meta=(Units="s"))
    float StartupTimeoutSeconds = 20.0f;

    //If true, then when running this engine as a commandlet the IPC's named pipe is randomized.
    //This is because commandlets usually are for small utilities and multiple could be running at once.
    UPROPERTY(Config, EditAnywhere, Category="IPC")
    bool RandomizeNamedPipeForCommandlets = true;
};