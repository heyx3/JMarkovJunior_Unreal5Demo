#include "JMJ_ProcessManagement.h"

#include <type_traits>
#include <string>

#include "Algo/Accumulate.h"
#include "Interfaces/IPluginManager.h"

#include "jmj_ipc.hpp"
#include "JMJ_DevSettings.h"


namespace
{
    template<typename TArr>
    auto TArraySpan(TArr&& arr) { return std::span{ arr.GetData(), static_cast<size_t>(arr.Num()) }; }
    template<typename TElType, typename TArr>
    auto TArrayReinterpretSpan(TArr&& arr)
    {
        using TSrcElType = typename std::remove_cvref_t<TArr>::ElementType;
        return std::span{
            reinterpret_cast<TElType*>(arr.GetData()),
            (sizeof(TSrcElType) * static_cast<size_t>(arr.Num())) / sizeof(TElType)
        };
    }

    auto& GetResolutionBuffer() { thread_local TArray<int> a; return a; }
}


void UMarkovJuniorSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    Super::Initialize(collection);

    //Apply initial config settings.
    if (IsRunningCommandlet() && GetDefault<UJmjDevSettings>()->RandomizeNamedPipeForCommandlets)
        jmj::ipc::PipeConnection::UsedPipeName() += std::to_string(FPlatformProcess::GetCurrentProcessId());

    //Start the IPC process (or connect to an existing one).
    FString processName =
        #if PLATFORM_WINDOWS
            TEXT("JMarkovJunior_IPC.exe")
        #else
            #warning "Unsupported platform for JMarkovJunior! Until it's built for this platform, you can only run it using an externally-managed Julia process"
            TEXT("!null!.exe")
        #endif
    ;
    FString processPath = FPaths::Combine(
        IPluginManager::Get().FindPlugin("JMarkovJunior")->GetBaseDir(),
        FString::Printf(
            TEXT("Binaries/ThirdParty/JMJ/bin/%s"),
            *processName
        )
    );
    processSlot.Emplace(jmj::ipc::ProcessSettings{
        .ExecutablePath = processPath,
        .Arguments = {
            "-k",
            FString{ jmj::ipc::PipeConnection::UsedPipeName().c_str() },
            "--julia-args", "-t", "auto"
        }
    });
}
void UMarkovJuniorSubsystem::Deinitialize()
{
    //Shut down all existing runs.
    for (auto* run : livingRuns)
        run->CancelRun(false);
    for (auto* run : livingRuns.Array())
        run->WaitForWorkerCleanup();
    check(livingRuns.IsEmpty());

    //Shut down the IPC process (if managed internally).
    processSlot.Reset();

    Super::Deinitialize();
}

void UMarkovJuniorSubsystem::EnsureProcessReady()
{
    if (processSlot->GetState() == jmj::ipc::ProcessState::Ready)
        return;

    //Read the timeout from config.
    std::optional<std::chrono::milliseconds> ipcTimeout;
    float ipcTimeoutConfig = GetDefault<UJmjDevSettings>()->StartupTimeoutSeconds;
    if (ipcTimeoutConfig > 0)
        ipcTimeout = std::chrono::milliseconds{ FMath::CeilToInt64(ipcTimeoutConfig * 1000) };
    
    if (!processSlot->WaitUntilReady(ipcTimeout))
    {
        UE_LOG(LogJMarkovJunior, Fatal,
               TEXT("JMJ IPC process crashed OR failed to be ready within the configured timeout of %.1f seconds"),
               ipcTimeoutConfig);
    }
}


static_assert(sizeof(uint8) == sizeof(jmj::ipc::Pixel_t), "Pixel byte-size has changed");
static_assert(sizeof(int32) == sizeof(jmj::ipc::Handle_t), "Handle size has changed");

UMarkovJuniorAlgorithm* UMarkovJuniorAlgorithm::Parse(const FString& src)
{
    GEngine->GetEngineSubsystem<UMarkovJuniorSubsystem>()->EnsureProcessReady();
    
    auto& connection = jmj::ipc::PipeConnection::GetOrMakeThreadLocal();
    auto result = connection.ParseAlgorithm(src);
    
    if (std::holds_alternative<FString>(result))
    {
        UE_LOG(LogJMarkovJunior, Error, TEXT("Failed to parse algorithm: %s"),
               *std::get<FString>(result));
        return nullptr;
    }
    else
    {
        auto* obj = NewObject<UMarkovJuniorAlgorithm>();
        obj->algoHandle = static_cast<int32>(std::get<jmj::ipc::Handle_t>(result));
        obj->isAlive = true;
        return obj;
    }
}
void UMarkovJuniorAlgorithm::CleanUp()
{
    if (!isAlive)
    {
        UE_LOG(LogJMarkovJunior, Error, TEXT("Tried to clean up an algorithm that's already dead!"));
        return;
    }

    //If the subsystem is already shut down, we can't talk to the IPC.
    auto* subsystem = (GEngine ? GEngine->GetEngineSubsystem<UMarkovJuniorSubsystem>() : nullptr);
    if (!subsystem || !subsystem->processSlot.IsSet())
        return;
    
    isAlive = false;
    verify(jmj::ipc::PipeConnection::GetOrMakeThreadLocal()
              .DestroyAlgorithm(static_cast<jmj::ipc::Handle_t>(algoHandle)));
}

UMarkovJuniorRunState* UMarkovJuniorAlgorithm::StartRunning(const TArray<int>& resolution,
                                                            const TArray<int32>& seeds,
                                                            const TArray<uint8>& initialValues)
{
    if (algoHandle == -1)
    {
        UE_LOG(LogJMarkovJunior, Error, TEXT("You created this object manually! Must call Parse() instead."));
        return nullptr;
    }
    else if (!isAlive)
    {
        UE_LOG(LogJMarkovJunior, Error, TEXT("Tried to start running an algorithm that's already been Cleaned Up"));
        return nullptr;
    }
    
    auto& connection = jmj::ipc::PipeConnection::GetOrMakeThreadLocal();
    
    auto newHandle = connection.StartAlgorithmRun(
        static_cast<jmj::ipc::Handle_t>(algoHandle),
        TArraySpan(resolution), TArrayReinterpretSpan<const std::byte>(seeds),
        { },
        initialValues.IsEmpty() ? std::span<const jmj::ipc::Pixel_t>{ } : TArraySpan(initialValues)
    );
    if (!newHandle.has_value())
        return nullptr;

    auto* stateObj = NewObject<UMarkovJuniorRunState>();
    int nPixels = Algo::Accumulate<int32>(resolution, 1, [](auto a, auto b) { return a * b; });
    stateObj->LastKnownPixels.SetNum(nPixels);
    stateObj->lastKnownGridResolution.Append(resolution);
    if (initialValues.IsEmpty())
    {
        verify(connection.ReadCurrentGrid(
            *newHandle,
            TArraySpan(stateObj->LastKnownPixels)
        ));
    }
    else
    {
        for (int i = 0; i < nPixels; ++i)
            stateObj->LastKnownPixels[i] = initialValues[i];
    }
    stateObj->runHandle = static_cast<int32>(*newHandle);

    stateObj->workerThreadRunner.emplace(stateObj);
    GEngine->GetEngineSubsystem<UMarkovJuniorSubsystem>()->livingRuns.Add(stateObj);
    return stateObj;
}
UMarkovJuniorRunState* UMarkovJuniorAlgorithm::StartRunning2D(const FJmjIntVector2D& resolution,
                                                              const TArray<int32>& seeds,
                                                              const TArray<uint8>& initialValues)
{
    auto& resolutionArray = GetResolutionBuffer();
    resolutionArray.Reset();
    resolutionArray.Add(resolution.X);
    resolutionArray.Add(resolution.Y);
    return StartRunning(resolutionArray, seeds, initialValues);
}
UMarkovJuniorRunState* UMarkovJuniorAlgorithm::StartRunning3D(const FIntVector& resolution,
                                                              const TArray<int32>& seeds,
                                                              const TArray<uint8>& initialValues)
{
    auto& resolutionArray = GetResolutionBuffer();
    resolutionArray.Reset();
    resolutionArray.Add(resolution.X);
    resolutionArray.Add(resolution.Y);
    resolutionArray.Add(resolution.Z);
    return StartRunning(resolutionArray, seeds, initialValues);
}

EMarkovJuniorRunStatus UMarkovJuniorRunState::GetLastKnownStatus() const
{
    std::lock_guard lock{ threadedCommsMutex };
    return threadedComms.status; 
}

FJmjIntVector2D UMarkovJuniorRunState::GetLastKnownResolution2D() const
{
    FJmjIntVector2D res{
        lastKnownGridResolution.IsEmpty() ? 1 : lastKnownGridResolution[0],
        (lastKnownGridResolution.Num() < 2) ? 1 : lastKnownGridResolution[1]
    };
    for (int extraAxis = 2; extraAxis < lastKnownGridResolution.Num(); ++extraAxis)
        res.Y *= lastKnownGridResolution[extraAxis];
    return { res };
}
FIntVector UMarkovJuniorRunState::GetLastKnownResolution3D() const
{
    FIntVector res{
        lastKnownGridResolution.IsEmpty() ? 1 : lastKnownGridResolution[0],
        (lastKnownGridResolution.Num() < 2) ? 1 : lastKnownGridResolution[1],
        (lastKnownGridResolution.Num() < 3) ? 1 : lastKnownGridResolution[2]
    };
    for (int extraAxis = 3; extraAxis < lastKnownGridResolution.Num(); ++extraAxis)
        res.Z *= lastKnownGridResolution[extraAxis];
    return res;
}

EMarkovJuniorRunStatus UMarkovJuniorRunState::WaitForEvent(bool skipTaggedEvents)
{
    while (true)
    {
        std::unique_lock lock{ threadedCommsMutex };
        threadedCommsChanged.wait(lock, [&]
        {
            return threadedComms.status == EMarkovJuniorRunStatus::Completed ||
                   threadedComms.status == EMarkovJuniorRunStatus::TaggedEvent;
        });
        
        if (skipTaggedEvents && threadedComms.status == EMarkovJuniorRunStatus::TaggedEvent)
        {
            threadedComms.status = EMarkovJuniorRunStatus::Running;
            threadedComms.resumeAction = ThreadedComms::R_GoAhead;
            threadedCommsChanged.notify_all();
        }
        else
        {
            //Currently, setting the status to 'Completed' is the very last thing the worker thread does.
            //However, in case that changes we will release the lock before waiting for the worker to close.
            auto status = threadedComms.status;
            lock.unlock();
            
            if (status == EMarkovJuniorRunStatus::Completed)
                WaitForWorkerCleanup();
            return status;
        }
    }
}

void UMarkovJuniorRunState::ResumeAfterEvent(bool uploadPixels)
{
    std::lock_guard lock{ threadedCommsMutex };

    if (threadedComms.status == EMarkovJuniorRunStatus::TaggedEvent)
    {
        threadedComms.status = EMarkovJuniorRunStatus::Running;
        threadedComms.resumeAction = uploadPixels ?
            ThreadedComms::R_GoAheadAndUpload :
            ThreadedComms::R_GoAhead;
    }
    else
    {
        UE_LOG(LogJMarkovJunior, Error,
               TEXT("Tried to 'resume' from a MarkovJunior run with status %s"),
               *UEnum::GetValueAsString(threadedComms.status));
    }
    threadedCommsChanged.notify_all();
}
void UMarkovJuniorRunState::CancelRun(bool downloadLastGridState)
{
    if (!workerThreadRunner.has_value())
        return;

    std::lock_guard lock{ threadedCommsMutex };
    threadedComms.requestCancellation = true;
    threadedComms.cancelWithoutReadingGrid = !downloadLastGridState;
    threadedCommsChanged.notify_all();
}
void UMarkovJuniorRunState::WaitForWorkerCleanup()
{
    if (!workerThreadRunner.has_value())
        return;
        
    workerThreadRunner->Thread->WaitForCompletion();
    delete workerThreadRunner->Thread;
    workerThreadRunner.reset();

    auto* subsystem = (GEngine ? GEngine->GetEngineSubsystem<UMarkovJuniorSubsystem>() : nullptr);
    if (subsystem)
        subsystem->livingRuns.Remove(this);
}

void UMarkovJuniorRunState::RunWorkerThreadLogic()
{
    auto& connection = jmj::ipc::PipeConnection::GetOrMakeThreadLocal();
    std::u8string taggedEventName;
    std::vector<int> resolutionRaw;
    
    auto downloadGridState = [&]()
    {
        //Update the resolution.
        resolutionRaw.clear();
        if (!connection.GetCurrentResolution(static_cast<jmj::ipc::Handle_t>(runHandle), resolutionRaw))
            return;
        lastKnownGridResolution.Reset();
        lastKnownGridResolution.Append(resolutionRaw.data(), static_cast<int>(resolutionRaw.size()));

        //Update the pixel buffer.
        auto nPixels = Algo::Accumulate<int32>(lastKnownGridResolution, 1, [](auto a, auto b) { return a * b; });
        LastKnownPixels.SetNum(nPixels, EAllowShrinking::No);
        verify(connection.ReadCurrentGrid(
            static_cast<jmj::ipc::Handle_t>(runHandle),
            TArraySpan(LastKnownPixels)
        ));
    };

    bool shouldBreakLoop = false,
         skipFinalDownload = false;
    while (!shouldBreakLoop)
    {
        auto tickResult = connection.TickAlgorithm(
            static_cast<jmj::ipc::Handle_t>(runHandle),
            jmj::ipc::TickAmount{ jmj::TickPriorityHigh, 1 },
            taggedEventName
        );
        switch (tickResult)
        {
            case jmj::ipc::TickResult::Error:
                check(false);
                //Shouldn't ever happen, but throw a log and loop-exit for shipping builds just in case.
                UE_LOG(LogJMarkovJunior, Error, TEXT("MarkovJunior IPC tick failed! Exiting early..."));
                shouldBreakLoop = true;
                skipFinalDownload = true;
            break;
                
            case jmj::ipc::TickResult::Success:
                //Check for cancellation request; otherwise just keep running.
                {
                    std::lock_guard lock{ threadedCommsMutex };
                    if (threadedComms.requestCancellation)
                    {
                        shouldBreakLoop = true;
                        skipFinalDownload = threadedComms.cancelWithoutReadingGrid;
                    }
                }
            break;

            case jmj::ipc::TickResult::TaggedEvent: {
                lastNamedEvent = { StringCast<TCHAR>(taggedEventName.c_str()).Get() };
                downloadGridState();

                //Tell the main thread what happened, and wait for a response.
                bool shouldUploadPixels = false;
                {
                    std::unique_lock lock{ threadedCommsMutex };
                    if (threadedComms.requestCancellation)
                    {
                        shouldBreakLoop = true;
                        skipFinalDownload = threadedComms.cancelWithoutReadingGrid;
                        break;
                    }

                    threadedComms.status = EMarkovJuniorRunStatus::TaggedEvent;
                    threadedComms.resumeAction = ThreadedComms::R_NotYet;
                
                    threadedCommsChanged.notify_all();
                    threadedCommsChanged.wait(lock, [&]
                    {
                        return (threadedComms.resumeAction != ThreadedComms::R_NotYet) ||
                               threadedComms.requestCancellation;
                    });
                    if (threadedComms.requestCancellation)
                    {
                        shouldBreakLoop = true;
                        skipFinalDownload = threadedComms.cancelWithoutReadingGrid;
                        break;
                    }
                    shouldUploadPixels = (threadedComms.resumeAction == ThreadedComms::R_GoAheadAndUpload);
                }

                //Carry out the response from the main thread.
                if (shouldUploadPixels)
                {
                    verify(connection.WriteCurrentGrid(
                        static_cast<jmj::ipc::Handle_t>(runHandle),
                        TArraySpan(LastKnownPixels)
                    ));
                }
            } break;

            case jmj::ipc::TickResult::AlgoCompleted:
                shouldBreakLoop = true;
            break;

            default: check(false); break;
        }
    }

    //Download the final state, clean up IPC resources, and notify the main thread.
    if (!skipFinalDownload)
    {
        {
            std::lock_guard lock{ threadedCommsMutex };
            threadedComms.status = EMarkovJuniorRunStatus::Running;
        }
        downloadGridState();
    }
    verify(connection.CancelAlgorithmRun(static_cast<jmj::ipc::Handle_t>(runHandle)));
    {
        std::lock_guard lock{ threadedCommsMutex };
        threadedComms.status = EMarkovJuniorRunStatus::Completed;
        threadedCommsChanged.notify_all();
    }
}