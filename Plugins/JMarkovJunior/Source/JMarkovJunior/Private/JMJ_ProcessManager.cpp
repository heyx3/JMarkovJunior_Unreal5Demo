#include "JMJ_ProcessManager.h"

#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "Windows/WindowsPlatformNamedPipe.h"

#include "jmj_ipc.hpp"


struct JmjPlatformKillJob;
#if PLATFORM_WINDOWS
	#include "Windows/AllowWindowsPlatformTypes.h"
	struct JmjPlatformKillJob
	{
		Windows::HANDLE hJob;

		JmjPlatformKillJob(Windows::HANDLE hProcess)
		{
			hJob = CreateJobObject(nullptr, nullptr);

			JOBOBJECT_EXTENDED_LIMIT_INFORMATION limitInfo = { };
			limitInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &limitInfo, sizeof(limitInfo));

			AssignProcessToJobObject(hJob, hProcess);
		}
		~JmjPlatformKillJob()
		{
			CloseHandle(hJob);
		}
	};
	#include "Windows/HideWindowsPlatformTypes.h"
#else
	#error "JMarkovJunior: process-kill jobs not yet implemented for this OS!"
#endif


void UJmjProcessManager::Initialize(FSubsystemCollectionBase& collection)
{
	//Note: the apparent point of `collection` is to allow us
	//  to force dependent subsystems to initialize first.
	
	Super::Initialize(collection);
	
	check(procState == ProcState::Uninitialized);
	check(stdoutBuffer.IsEmpty());
	check(stderrBuffer.IsEmpty());
	check(killJobPimpl == nullptr);
	
	pipeName = StringCast<TCHAR>(jmj::ipc::NamedPipe.data()).Get();
	namedPipe = MakeUnique<FWindowsPlatformNamedPipe>();
	
	//Try to make the connection with any existing process first.
	if (namedPipe->Create(pipeName, false, false))
	{
		UE_LOG(LogJMarkovJunior, Warning, TEXT("Found an existing named pipe! No need to manage our own process."));;
		isManagingProcess = false;
		
		InitialIPCHandshake();
		procState = ProcState::Ready;
	}
	//Otherwise start and manage our own process.
	else
	{
		isManagingProcess = true;

		//CreatePipe() initializes the two sides of one pipe between us and child process.
		FPlatformProcess::CreatePipe(stdoutFromHere, stdoutFromChild, false);
		FPlatformProcess::CreatePipe(stderrFromHere, stderrFromChild, false);
	
		UE_LOG(LogJMarkovJunior, Log, TEXT("Starting IPC process..."));
		bool hideProcess =
			#if UE_BUILD_SHIPPING || UE_BUILD_TEST
				false
			#else
				true
			#endif
		;
		FString processPath = FPaths::Combine(
			IPluginManager::Get().FindPlugin(TEXT("JMarkovJunior"))->GetBaseDir(),
			TEXT("Binaries/ThirdParty/JMJ/bin/JMarkovJunior_IPC.exe")
		);
		process = FPlatformProcess::CreateProc(
			GetData(processPath),
			TEXT("-k --julia-args -t auto"), //TODO: Support configuration of params (both IPC and JRT) through project settings
			false, hideProcess, hideProcess,
			nullptr, 0, nullptr,
			stdoutFromChild, nullptr, stderrFromChild
		);
		procState = ProcState::Booting;
		killJobPimpl = new JmjPlatformKillJob(process.Get());
		UE_LOG(LogJMarkovJunior, Log, TEXT("\tProcess Started"));	
	}
}
void UJmjProcessManager::Deinitialize()
{
	procState = ProcState::Dead;
	CleanUpProcessHandles();
	if (killJobPimpl != nullptr)
		delete static_cast<JmjPlatformKillJob*>(killJobPimpl);
	
	Super::Deinitialize();
}

void UJmjProcessManager::Tick(float deltaSeconds)
{
	//Poll the IPC service's process.
	SecondsToNextProcessPoll -= deltaSeconds;
	if (SecondsToNextProcessPoll <= 0)
	{
		PollProcess();
		SecondsToNextProcessPoll = ProcessPollIntervalSeconds;
	}

	//Issue tick commands.
	for (auto& [stateID, stateInfo] : algoStateInfos)
	{
		if (stateInfo.TicksPerSecond <= 0)
			continue;

		//Update timing and count how many ticks will happen this frame.
		stateInfo.TimeTillNextTick -= deltaSeconds;
		int nTicks = FMath::FloorToInt(-stateInfo.TimeTillNextTick * stateInfo.TicksPerSecond);
		if (nTicks < 1)
			continue;
		stateInfo.TimeTillNextTick += nTicks / stateInfo.TicksPerSecond;

		stateInfo.StillRunning = !StepAlgorithm({ stateID, stateInfo.AlgoID }, nTicks);
	}
}
void UJmjProcessManager::PollProcess()
{
	auto checkProcSuddenDeath = [&]()
	{
		if (FPlatformProcess::IsProcRunning(process))
			return false;
		
		//Launching again is an option, but something killed it.
		//I don't want to bog down the system with a million process launches.
		procState = ProcState::Dead;
		UE_LOG(LogJMarkovJunior, Error, TEXT("IPC process suddenly died! No more requests can be serviced."));
		CleanUpProcessHandles();
		return true;
	};
	
	switch (procState)
	{
		case ProcState::Uninitialized:
			//It can happen that we tick before initialization.
			//Just ignore it.
		break;
	
		case ProcState::Booting:
			FlushStderr();
		
			//Look for the "ready for clients" signal.
			pipeInnerBuffer.Empty();
			FPlatformProcess::ReadPipeToArray(stdoutFromHere, pipeInnerBuffer);
			//  (note: the above function returns whether any new bytes were read in)
			stdoutBuffer.Append(pipeInnerBuffer);
			if (stdoutBuffer.Num() >= 4)
			{
				auto code = *reinterpret_cast<uint32*>(stdoutBuffer.GetData());
				stdoutBuffer.RemoveAt(0, 4, EAllowShrinking::No);
				checkf(code == jmj::ipc::StdoutStartCode,
					   TEXT("Got JMJ startup code %u instead of %u"),
					   code, jmj::ipc::StdoutStartCode);

				procState = ProcState::Ready;
				UE_LOG(LogJMarkovJunior, Log, TEXT("IPC process is ready for clients at %s"), *pipeName);

				if (namedPipe->Create(pipeName, false, false))
				{
					InitialIPCHandshake();
				}
				else
				{
					UE_LOG(LogJMarkovJunior, Error,
					       TEXT("Failed to open the named pipe \"%s\"! We can't service any JMarkovJunior requests"),
					       *pipeName);
					FPlatformProcess::TerminateProc(process);

					procState = ProcState::Dead;
					CleanUpProcessHandles();
				}
			}
			else
			{
				checkProcSuddenDeath();
			}
		break;
		
		case ProcState::Ready:
			FlushStderr();
			if (!isManagingProcess)
				break;
			//Look for the "no more clients" signal.
			pipeInnerBuffer.Empty();
			FPlatformProcess::ReadPipeToArray(stdoutFromHere, pipeInnerBuffer);
			stdoutBuffer.Append(pipeInnerBuffer);
			if (stdoutBuffer.Num() >= 4)
			{
				auto code = *reinterpret_cast<uint32*>(stdoutBuffer.GetData());
				stdoutBuffer.RemoveAt(0, 4, EAllowShrinking::No);
				checkf(code == jmj::ipc::StdoutStopCode,
					   TEXT("Got JMJ stop code %u instead of %u"),
					   code, jmj::ipc::StdoutStopCode);

				procState = ProcState::Dying;
				UE_LOG(LogJMarkovJunior, Log, TEXT("IPC process announced it is rejecting new clients (shutting down)"));
			}
			else
			{
				checkProcSuddenDeath();
			}
		break;
		
		case ProcState::Dying:
			FlushStderr();
			if (!FPlatformProcess::IsProcRunning(process))
			{
				procState = ProcState::Dead;
				UE_LOG(LogJMarkovJunior, Log, TEXT("IPC process has died"));
				CleanUpProcessHandles();
			}
		break;
		
		case ProcState::Dead:
			//Nothing to do here.
		break;
		
		default: check(false); break;
	}
}

void UJmjProcessManager::CleanUpProcessHandles()
{
	if (process.IsValid())
	{
		check(isManagingProcess);
		
		FPlatformProcess::ClosePipe(stdoutFromHere, stdoutFromChild);
		FPlatformProcess::ClosePipe(stderrFromHere, stderrFromChild);
		FPlatformProcess::CloseProc(process);
		
		stdoutFromChild = nullptr;
		stdoutFromHere = nullptr;
		stderrFromChild = nullptr;
		stderrFromHere = nullptr;
		process.Reset();
	}
}

bool UJmjProcessManager::ParseAlgorithm(const FString& sourceCode,
										FString& outErrMsg, FJmjParsedAlgo& outAlgo)
{
	check(IsInGameThread());
	if (!IsOurClientConnected())
	{
		outErrMsg = TEXT("We aren't currently connected to the JMJ process, so no operations can be performed");
		return false;
	}
	
	//By my understanding the 'length' of StringCast<UTF8CHAR> is actually the byte count --
	//   not the number of UTF8 characters, which are variable-size.
	//The below test verifies this.
	#if DO_CHECK
		FString testStr = TEXT("Hello, 世界!");
		check(testStr.Len() == 10);
		auto testStrUtf8 = StringCast<UTF8CHAR>(*testStr);
		check(testStrUtf8.Length() == 14);
	#endif
	auto sourceCodeUTF8 = StringCast<UTF8CHAR>(*sourceCode);
	
	NPWrite(uint32{ 1 });
	NPWrite(static_cast<uint32>(sourceCodeUTF8.Length()));
	NPWrite(sourceCodeUTF8.Get(), sourceCodeUTF8.Length());

	bool succeeded = NPRead<uint8>() == 1;
	if (succeeded)
	{
		outAlgo.ID = static_cast<int>(NPRead<uint32>());
		UE_LOG(LogJMarkovJunior, Log, TEXT("Successfully parsed algorithm #%i"), outAlgo.ID);
	}
	else
	{
		auto errMsgSize = NPRead<uint32>();
		TArray<UTF8CHAR> errMsgBuffer;
		errMsgBuffer.SetNumUninitialized(errMsgSize);
		NPRead(std::span{ errMsgBuffer.GetData(), errMsgSize });

		outErrMsg = { errMsgBuffer.GetData() };
		UE_LOG(LogJMarkovJunior, Error, TEXT("Failed to parse algorithm: %s"), *outErrMsg);
	}
	return succeeded;
}

bool UJmjProcessManager::DestroyAlgorithm(FJmjParsedAlgo& algo)
{
	check(IsInGameThread());
	if (!IsOurClientConnected())
	{
		UE_LOG(LogJMarkovJunior, Error,
		       TEXT("DestroyAlgorithm(): We aren't currently connected to the JMJ process, "
				      "so no operations can be performed"));
		return false;
	}

	NPWrite(uint32{ 2 });
	NPWrite(static_cast<uint32>(algo.ID));

	bool success = NPRead<uint8>() == 1;
	if (success)
	{
		UE_LOG(LogJMarkovJunior, Log, TEXT("Destroyed algorithm #%i"), algo.ID);
		algo.ID = 0;
	}
	else
	{
		UE_LOG(LogJMarkovJunior, Error, TEXT("DestroyAlgorithm(): #%i doesn't exist!"), algo.ID);
	}
	return success;
}

bool UJmjProcessManager::StartAlgorithm(FJmjParsedAlgo algo,
										const TArray<int>& resolutionPerAxis,
										const TArray<int>& rngSeed,
										float autoTicksPerSecond,
										FJmjAlgoState& outState)
{
	check(IsInGameThread());
	if (!IsOurClientConnected())
	{
		UE_LOG(LogJMarkovJunior, Error,
			   TEXT("DestroyAlgorithm(): We aren't currently connected to the JMJ process, "
					  "so no operations can be performed"));
		return false;
	}

	if (resolutionPerAxis.IsEmpty())
	{
		UE_LOG(LogJMarkovJunior, Error, TEXT("StartAlgorithm(): grid must be at least 1D!"));
		return false;
	}
	if (autoTicksPerSecond < 0)
	{
		UE_LOG(LogJMarkovJunior, Warning,
			   TEXT("StartAlgorithm() received autoTicksPerSecond=%f! Bumping it to 0"),
			   autoTicksPerSecond);
		autoTicksPerSecond = 0;
	}
	
	NPWrite(uint32{ 3 });
	NPWrite(static_cast<uint32>(algo.ID));
	NPWrite(static_cast<uint32>(resolutionPerAxis.Num()));
	for (int r : resolutionPerAxis)
		NPWrite(static_cast<uint32>(r));
	NPWrite(static_cast<uint32>(rngSeed.Num() * sizeof(int)));
	for (int s : rngSeed)
		NPWrite(static_cast<unsigned int>(s));

	bool success = NPRead<uint8>() == 1;
	if (success)
	{
		outState = {
			static_cast<int>(NPRead<uint32_t>()),
			algo.ID,
			resolutionPerAxis.Num()
		};
		algoStateInfos.Add(outState.ID, AlgoStateInfo{
			outState,
			autoTicksPerSecond, 1.0f / autoTicksPerSecond,
			true
		});
		return true;
	}
	else
	{
		UE_LOG(LogJMarkovJunior, Error, TEXT("StartAlgorithm() failed!"));
		return false;
	}
}
void UJmjProcessManager::DestroyAlgoState(FJmjAlgoState& state)
{
	check(IsInGameThread());
	if (!IsOurClientConnected())
	{
		UE_LOG(LogJMarkovJunior, Error,
			   TEXT("DestroyAlgorithm(): We aren't currently connected to the JMJ process, "
					  "so no operations can be performed"));
		return;
	}
	
	NPWrite(uint32_t{ 4 });
	NPWrite(static_cast<uint32_t>(state.AlgoID));
	NPWrite(static_cast<uint32_t>(state.ID));

	bool success = NPRead<uint8>() == 1;
	if (success)
	{
		algoStateInfos.Remove(state.ID);
		UE_LOG(LogJMarkovJunior, Log, TEXT("DestroyAlgoState(#%i) completed"), state.ID);
		state.ID = 0;
	}
	else
	{
		UE_LOG(LogJMarkovJunior, Error, TEXT("DestroyAloState(#%i) failed!"), state.ID);
	}
}

bool UJmjProcessManager::StepAlgorithm(FJmjAlgoState state, int count)
{
	check(IsInGameThread());
	if (!IsOurClientConnected())
	{
		UE_LOG(LogJMarkovJunior, Error,
			   TEXT("DestroyAlgorithm(): We aren't currently connected to the JMJ process, "
					  "so no operations can be performed"));
		return false;
	}
	
	NPWrite(uint32_t{ 5 });
	NPWrite(static_cast<uint32_t>(state.AlgoID));
	NPWrite(static_cast<uint32_t>(state.ID));
	NPWrite(static_cast<uint32>(count));
	
	bool success = NPRead<uint8>() == 1;
	if (success)
	{
		bool isFinished = NPRead<uint8>() != 0;
		return isFinished;
	}
	else
	{
		UE_LOG(LogJMarkovJunior, Error, TEXT("StepAlgorithm(#%i) failed!"), state.ID);
		return true;
	}
}
void UJmjProcessManager::FinishAlgorithm(FJmjAlgoState state)
{
	check(IsInGameThread());
	if (!IsOurClientConnected())
	{
		UE_LOG(LogJMarkovJunior, Error,
			   TEXT("DestroyAlgorithm(): We aren't currently connected to the JMJ process, "
					  "so no operations can be performed"));
		return;
	}
	
	NPWrite(uint32_t{ 6 });
	NPWrite(static_cast<uint32_t>(state.AlgoID));
	NPWrite(static_cast<uint32_t>(state.ID));

	bool success = NPRead<uint8>() == 1;
	if (success)
	{
		algoStateInfos[state.ID].StillRunning = false;
	}
	else
	{
		UE_LOG(LogJMarkovJunior, Error, TEXT("FinishAlgorithm(#%i) failed!"), state.ID);
	}
}
bool UJmjProcessManager::CheckAlgorithmFinished(FJmjAlgoState state)
{
	check(IsInGameThread());
	if (!IsOurClientConnected())
	{
		UE_LOG(LogJMarkovJunior, Error,
			   TEXT("DestroyAlgorithm(): We aren't currently connected to the JMJ process, "
					  "so no operations can be performed"));
		return false;
	}
	
	//We actually cache this locally.
	const auto* info = algoStateInfos.Find(state.ID);
	if (!info)
	{
		UE_LOG(LogJMarkovJunior, Error, TEXT("FinishAlgorithm(#%i) failed!"), state.ID);
		return true;
	}

	return !info->StillRunning;
}

void UJmjProcessManager::DownloadGrid(FJmjAlgoState state,
									  TArray<int>& outResolution,
									  TArray<uint8>& outValues)
{
	check(IsInGameThread());
	outResolution.Empty();
	outValues.Empty();

	NPWrite(uint32_t{ 8 });
	NPWrite(static_cast<uint32_t>(state.ID));

	bool success = NPRead<uint8>() == 1;
	if (success)
	{
		int nDims = NPRead<uint32_t>();
		check(nDims == algoStateInfos[state.ID].NDims);

		outResolution.SetNumUninitialized(nDims, EAllowShrinking::No);
		for (int& r : outResolution)
			r = static_cast<int>(NPRead<uint32_t>());

		outValues.SetNumUninitialized(Algo::Accumulate(outResolution, 1, std::multiplies<int>{ }));
		NPRead(std::span{ outValues.GetData(), static_cast<size_t>(outValues.Num()) });
	}
	else
	{
		UE_LOG(LogJMarkovJunior, Verbose, TEXT("DownloadGrid(#%i) succeeded"), state.ID);
	}
}
FJmjIntVector2D UJmjProcessManager::DownloadGrid2D(FJmjAlgoState state, TArray<uint8>& outValues)
{
	TArray<int> resolution;
	DownloadGrid(state, resolution, outValues);

	if (resolution.Num() != 2)
	{
		UE_LOG(LogJMarkovJunior, Error, TEXT("Expected a 2D grid but it was %iD"), resolution.Num());
		return { 0, 0 };
	}
	return { resolution[0], resolution[1] };
}
FIntVector UJmjProcessManager::DownloadGrid3D(FJmjAlgoState state, TArray<uint8>& outValues)
{
	TArray<int> resolution;
	DownloadGrid(state, resolution, outValues);

	if (resolution.Num() != 3)
	{
		UE_LOG(LogJMarkovJunior, Error, TEXT("Expected a 3D grid but it was %iD"), resolution.Num());
		return { 0, 0, 0 };
	}
	return { resolution[0], resolution[1], resolution[2] };
}

void UJmjProcessManager::InitialIPCHandshake()
{
	//TODO: User setting to change the client name 
	static std::string clientName = "UnrealDefaultPlugin";
	
	NPWrite(static_cast<uint32>(clientName.size()));
	NPWrite(clientName.data(), clientName.size());
}
void UJmjProcessManager::FlushStderr()
{
	if (stderrFromHere == nullptr)
		return;
		
	//Drain the pipe.
	int firstNewByte = stderrBuffer.Num();
	pipeInnerBuffer.Empty();
	FPlatformProcess::ReadPipeToArray(stderrFromHere, pipeInnerBuffer);
	stderrBuffer.Append(pipeInnerBuffer);
	int nNewBytes = stderrBuffer.Num() - firstNewByte;

	if (nNewBytes < 1)
		return;

	//For each newline, log that line in Unreal.
	int lastNewline = firstNewByte - 1;
	auto* foundNewline = Algo::Find(
		std::span{ &stderrBuffer[lastNewline + 1], static_cast<size_t>(stderrBuffer.Num() - lastNewline) },
		static_cast<uint8>('\n') //Apparently OK to do this in UTF-8
	);
	while (foundNewline)
	{
		//Copy the line's bytes into a real FString.
		int newlineIdx = foundNewline - stderrBuffer.GetData(),
			lineLength = newlineIdx - lastNewline; //In bytes, so this is an upper bound on char count
		stderrStringBuffer.Reset(FMath::Max(stderrStringBuffer.Len(), lineLength));
		stderrStringBuffer.AppendChars(
			reinterpret_cast<const UTF8CHAR*>(&stderrBuffer[lastNewline + 1]),
			lineLength
		);

		UE_LOG(LogJMarkovJunior, Log, TEXT("<IPC> %s"), *stderrStringBuffer);

		//Move on to the next line.
		lastNewline = newlineIdx;
		foundNewline = (lastNewline == stderrBuffer.Num() - 1) ?
			nullptr :
			Algo::Find(
				std::span{ &stderrBuffer[lastNewline + 1], static_cast<size_t>(stderrBuffer.Num() - lastNewline) },
				static_cast<uint8>('\n')
			);
	}

	//Clear the buffer up to the last newline.
	stderrBuffer.RemoveAt(0, lastNewline + 1);
}
