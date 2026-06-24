#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformNamedPipe.h"
#include <span>

#include "JMarkovJunior.h"
#include "JMJ_Constants.h"

#include "JMJ_ProcessManager.generated.h"


//A parsed MarkovJunior algorithm, which may be running any number of instances.
//Create a new one by calling 'ParseAlgorithm' on the engine subsystem 'UJmjProcessManager'.
USTRUCT(BlueprintType)
struct JMARKOVJUNIOR_API FJmjParsedAlgo
{
	GENERATED_BODY()
public:

	//ID's are uint32; the int type is just to get into BP.
	//0 is null.
	UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly)
	int ID = 0;
};
//A specific instance of a running MarkovJunior algorithm on a specific grid.
//Create a new one by calling 'StartAlgorithm' on the engine subsystem 'UJmjProcessManager'.
USTRUCT(BlueprintType)
struct JMARKOVJUNIOR_API FJmjAlgoState
{
	GENERATED_BODY()
public:

	//ID's are uint32; the int type is just to get into BP.
	//0 is null.
	UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly)
	int ID = 0;

	//The ID of the originating algorithm.
	//It's uint32; the type is just to get into BP.
	//0 is null.
	UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly)
	int AlgoID = 0;

	//The dimensionality of the grid this state is running on.
	UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly)
	int NDims = 0;
};

//Manages the JMarkovJunior process, which parses and runs algorithms for us.
//All requests must be handled on the game thread.
//
//For debug purposes this manager will first look for an existing IPC pipe,
//   so you can run your own Julia client.
//In that case the usual process management and stderr logging will not happen.
//
//In debug builds, the IPC process will be visible in the background.
//In test/release builds, the IPC process will be as hidden as possible.
UCLASS(BlueprintType)
class JMARKOVJUNIOR_API UJmjProcessManager : public UEngineSubsystem, public FTickableGameObject
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, BlueprintPure)
	static UJmjProcessManager* GetJMJSubsystem();
	

	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(Keywords="time duration"))
	float ProcessPollIntervalSeconds = 3.0f;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(Keywords="time left"))
	float SecondsToNextProcessPoll = 0.0f;
	//If true, the IPC process's stderr is flushed to our Unreal log much more frequently.
	//This helps ensure we see everything leading up to an internal crash.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, AdvancedDisplay)
	bool DebugFlushStderr = false;

	virtual void Initialize(FSubsystemCollectionBase& collection) override;
	virtual void Deinitialize() override;

	
	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsProcessInitialized() const { return procState > ProcState::Uninitialized; }
	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsProcessAlive() const { return procState > ProcState::Uninitialized && procState < ProcState::Dead; }
	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsProcessAcceptingClients() const { return procState == ProcState::Ready; }
	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsOurClientConnected() const { return namedPipe.IsValid() && namedPipe->IsReadyForRW(); }

	//Gets whether this manager is actually running the IPC process.
	//For debug purposes it can be managed externally.
	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsManagingProcess() const { return isManagingProcess; }

	//// FTickableGameObject interface
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return true; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UJmjProcessManager, STATGROUP_Tickables); }
	virtual void Tick(float deltaSeconds) override;
	///////

	////  Basic IPC calls (must be on the Game Thread for now)
	
	//Tries to parse a new MarkovJunior algorithm instance.
	//If it fails, then false is returned and an error message will be provided.
	//If it succeeds, true will be returned and an algorithm instance will be provided;
	//  consider manually destroying it once you know you're done with it.
	UFUNCTION(BlueprintCallable, Category="JMarkovJunior")
	bool ParseAlgorithm(const FString& sourceCode,
						FString& outErrMsg, FJmjParsedAlgo& outAlgo);
	//Destroys the given parsed algorithm, nulls out your own reference to it.
	//If the algorithm is invalid then nothing happens and False is returned.
	UFUNCTION(BlueprintCallable, Category="JMarkovJunior")
	bool DestroyAlgorithm(UPARAM(Ref) FJmjParsedAlgo& algo);

	//Starts running the given algorithm.
	//If your arguments are valid, returns true and outputs the new running algorithm state.
	//Otherwise returns false.
	//
	//You can provide as many RNG seeds as you want, or nothing at all to produce a nondeterministic run.
	//
	//Remember to destroy this instance once you're done using it!
	UFUNCTION(BlueprintCallable, Category="JMarkovJunior")
	bool StartAlgorithm(FJmjParsedAlgo algo,
						const TArray<int>& resolutionPerAxis,
						const TArray<int>& rngSeed,
						float autoTicksPerSecond,
						FJmjAlgoState& outState);
	//Deallocates the given running algorithm state.
	//You can't download its grid after calling this!
	UFUNCTION(BlueprintCallable, Category="JMarkovJunior")
	void DestroyAlgoState(UPARAM(Ref) FJmjAlgoState& state);

	//Manually ticks the given algorithm N iterations and returns whether the algorithm finished.
	UFUNCTION(BlueprintCallable, Category="JMarkovJunior")
	bool StepAlgorithm(FJmjAlgoState state, int count = 1);
	//Runs the given algorithm to completion.
	UFUNCTION(BlueprintCallable, Category="JMarkovJunior")
	void FinishAlgorithm(FJmjAlgoState state);
	UFUNCTION(BlueprintCallable, Category="JMarkovJunior")
	bool CheckAlgorithmFinished(FJmjAlgoState state);

	//Writes the current size and state of an algorithm's grid into your arrays (emptying their previous contents first).
	//To look up values in this array, call 'GridIndex()'.
	//
	//Note that the innermost axis on the grid is X.
	//There are 3D-specific and 2D-specific overloads as well.
	UFUNCTION(BlueprintCallable, Category="JMarkovJunior")
	void DownloadGrid(FJmjAlgoState state, TArray<int>& outResolution, TArray<uint8>& outValues);
	
	//////////////////////////////
	
	////  Higher-level IPC calls (must be on the Game Thread for now)

	//Reads the current state of a 2D algorithm's grid.
	//Fills your array with the grid bytes and returns its resolution.
	//
	//Note that the innermost axis on the grid is X.
	UFUNCTION(BlueprintCallable, Category="JMarkovJunior")
	FJmjIntVector2D DownloadGrid2D(FJmjAlgoState state, TArray<uint8>& outValues);
	//Reads the current state of a 3D algorithm's grid.
	//Fills your array with the grid bytes and returns its resolution.
	//
	//Note that the innermost axis on the grid is X.
	UFUNCTION(BlueprintCallable, Category="JMarkovJunior")
	FIntVector DownloadGrid3D(FJmjAlgoState state, TArray<uint8>& outValues);

	//Immediately parses an algorithm, generates a 2D grid, downloads that grid, and cleans up the objects.
	//Returns false if parsing failed and there is no result.
	//
	//Great for demo purposes or generating small levels.
	//Awful if you want to run a parsed algorithm multiple times.
	//
	//The output array is cleared before appending the grid data to it.
	//The output resolution will match the input unless you used Operations that resize the grid.
	UFUNCTION(BlueprintCallable, Category="JMarkovJunior")
	bool Generate2D(const FString& algoSrc, const FJmjIntVector2D& resolution,
					const TArray<int>& seeds,
					FString& parsingErrorMsg,
					TArray<uint8>& output, FJmjIntVector2D& outputResolution)
	{
		FJmjParsedAlgo algo;
		if (!ParseAlgorithm(algoSrc, parsingErrorMsg, algo))
			return false;
		
		FJmjAlgoState state;
		verify(StartAlgorithm(algo, { resolution.X, resolution.Y }, seeds, 0, state));

		FinishAlgorithm(state);
		TArray<int> outputRes;
		output.Empty();
		DownloadGrid(state, outputRes, output);
		outputResolution = { outputRes[0], outputRes[1] };

		DestroyAlgoState(state);
		DestroyAlgorithm(algo);
		return true;
	}
	//Immediately parses an algorithm, generates a 3D grid, downloads that grid, and cleans up the objects.
	//Returns false if parsing failed and there is no result.
	//
	//Great for demo purposes or generating small levels.
	//Awful if you want to run a parsed algorithm multiple times.
	//
	//The output array is cleared before appending the grid data to it.
	//The output resolution will match the input unless you used Operations that resize the grid.
	UFUNCTION(BlueprintCallable, Category="JMarkovJunior")
	bool Generate3D(const FString& algoSrc, const FIntVector& resolution,
					const TArray<int>& seeds,
					FString& parsingErrorMsg,
					TArray<uint8>& output, FIntVector& outputResolution)
	{
		FJmjParsedAlgo algo;
		if (!ParseAlgorithm(algoSrc, parsingErrorMsg, algo))
			return false;
		
		FJmjAlgoState state;
		verify(StartAlgorithm(algo, { resolution.X, resolution.Y, resolution.Z }, seeds, 0, state));

		FinishAlgorithm(state);
		TArray<int> outputRes;
		output.Empty();
		DownloadGrid(state, outputRes, output);
		outputResolution = { outputRes[0], outputRes[1], outputRes[2] };

		DestroyAlgoState(state);
		DestroyAlgorithm(algo);
		return true;
	}

	//////////////////////////////

private:

	bool isManagingProcess = false;
	FProcHandle process;
	void *stdoutFromChild = nullptr,
	        *stdoutFromHere = nullptr,
		 *stderrFromChild = nullptr,
	        *stderrFromHere = nullptr;
	FString pipeName;
	TArray<uint8> stdoutBuffer, stderrBuffer, pipeInnerBuffer;
	FString stderrStringBuffer;
	TUniquePtr<FGenericPlatformNamedPipe> namedPipe;
	void* killJobPimpl = nullptr;

	struct AlgoStateInfo : public FJmjAlgoState
	{
		float TicksPerSecond,
			  TimeTillNextTick;
		bool StillRunning;
	};
	TMap<int, AlgoStateInfo> algoStateInfos;

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

	void PollProcess();
	void CleanUpProcessHandles();
	void InitialIPCHandshake();
	void FlushStderr();

	//Pipe helpers:

	template<typename T>
	void NPWrite(const T& value)
	{
		if (DebugFlushStderr)
			FlushStderr();
		verify(namedPipe->WriteBytes(sizeof(T), &value));
	}
	template<typename T>
	void NPWrite(const T* value, int count)
	{
		if (DebugFlushStderr)
			FlushStderr();
		verify(namedPipe->WriteBytes(sizeof(T) * count, value));
	}

	template<typename T>
	T NPRead()
	{
		if (DebugFlushStderr)
			FlushStderr();
		
		T output;
		verify(namedPipe->ReadBytes(sizeof(T), &output));
		return output;
	}
	template<typename T>
	void NPRead(std::span<T> output)
	{
		if (DebugFlushStderr)
			FlushStderr();
		
		verify(namedPipe->ReadBytes(sizeof(T) * output.size(), output.data()));
	}
};