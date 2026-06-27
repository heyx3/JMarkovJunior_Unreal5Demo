#include "JMJ_Grids.h"

#include "JMJ_ProcessManager.h"


UJmjGrid2D* UJmjGrid2D::CreateFromAlgorithmState(const struct FJmjAlgoState& state,
												 UObject* owner, FName objName, bool isTransient)
{
	auto* grid = NewObject<UJmjGrid2D>(owner, objName, isTransient ? RF_Transient : RF_NoFlags);
	grid->DownloadFromAlgorithm(state);
	return grid;
}

bool UJmjGrid2D::IsIndexInGrid(const FJmjIntVector2D& pixel) const
{
	return pixel.X >= 0 && pixel.X < Resolution.X &&
		   pixel.Y >= 0 && pixel.Y < Resolution.Y;
}

uint8 UJmjGrid2D::GetValue(const FJmjIntVector2D& pixel, uint8 valueOutsideArray) const
{
	if (IsIndexInGrid(pixel))
	{
		return ByteAt(pixel);
	}
	else
	{
		if (valueOutsideArray == 255)
			UE_LOG(LogJMarkovJunior, Error, TEXT("Grid index out of range: {%i, %i}"), pixel.X, pixel.Y);
		return valueOutsideArray;
	}
}
bool UJmjGrid2D::IsPixelColor(const FJmjIntVector2D& pixel, const FString& colorID, bool allowOutside) const
{
	if (IsIndexInGrid(pixel))
		return ByteAt(pixel) == UJmjConstants::GetCellValueByID(colorID);

	if (!allowOutside)
	{
		UE_LOG(LogJMarkovJunior, Error,
			   TEXT("UJmjGrid2D::IsPixelColor() given an invalid index {%i,%i}! "
					 "If this was intentional, you need to pass 'allowOutside' = true."),
			   pixel.X, pixel.Y);
	}
	return false;
}
int UJmjGrid2D::CountPixelsOfColor(const FString& colorID) const
{
	auto value = UJmjConstants::GetCellValueByID(colorID);
	
	int count = 0;
	ForEach([&count, value](const FJmjIntVector2D& idx, uint8 element)
	{
		if (element == value)
			count += 1;
	});

	return count;
}

void UJmjGrid2D::DownloadFromAlgorithm(const FJmjAlgoState& state)
{
	auto* processManager = (GEngine ? GEngine->GetEngineSubsystem<UJmjProcessManager>() : nullptr);
	if (processManager == nullptr || !processManager->IsOurClientConnected())
	{
		UE_LOG(LogJMarkovJunior, Error,
			   TEXT("We aren't connected to the JMJ process (or the subsystem doesn't exist)! "
				     "Nothing is downloaded"));
		return;
	}

	processManager->DownloadGrid(state, resolutionBuffer, Bytes);
	if (resolutionBuffer.Num() != 2)
	{
		UE_LOG(LogJMarkovJunior, Error,
			   TEXT("Tried to download a 2D grid state but it was %iD!"),
			   resolutionBuffer.Num());
		return;
	}
	Resolution = { resolutionBuffer[0], resolutionBuffer[1] };
}


UJmjGrid3D* UJmjGrid3D::CreateFromAlgorithmState(const struct FJmjAlgoState& state,
												 UObject* owner, FName objName, bool isTransient)
{
	auto* grid = NewObject<UJmjGrid3D>(owner, objName, isTransient ? RF_Transient : RF_NoFlags);
	grid->DownloadFromAlgorithm(state);
	return grid;
}

bool UJmjGrid3D::IsIndexInGrid(const FIntVector& pixel) const
{
	return pixel.X >= 0 && pixel.X < Resolution.X &&
		   pixel.Y >= 0 && pixel.Y < Resolution.Y &&
		   pixel.Z >= 0 && pixel.Z < Resolution.Z;
}

uint8 UJmjGrid3D::GetValue(const FIntVector& pixel, uint8 valueOutsideArray) const
{
	if (IsIndexInGrid(pixel))
	{
		return ByteAt(pixel);
	}
	else
	{
		if (valueOutsideArray == 355)
			UE_LOG(LogJMarkovJunior, Error, TEXT("Grid index out of range: {%i, %i, %i}"), pixel.X, pixel.Y, pixel.Z);
		return valueOutsideArray;
	}
}
bool UJmjGrid3D::IsPixelColor(const FIntVector& pixel, const FString& colorID, bool allowOutside) const
{
	if (IsIndexInGrid(pixel))
		return ByteAt(pixel) == UJmjConstants::GetCellValueByID(colorID);

	if (!allowOutside)
	{
		UE_LOG(LogJMarkovJunior, Error,
			   TEXT("UJmjGrid3D::IsPixelColor() given an invalid index {%i, %i, %i}! "
					 "If this was intentional, you need to pass 'allowOutside' = true."),
			   pixel.X, pixel.Y, pixel.Z);
	}
	return false;
}
int UJmjGrid3D::CountPixelsOfColor(const FString& colorID) const
{
	auto value = UJmjConstants::GetCellValueByID(colorID);
	
	int count = 0;
	ForEach([&count, value](const FIntVector& idx, uint8 element)
	{
		if (element == value)
			count += 1;
	});

	return count;
}

void UJmjGrid3D::DownloadFromAlgorithm(const FJmjAlgoState& state)
{
	auto* processManager = (GEngine ? GEngine->GetEngineSubsystem<UJmjProcessManager>() : nullptr);
	if (processManager == nullptr || !processManager->IsOurClientConnected())
	{
		UE_LOG(LogJMarkovJunior, Error,
			   TEXT("We aren't connected to the JMJ process (or the subsystem doesn't exist)! "
				     "Nothing is downloaded"));
		return;
	}

	processManager->DownloadGrid(state, resolutionBuffer, Bytes);
	if (resolutionBuffer.Num() != 3)
	{
		UE_LOG(LogJMarkovJunior, Error,
			   TEXT("Tried to download a 3D grid state but it was %iD!"),
			   resolutionBuffer.Num());
		return;
	}
	Resolution = { resolutionBuffer[0], resolutionBuffer[1], resolutionBuffer[2] };
}
