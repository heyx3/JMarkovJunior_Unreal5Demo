#pragma once

#include "CoreMinimal.h"

#include "JMarkovJuniorDemo.generated.h"


UCLASS(BlueprintType)
class JMARKOVJUNIORDEMO_API UJmjDemoUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, BlueprintPure)
	static FBox BoundingBoxForPoints(const TArray<FVector>& points);
};