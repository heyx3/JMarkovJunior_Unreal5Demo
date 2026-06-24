//An Unreal-friendly port of the generated constants file from the core library.

#pragma once

#include "CoreMinimal.h"
#include <span>

#include "JMJ_Constants.generated.h"


USTRUCT(BlueprintType)
struct FJmjIntVector2D
{
	GENERATED_BODY()
public:

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int X = 0;
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int Y = 0;

	bool operator==(const FJmjIntVector2D& v) const { return X == v.X && Y == v.Y; }
};
template<> struct TStructOpsTypeTraits<FJmjIntVector2D> : public TStructOpsTypeTraitsBase2<FJmjIntVector2D>
{
	enum
	{
		WithZeroConstructor = true,
		WithNoDestructor = true,
		WithIdenticalViaEquality = true
	};
};
inline uint32 GetTypeHash(const FJmjIntVector2D& v) { return GetTypeHash(MakeTuple(v.X, v.Y)); }


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
	static std::span<const FJmjCellType> GetCellTypes();
	// (implementation note: the JMJ grid is deliberately 0-based even though Julia is 1-based,
	//    so these lookups work as expected!)

	//Finds a cell's full name based on its identifying char.
	UFUNCTION(BlueprintCallable, BlueprintPure)
	static FString GetCellName(const FString& cellChar);
	//Finds a cell's full name based on its identifying char.
	static FString GetCellName(TCHAR cellChar) { const TCHAR a[] = { cellChar, '\0' }; return GetCellName(FString{ a }); }

	//Finds a cell's char based on its full name.
	static TCHAR GetCellChar(const FString& name);
	//Finds a cell's char based on its byte value.
	static TCHAR GetCellChar(uint8 value);
	
	//Finds a cell's char based on its full name.
	UFUNCTION(BlueprintCallable, BlueprintPure, DisplayName="Get Cell Char")
	static FString GetCellCharAsStr(const FString& name) { const TCHAR a[] = { GetCellChar(name), '\0' }; return { a }; }
	//Finds a cell's char based on its byte value.
	UFUNCTION(BlueprintCallable, BlueprintPure, DisplayName="Get Cell Char by Value")
	static FString GetCellCharAsStrByValue(uint8 value) { const TCHAR a[] = { GetCellChar(value), '\0' }; return { a }; }
	
	//Finds a cell's color based on its identifying char or full name.
	UFUNCTION(BlueprintCallable, BlueprintPure, DisplayName="Get Cell Color by ID")
	static FLinearColor GetCellColor(const FString& id);
	//Finds a cell's color based on its byte value.
	static FLinearColor GetCellColor(uint8 value);

	//A simple maze-generator algorithm that works in any number of dimensions.
	static const FString& GetBasicMaze();

	//Formats the given grid as a nicely-printable array of color chars,
	//   in the style of a Julia multidimensional array.
	//
	//In this style each 2D slice is printed on its own,
	//   then separated across 3D by ';;;',
	//   then each 3D block is separated across 4D by ';;;;', and so on.
	//
	//If you pass 'raw = true' then we match Julia convention by treating the first axis (X) as Row rather than Column;
	//   mainly intended for internal debugging.
	UFUNCTION(BlueprintCallable, BlueprintPure, meta=(Keywords="string"))
	static FString FormatCellGrid(const TArray<uint8>& grid, const TArray<int>& resolution,
								  bool raw = false);

protected:
	UFUNCTION(BlueprintCallable, BlueprintPure)
	static void GetCellTypes(TArray<FJmjCellType>& output)
	{
		output.Empty();
		auto cellTypes = GetCellTypes();
		output.Append(cellTypes.data(), cellTypes.size());
	}
	UFUNCTION(BlueprintCallable, BlueprintPure)
	static FLinearColor GetCellColorByValue(uint8 value) { return GetCellColor(value); }
	UFUNCTION(BlueprintCallable, BlueprintPure)
	static void GetBasicMaze(FString& algoString)
	{
		algoString = GetBasicMaze();
	}
};
