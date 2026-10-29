#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"

#include <span>
#include <optional>
#include <mutex>
#include <condition_variable>
#include <thread>

#include "JMarkovJunior.h"
#include "jmj_process.hpp"
#include "jmj_connection.hpp"
#include "JMJ_Constants.h"

#include "JMJ_ProcessManagement.generated.h"


class UMarkovJuniorAlgorithm;
class UMarkovJuniorRunState;


//Ties the lifetime of the IPC process to Unreal Engine's startup logic.
//
//Note that through this subsystem and the accompanying classes,
//   algorithm runs happen on worker threads and user logic happens on the game thread.
UCLASS(BlueprintType)
class JMARKOVJUNIOR_API UMarkovJuniorSubsystem : public UEngineSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& collection) override;
    virtual void Deinitialize() override;


private:
    friend UMarkovJuniorAlgorithm;
    friend UMarkovJuniorRunState;

    TOptional<jmj::ipc::ProcessManager> processSlot;
    
    // ReSharper disable once CppUE4ProbableMemoryIssuesWithUObjectsInContainer
    TSet<UMarkovJuniorRunState*> livingRuns;
    
    void EnsureProcessReady();
};

//A parsed algorithm which can be run any number of times.
UCLASS(BlueprintType, meta=(DontUseGenericSpawnObject))
class JMARKOVJUNIOR_API UMarkovJuniorAlgorithm final : public UObject
{
    GENERATED_BODY()
public:
    
    UFUNCTION(BlueprintCallable, DisplayName="Parse MarkovJunior algorithm")
    static UMarkovJuniorAlgorithm* Parse(const FString& src);


    //Starts a new 2D run of this algorithm.
    //
    //Note that state data is always X-innermost.
    //If you want the default initial state (filled with one color), pass an empty array.
    UFUNCTION(BlueprintCallable)
    UMarkovJuniorRunState* StartRunning2D(const FJmjIntVector2D& resolution,
                                          const TArray<int32>& seeds,
                                          const TArray<uint8>& initialValues);
    //Starts a new 3D run of this algorithm.
    //
    //Note that state data is always X-innermost.
    //If you want the default initial state (filled with one color), pass an empty array.
    UFUNCTION(BlueprintCallable)
    UMarkovJuniorRunState* StartRunning3D(const FIntVector& resolution,
                                          const TArray<int32>& seeds,
                                          const TArray<uint8>& initialValues);
    //Starts a new N-dimensional run of this algorithm.
    //
    //Note that state data is always X-innermost.
    //If you want the default initial state (filled with one color), pass an empty array.
    UFUNCTION(BlueprintCallable)
    UMarkovJuniorRunState* StartRunning(const TArray<int>& resolution,
                                        const TArray<int32>& seeds,
                                        const TArray<uint8>& initialValues);

    
    //"Destroys" this object from the POV of the library, preventing new algorithm runs
    //   but not affecting current ones.
    //Does not trigger GC or anything else here on the Unreal side.
    void CleanUp();
    virtual void BeginDestroy() override { if (isAlive) CleanUp(); Super::BeginDestroy(); }

private:
    UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly, meta=(AllowPrivateAccess))
    int32 algoHandle = -1;
    UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly, meta=(AllowPrivateAccess))
    bool isAlive = false;
};


UENUM(BlueprintType)
enum class EMarkovJuniorRunStatus : uint8
{
    //Currently executing (in a worker thread).
    Running,
    //Emitted a tagged event; waiting for the user to acknowledge and resume.
    TaggedEvent,
    //Finished running.
    Completed
};

//Represents a running JMarkovJunior algorithm instance.
//
//The run is automatically cancelled if you drop this object and the GC cleans it up,
//  and this works just fine if the algorithm is already done running,
//  but if you are canceling an ongoing run then it's more efficient to manually call 'CancelRun()'.
UCLASS(BlueprintType, meta=(DontUseGenericSpawnObject))
class JMARKOVJUNIOR_API UMarkovJuniorRunState : public UObject
{
    GENERATED_BODY()
public:
    
    //When a tagged event happens (current status == 'TaggedEvent'), or the run finishes (current status == 'Completed'),
    //   this array is automatically updated to hold the newest grid state.
    //You MUST NOT read from this grid during other states!
    //
    //During mutable tagged events, you may also MODIFY this array.
    //
    //Once you're finished processing your tagged event, call 'ResumeAfterEvent()'.
    UPROPERTY(BlueprintReadWrite, EditAnywhere, AdvancedDisplay)
    TArray<uint8> LastKnownPixels;

    UFUNCTION(BlueprintCallable, BlueprintPure)
    EMarkovJuniorRunStatus GetLastKnownStatus() const;
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool CanReadGrid() const { auto s = GetLastKnownStatus(); return (s == EMarkovJuniorRunStatus::TaggedEvent) || (s == EMarkovJuniorRunStatus::Completed); }
    const auto& GetLastKnownResolution() const { return lastKnownGridResolution; }
    auto GetLastNamedEvent() const { return lastNamedEvent; }

    //Extra dimensions are flattened onto the outer (Y) axis.
    //Missing dimensions are given the size 1.
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FJmjIntVector2D GetLastKnownResolution2D() const;
    //Extra dimensions are flattened onto the outer (Z) axis.
    //Missing dimensions are given the size 1.
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FIntVector GetLastKnownResolution3D() const;

    //Waits on the calling thread (usually the game thread),
    //  until this run finishes or hits a tagged event.
    //Returns the new status of this run (Completed or TaggedEvent).
    //
    //Good for tests and demos, but probably a bad idea for release.
    //
    //Internal note: if we make it to completion, the worker thread is also cleaned up.
    UFUNCTION(BlueprintCallable)
    EMarkovJuniorRunStatus WaitForEvent(bool skipTaggedEvents = false);

    //Once this run has paused on a tagged event (current status == 'TaggedEvent'),
    //   you can read 'LastKnownPixels' and 'LastKnownGridResolution' as desired.
    //If it was a mutating event, you can write into 'LastKnownPixels'.
    //Finally, call this function to resume the algorithm.
    //
    //Once you call this function, do not touch the pixel grid/resolution again
    //   until hitting another tagged event or algorithm completion!
    UFUNCTION(BlueprintCallable)
    void ResumeAfterEvent(bool uploadPixels = false);
    //Kills this run.
    //After calling this, you should not use this object anymore
    //  apart from optionally reading its final grid state.
    UFUNCTION(BlueprintCallable)
    void CancelRun(bool downloadLastGridState = false);

    //The below functions handle cleanup through GC.
    //The subsystem will also force a cleanup as it dies,
    //  so this code needs to handle the possibility of already being dead.
    virtual void BeginDestroy() override { CancelRun(false); Super::BeginDestroy(); }
    virtual bool IsReadyForFinishDestroy() override { return Super::IsReadyForFinishDestroy() && GetLastKnownStatus() == EMarkovJuniorRunStatus::Completed; }
    virtual void FinishDestroy() override
    {
        WaitForWorkerCleanup();
        Super::FinishDestroy();
    }


private:
    
    friend class UMarkovJuniorAlgorithm;

    UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly, Transient, meta=(AllowPrivateAccess))
    int32 runHandle = -1;
    UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly, Transient, meta=(AllowPrivateAccess))
    FName lastNamedEvent;
    
    //When a tagged event happens (current status == 'TaggedEvent'), or the run finishes (current status == 'Completed'),
    //   this array is automatically updated to hold the newest grid state.
    //You MUST NOT read from this grid during other states!
    UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly, Transient, meta=(AllowPrivateAccess))
    TArray<int> lastKnownGridResolution;

    //Stuff in this struct should be guarded with a mutex.
    struct ThreadedComms
    {
        EMarkovJuniorRunStatus status;
        
        enum EResumeState : uint8 { R_NotYet, R_GoAhead, R_GoAheadAndUpload };
        EResumeState resumeAction;

        bool requestCancellation, cancelWithoutReadingGrid;
    };
    ThreadedComms threadedComms{
        .status = EMarkovJuniorRunStatus::Running,
        .resumeAction = ThreadedComms::R_NotYet,
        .requestCancellation = false,
        .cancelWithoutReadingGrid = false
    };
    mutable std::mutex threadedCommsMutex;
    std::condition_variable threadedCommsChanged;
    
    struct WorkerThreadRunner : public FRunnable
    {
        UMarkovJuniorRunState* State;
        FRunnableThread* Thread;
        WorkerThreadRunner(UMarkovJuniorRunState* state) : State(state)
        {
            Thread = FRunnableThread::Create(this, TEXT("JMarkovJuniorWorkerThread"));
        }
        virtual bool Init() override { return true; }
        virtual uint32 Run() override { State->RunWorkerThreadLogic(); return 0; }
    };
    std::optional<WorkerThreadRunner> workerThreadRunner;
    void RunWorkerThreadLogic();

    //Assumes that CancelRun() has already been executed.
    void WaitForWorkerCleanup();

    friend UMarkovJuniorSubsystem;
};