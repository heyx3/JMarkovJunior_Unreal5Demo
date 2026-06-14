#include "JMJ_ProcessManager.h"

#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "Windows/WindowsPlatformNamedPipe.h"

#include "jmj_consts.hpp"
#include "jmj_ipc.hpp"


namespace
{
	template<typename T>
	void NPWrite(FGenericPlatformNamedPipe& pipe, const T& value, int count = 1)
	{
		pipe.WriteBytes(sizeof(T) * count, &value);
	}
	template<typename T>
	T NPRead(FGenericPlatformNamedPipe& pipe)
	{
		T output;
		verify(pipe.ReadBytes(sizeof(T), &output));
		return output;
	}
}

std::span<FJmjCellType> UJmjConstants::GetCellTypes()
{
	static auto lookup = []() {
		std::array<FJmjCellType, jmj::NGridValues> output;
		for (size_t i = 0; i < jmj::NGridValues; i++)
		{
			TCHAR charStr[] = { CharCast<TCHAR>(jmj::GridChars[i]), static_cast<TCHAR>(0) };
			output[i] = {
				{ jmj::GridColors[i][0], jmj::GridColors[i][1], jmj::GridColors[i][2] },
				charStr,
				StringCast<TCHAR>(jmj::GridNames[i].data()).Get()
			};
		}
		return output;
	}();
	return std::span{ lookup };
}
const FString& UJmjConstants::GetBasicMaze()
{
	static FString maze = StringCast<TCHAR>(jmj::BasicMaze.data()).Get();
	return maze;
}

void UJmjProcessManager::Initialize(FSubsystemCollectionBase& collection)
{
	//Note: the apparent point of `collection` is to allow us
	//  to force dependent subsystems to initialize first.
	
	Super::Initialize(collection);
	
	check(procState == ProcState::Uninitialized);
	check(stdoutBuffer.IsEmpty());
	check(stderrBuffer.IsEmpty());

	FString processPath = FPaths::Combine(
		IPluginManager::Get().FindPlugin(TEXT("JMarkovJunior"))->GetBaseDir(),
		TEXT("Binaries/ThirdParty/JMJ/bin/JMarkovJunior_IPC.exe")
	);
	pipeName = StringCast<TCHAR>(jmj::ipc::NamedPipe.data()).Get();

	//CreatePipe() initializes the two sides of one pipe between us and child process.
	FPlatformProcess::CreatePipe(stdoutFromHere, stdoutFromChild, false);
	FPlatformProcess::CreatePipe(stderrFromHere, stderrFromChild, false);

	UE_LOG(LogJMarkovJunior, Log, TEXT("Starting IPC process..."));
	process = FPlatformProcess::CreateProc(
		GetData(processPath),
		TEXT("-k --julia-args -t auto"), //TODO: Support configuration of params (both IPC and JRT) through project settings
		false, false, false,
		nullptr, 0, nullptr,
		stdoutFromChild, nullptr, stderrFromChild
	);
	procState = ProcState::Booting;
	UE_LOG(LogJMarkovJunior, Log, TEXT("\tProcess Started"));
}
void UJmjProcessManager::Deinitialize()
{
	if (process.IsValid())
		FPlatformProcess::TerminateProc(process);
	
	procState = ProcState::Dead;
	CleanUpProcessHandles();
	
	Super::Deinitialize();
}

void UJmjProcessManager::Tick(float deltaSeconds)
{
	SecondsToNextProcessPoll -= deltaSeconds;
	if (SecondsToNextProcessPoll <= 0)
	{
		PollProcess();
		SecondsToNextProcessPoll = ProcessPollIntervalSeconds;
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
	auto flushStderr = [&]()
	{
		if (stderrFromHere == nullptr)
			return;
		
		//Drain the pipe.
		int firstNewByte = stderrBuffer.Num();
		FPlatformProcess::ReadPipeToArray(stderrFromHere, stderrBuffer);
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
			stderrString.Reset(FMath::Max(stderrString.Len(), lineLength));
			stderrString.AppendChars(
				reinterpret_cast<const UTF8CHAR*>(&stderrBuffer[lastNewline + 1]),
				lineLength
			);

			UE_LOG(LogJMarkovJunior, Log, TEXT("<IPC> %s"), *stderrString);

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
	};
	
	switch (procState)
	{
		case ProcState::Uninitialized:
			//It can happen that we tick before initialization.
			//Just ignore it.
		break;
	
		case ProcState::Booting:
			flushStderr();
		
			//Look for the "ready for clients" signal.
			FPlatformProcess::ReadPipeToArray(stdoutFromHere, stdoutBuffer);
			//  (note: the above function returns whether any new bytes were read in)
			if (stdoutBuffer.Num() >= 4)
			{
				auto code = *reinterpret_cast<uint32*>(stdoutBuffer.GetData());
				stdoutBuffer.RemoveAt(0, 4, EAllowShrinking::No);
				checkf(code == jmj::ipc::StdoutStartCode,
					   TEXT("Got JMJ startup code %u instead of %u"),
					   code, jmj::ipc::StdoutStartCode);

				procState = ProcState::Ready;
				UE_LOG(LogJMarkovJunior, Log, TEXT("IPC process is ready for clients at %s"), *pipeName);

				namedPipe = MakeUnique<FWindowsPlatformNamedPipe>();
				if (!namedPipe->Create(pipeName, false, false))
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
			flushStderr();
			//Look for the "no more clients" signal.
			FPlatformProcess::ReadPipeToArray(stdoutFromHere, stdoutBuffer);
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
			flushStderr();
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

void UJmjProcessManager::ParseAlgorithm(const FString& sourceCode,
										bool& succeeded, FString& outErrMsg, FJmjParsedAlgo& outAlgo)
{
	NPWrite(*namedPipe, uint32_t{ 1 });
	//TODO: Implement
	check(false);
}
