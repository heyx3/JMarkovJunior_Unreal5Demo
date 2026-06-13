#pragma once

#include "CoreMinimal.h"
#include <span>

#include "JMarkovJunior.h"
#include "GenericPlatform/GenericPlatformNamedPipe.h"

#include "JMJ_ProcessManager.generated.h"


USTRUCT(BlueprintType)
struct JMARKOVJUNIOR_API FJmjCellType
{
	GENERATED_BODY()
public:

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FLinearColor Color = FLinearColor::Transparent;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FString Char;
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FString Name;
};

UCLASS(BlueprintType)
class JMARKOVJUNIOR_API UJmjConstants : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	//All possible kinds of cells, ordered by their value.
	static std::span<FJmjCellType> GetCellTypes();
	// (implementation note: the JMJ grid is deliberately 0-based even though Julia is 1-based,
	//    so these lookups work as expected!)

	//A simple maze-generator algorithm that works in any number of dimensions.
	static const FString& GetBasicMaze();

protected:
	UFUNCTION(BlueprintCallable, BlueprintPure)
	static void GetCellTypes(TArray<FJmjCellType>& output)
	{
		output.Empty();
		auto cellTypes = GetCellTypes();
		output.Append(cellTypes.data(), cellTypes.size());
	}
	UFUNCTION(BlueprintCallable, BlueprintPure)
	static void GetBasicMaze(FString& algoString)
	{
		algoString = GetBasicMaze();
	}
};

USTRUCT(BlueprintType)
struct JMARKOVJUNIOR_API FJmjParsedAlgo
{
	GENERATED_BODY()
public:

	//ID's are uint32; the int type is just to get into BP.
	//0 is null.
	UPROPERTY(BlueprintReadWrite, VisibleInstanceOnly)
	int ID = 0;
};

//Manages the JMarkovJunior process, which parses and runs algorithms for us.
//All requests must be handled on the game thread.
UCLASS(BlueprintType)
class JMARKOVJUNIOR_API UJmjProcessManager : public UEngineSubsystem, public FTickableGameObject
{
	GENERATED_BODY()
public:

	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(Keywords="time duration"))
	float ProcessPollIntervalSeconds = 3.0f;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(Keywords="time left"))
	float SecondsToNextProcessPoll = 0.0f;

	virtual void Initialize(FSubsystemCollectionBase& collection) override;
	virtual void Deinitialize() override;

	bool IsInitialized() const { return procState > ProcState::Uninitialized; }
	bool IsProcessAlive() const { return procState > ProcState::Uninitialized && procState < ProcState::Dead; }
	bool IsProcessAcceptingClients() const { return procState == ProcState::Ready; }

	//// FTickableGameObject interface
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return true; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UJmjProcessManager, STATGROUP_Tickables); }
	virtual void Tick(float deltaSeconds) override;
	///////
	
private:
	
	FProcHandle process;
	void *stdoutFromChild, *stdoutFromHere,
		 *stderrFromChild, *stderrFromHere;
	FString pipeName;
	TArray<uint8> stdoutBuffer, stderrBuffer;
	FString stderrString;
	TUniquePtr<FGenericPlatformNamedPipe> namedPipe;

	enum class ProcState
	{
		Uninitialized, //Not started yet
		Booting, //Started, but not ready for clients
		Ready, //Ready for clients
		Dying, //No longer accepting new clients
		Dead //Not running anymore.
	};
	ProcState procState = ProcState::Uninitialized;

	//TODO: queue of things to do after Ready

	//Updates our knowledge of the IPC service's state.
	void PollProcess();
	void CleanUpProcessHandles();
};