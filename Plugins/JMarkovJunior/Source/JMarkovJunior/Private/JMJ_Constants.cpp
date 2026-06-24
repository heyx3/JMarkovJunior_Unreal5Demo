#include "JMJ_Constants.h"

#include "jmj_consts.hpp"
#include "JMarkovJunior.h"
#include "Algo/AnyOf.h"


std::span<const FJmjCellType> UJmjConstants::GetCellTypes()
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

FLinearColor UJmjConstants::GetCellColor(const FString& id)
{
	static auto lookup = []() {
		TMap<FString, FLinearColor> output;
		for (const auto& type : GetCellTypes())
		{
			output.Add(type.Char, type.Color);
			output.Add(type.Name, type.Color);
			output.Add(type.Name.ToLower(), type.Color);
		}
		return output;
	}();

	auto* found = lookup.Find(id);
	if (found)
		return *found;
	
	UE_LOG(LogJMarkovJunior, Error,
		   TEXT("No cell type found with the ID '%s'! Returning color {0,0,0,0}"), *id);
	return { 0, 0, 0, 0 };
}
FLinearColor UJmjConstants::GetCellColor(uint8 value)
{
	auto lookup = GetCellTypes();
	if (value < lookup.size())
		return lookup[value].Color;

	UE_LOG(LogJMarkovJunior, Error,
		   TEXT("Invalid cell value: %i! Returning color {0,0,0,0}"), static_cast<int>(value));
	return { 0, 0, 0, 0 };
}

FString UJmjConstants::GetCellName(const FString& cellChar)
{
	static auto lookup = []() {
		TMap<FString, FString> output;
		for (const auto& type : GetCellTypes())
			output.Add(type.Char, type.Name);
		return output;
	}();
	
	auto* found = lookup.Find(cellChar);
	if (found)
		return *found;
	
	UE_LOG(LogJMarkovJunior, Error,
		   TEXT("No cell type found with the char '%s'! Returning the name 'ERR'"), *cellChar);
	return TEXT("ERR");
}
TCHAR UJmjConstants::GetCellChar(const FString& cellName)
{
	static auto lookup = []() {
		TMap<FString, TCHAR> output;
		for (const auto& type : GetCellTypes())
			output.Add(type.Name, type.Char[0]);
		return output;
	}();
	
	auto* found = lookup.Find(cellName);
	if (found)
		return *found;
	
	UE_LOG(LogJMarkovJunior, Error,
		   TEXT("No cell type found with the name '%s'! Returning the char '!'"), *cellName);
	return TEXT('!');
}
TCHAR UJmjConstants::GetCellChar(uint8 value)
{
	auto cellTypes = GetCellTypes();
	if (value < cellTypes.size())
		return cellTypes[value].Char[0];
	
	UE_LOG(LogJMarkovJunior, Error,
		   TEXT("No cell type has the value '%i'! Returning the char '!'"), static_cast<int>(value));
	return TEXT('!');
}

const FString& UJmjConstants::GetBasicMaze()
{
	static FString maze = StringCast<TCHAR>(jmj::BasicMaze.data()).Get();
	return maze;
}

FString UJmjConstants::FormatCellGrid(const TArray<uint8>& grid, const TArray<int>& resolution, bool raw)
{
	//Look for errors and edge-cases.
	if (Algo::Accumulate(resolution, 1, [](int a, int b) { return a * b; }) != grid.Num())
	{
		TStringBuilder<64> resolutionStr;
		for (int axis = 0; axis < resolution.Num(); ++axis)
		{
			resolutionStr.Appendf(TEXT("%s%i"),
								  (axis > 0) ? TEXT(", ") : TEXT(""),
								  resolution[axis]);
		}
		UE_LOG(LogJMarkovJunior, Error, TEXT("Cell grid has %i elements but resolution is %s, which implies %i elements"),
			   grid.Num(), resolutionStr.ToString(), Algo::Accumulate(resolution, 0, TPlus{ }));
		return TEXT("[ <ERR: invalid resolution> ]");
	}
	if (Algo::AnyOf(grid, [](uint8 u) { return u >= jmj::NGridValues; }))
	{
		UE_LOG(LogJMarkovJunior, Error, TEXT("Cell grid has some values larger than %i! Those will be written as '!'"),
			   static_cast<int>(jmj::NGridValues));
		//Don't exit early; just write bad chars as '!' below.
	}
	//The main algorithm assumes there's at least one axis and that all axes are non-zero.
	if (grid.IsEmpty())
		return TEXT("[ ]");

	//Pre-calculate the size of each kind of slice (1D, 2D, 3D, etc).
	TArray<int, TInlineAllocator<32>> sliceSizes;
	sliceSizes.SetNumUninitialized(resolution.Num());
	sliceSizes[0] = 1;
	for (int i = 1; i < sliceSizes.Num(); ++i)
		sliceSizes[i] = sliceSizes[i - 1] * resolution[i - 1];
	
	TStringBuilder<1024> sb;
	sb.Append(TEXT("[\n"));

	TArray<int> idx;
	idx.SetNumZeroed(resolution.Num());
	int idxFlat = 0;
	auto advanceIdxAndReturnVisualAxis = [&]() -> int
	{
		//Advance the multi-dimensional index.
		int logicalAxis;
		for (logicalAxis = 0; logicalAxis < resolution.Num(); ++logicalAxis)
		{
			int memoryAxis = logicalAxis;
			if (raw && logicalAxis < 2)
				memoryAxis = (memoryAxis + 1) % 2;

			if (idx[memoryAxis] == resolution[memoryAxis] - 1)
			{
				idx[memoryAxis] = 0;
			}
			else
			{
				idx[memoryAxis] += 1;
				break;
			}
		}

		//If we passed the end of the grid, return an axis of -1.
		if (logicalAxis >= resolution.Num())
		{
			idxFlat = grid.Num();
			return -1;
		}

		//Recalculate the flat index.
		//This can be done a bit more efficiently within the above loop, but it's a pain to figure out.
		idxFlat = 0;
		for (int memoryAxis = 0; memoryAxis < resolution.Num(); ++memoryAxis)
			idxFlat += idx[memoryAxis] * sliceSizes[memoryAxis];

		return logicalAxis;
	};
	while (idxFlat < grid.Num())
	{
		TCHAR c = GetCellChar(grid[idxFlat]);
		sb.Append(&c, 1);

		int movedAxis = advanceIdxAndReturnVisualAxis();
		if (movedAxis == 0)
			sb.Append(TEXT(" "));
		else if (movedAxis == 1)
			sb.Append(TEXT("\n    "));
		else if (movedAxis > 1)
		{
			sb.Append(TEXT(" "));
			for (int sI = 0; sI < movedAxis; ++sI)
				sb.Append(TEXT(";"));
			
			sb.Append(TEXT("\n"));
			if (movedAxis > 2)
				sb.Append(TEXT("\n"));
			sb.Append(TEXT("    "));
		}
	}

	sb.Append(TEXT("]"));
	return sb.ToString();
}
