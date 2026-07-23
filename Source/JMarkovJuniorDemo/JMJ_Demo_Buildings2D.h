#pragma once

#include "CoreMinimal.h"
#include "JMJ_ProcessManager.h"
#include "JMJ_Demo_Buildings2D.generated.h"


//Represents a specific kind of MarkovJunior-generated 2D building.
UCLASS(BlueprintType, Blueprintable)
class AJmjBuilding2DSchema : public AInfo
{
	GENERATED_BODY()
public:

	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly, meta=(MultiLine))
	FString AlgorithmSrc = TEXT("@markovjunior 'T' begin; end");
	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly)
	FString AlgorithmCharMetalBody = TEXT("g");
	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly)
	FString AlgorithmCharMetalDetails = TEXT("b");
	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly)
	FString AlgorithmCharSignalLights = TEXT("R");
	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly)
	FString AlgorithmCharWindowLights = TEXT("Y");
	
	UFUNCTION(BlueprintCallable, BlueprintPure)
	FJmjParsedAlgo GetAlgorithmHandle();

	virtual void BeginDestroy() override;

protected:
	UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly, Transient)
	FJmjParsedAlgo algorithmHandle;
};

//Runs a 2D MarkovJunior algorithm to generate a building profile,
//    then extrapolates it into a 3D voxel scene.
//
//The building keeps its origin at the bottom-center of the voxel scene, and its unscaled bounds at {1, 1, height/width}.
UCLASS(BlueprintType, Blueprintable)
class AJmjBuilding2D : public AActor
{
	GENERATED_BODY()
public:

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	UInstancedStaticMeshComponent* VoxelsMetalBody;
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	UInstancedStaticMeshComponent* VoxelsMetalDetails;
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	UInstancedStaticMeshComponent* VoxelsSignalLights;
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	UInstancedStaticMeshComponent* VoxelsWindowLights;

	//What kind of building to generate.
	UPROPERTY(BlueprintReadOnly, EditAnywhere)
	AJmjBuilding2DSchema* BuildingSchema = nullptr;
	UPROPERTY(BlueprintReadOnly, EditAnywhere)
	int BuildingResolutionHorizontal = 8;
	UPROPERTY(BlueprintReadOnly, EditAnywhere)
	int BuildingResolutionVertical = 32;
	UPROPERTY(BlueprintReadOnly, EditAnywhere)
	float AlgoTicksPerSecond = 1000;
	//Set on spawn, or leave at 0 to randomize.
	UPROPERTY(BlueprintReadOnly, EditAnywhere)
	int Seed = 0;

	UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly, Transient)
	bool IsFinishedGenerating = false;

	AJmjBuilding2D();
	virtual void BeginPlay() override;
	virtual void BeginDestroy() override;
	virtual void Tick(float deltaSeconds) override;

protected:

	UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly, Transient)
	FJmjAlgoState AlgorithmState;

private:

	void MeshVoxels(const TArray<uint8>& bytes, const FIntPoint& resolution);
};