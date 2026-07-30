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

	FVector2D ToVf() const { return { static_cast<double>(X), static_cast<double>(Y) }; }
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

	static constexpr int NCellTypes = 16; //static_assert in cpp file keeps this correct

	//All possible kinds of cells, ordered by their value.
	static std::span<const FJmjCellType> GetCellTypes();
	// (implementation note: the JMJ grid is deliberately 0-based even though Julia is 1-based,
	//    so these lookups work as expected!)

	//Finds a cell's value based on its char or full name.
	UFUNCTION(BlueprintCallable, BlueprintPure)
	static uint8 GetCellValueByID(const FString& id);
	//Finds a cell's value based on its char.
	static uint8 GetCellValueByID(TCHAR id);
	
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

//A set of JMarkovJunior cell types.
//
//Highly optimized into nothing but a UInt16!
//And iteration through it happens in a deterministic order.
USTRUCT(BlueprintType)
struct FJmjCellSet
{
	GENERATED_BODY()
public:

	uint16 Bits = 0;

	template<typename I>
	requires std::integral<I> && (!std::same_as<I, char>) && (!std::same_as<I, wchar_t>) && (!std::same_as<I, char16_t>) && (!std::same_as<I, char32_t>)
	bool Contains(I cellValue) const
	{
		return (Bits & static_cast<uint16>(uint16{ 1 } << static_cast<uint16>(cellValue))) != uint16{ 0 };
	}
	bool Contains(TCHAR cellChar) const { return Contains(UJmjConstants::GetCellValueByID(cellChar)); }
	bool Contains(const FString& nameOrChar) const { return Contains(UJmjConstants::GetCellValueByID(nameOrChar)); }

	FJmjCellSet Union(FJmjCellSet s2) const { return { static_cast<uint16>(
		Bits | s2.Bits
	) }; }
	template<typename I>
	requires std::integral<I> && (!std::same_as<I, char>) && (!std::same_as<I, wchar_t>) && (!std::same_as<I, char16_t>) && (!std::same_as<I, char32_t>)
	FJmjCellSet Union(I cellValue) const { return Union(FJmjCellSet{ static_cast<uint16>(1 << cellValue) }); }
	FJmjCellSet Union(TCHAR cellChar) const { return Union(UJmjConstants::GetCellValueByID(cellChar)); }
	FJmjCellSet Union(const FString& cellNameOrChar) const { return Union(UJmjConstants::GetCellValueByID(cellNameOrChar)); }

	FJmjCellSet Difference(FJmjCellSet s2) const { return { static_cast<uint16>(
		Bits & static_cast<uint16>(~s2.Bits)
	) }; }
	template<typename I>
	requires std::integral<I> && (!std::same_as<I, char>) && (!std::same_as<I, wchar_t>) && (!std::same_as<I, char16_t>) && (!std::same_as<I, char32_t>)
	FJmjCellSet Difference(I cellValue) const { return Difference(FJmjCellSet{ static_cast<uint16>(1 << cellValue) }); }
	FJmjCellSet Difference(TCHAR cellChar) const { return Difference(UJmjConstants::GetCellValueByID(cellChar)); }
	FJmjCellSet Difference(const FString& cellNameOrChar) const { return Difference(UJmjConstants::GetCellValueByID(cellNameOrChar)); }
	
	FJmjCellSet Intersection(FJmjCellSet s2) const { return { static_cast<uint16>(
		Bits & s2.Bits
	) }; }

	//Iterates over every value in this set and invokes your lambda on it,
	//   passing the value by copy.
	//
	//To interrupt the loop, have your lambda return true
	//  (then this function returns whether the interrupt occurred).
	template<typename ToDo>
	auto ForEachElement(ToDo&& func) const
	{
		constexpr bool ReturnsBool = std::is_same_v<bool, std::invoke_result_t<ToDo, FJmjIntVector2D, uint8&>>;

		for (uint16 i = 0; i < UJmjConstants::NCellTypes; ++i)
		{
			if (!Contains(i))
				return;
			if constexpr (ReturnsBool)
			{
				if (std::invoke(func, i))
					return true;
			}
			else
			{
				std::invoke(func, i);
			}
		}

		if constexpr (ReturnsBool)
			return false;
	}

	bool operator==(FJmjCellSet s2) const { return Bits == s2.Bits; }
	bool Serialize(FArchive& ar) { ar << Bits; return true; }
};
inline FArchive& operator<<(FArchive& ar, FJmjCellSet s) { s.Serialize(ar); return ar; }
template<>
struct TStructOpsTypeTraits<FJmjCellSet> : public TStructOpsTypeTraitsBase2<FJmjCellSet>
{
	enum
	{
		WithZeroConstructor = true,
		WithNoDestructor = true,
		WithIdenticalViaEquality = true,
		WithSerializer = true
	};
};
inline uint32 GetTypeHash(FJmjCellSet s) { return GetTypeHash(s.Bits); }
UCLASS(BlueprintType)
class UJmjCellSetFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, BlueprintPure)
	static FJmjCellSet NewJmJCellSetFromIDs(const TArray<FString>& ids)
	{
		FJmjCellSet set;
		for (const auto& id : ids)
			set = set.Union(id);
		return set;
	}
	UFUNCTION(BlueprintCallable, BlueprintPure)
	static FJmjCellSet NewJmjCellSetFromChars(const FString& chars)
	{
		FJmjCellSet set;
		for (TCHAR c : chars)
			set = set.Union(c);
		return set;
	}

	UFUNCTION(BlueprintCallable, BlueprintPure)
	static bool JmjCellSetContains(FJmjCellSet set, uint8 value) { return set.Contains(value); }
	UFUNCTION(BlueprintCallable, BlueprintPure)
	static bool JmjCellSetContainsByChar(FJmjCellSet set, const FString& charOrID) { return set.Contains(charOrID); }

	UFUNCTION(BlueprintCallable, BlueprintPure)
	static FJmjCellSet UnionJmjCellSet(FJmjCellSet a, FJmjCellSet b)
	{
		return a.Union(b);
	}
	UFUNCTION(BlueprintCallable, BlueprintPure)
	static FJmjCellSet DifferenceJmjCellSet(FJmjCellSet a, FJmjCellSet b)
	{
		return a.Difference(b);
	}
	UFUNCTION(BlueprintCallable, BlueprintPure)
	static FJmjCellSet IntersectJmjCellSet(FJmjCellSet a, FJmjCellSet b)
	{
		return a.Intersection(b);
	}
};