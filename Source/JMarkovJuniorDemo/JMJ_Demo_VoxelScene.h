#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Components/InstancedStaticMeshComponent.h"

#include "JMJ_Constants.h"
#include "JMJ_ProcessManagement.h"
#include "JMJ_Demo_VoxelScene.generated.h"


//The unscaled bounds of this actor is {0, 0, 0} - {Resolution...}.
//Note that is Unreal units -- cm.
UCLASS(BlueprintType)
class JMARKOVJUNIORDEMO_API AJmjDemoVoxelScene : public AActor
{
	GENERATED_BODY()
public:

	//Maps each cell type (as its name or char) to a Material.
	//You can map multiple cell types to the same Material, in which case they are grouped together.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TMap<FString, UMaterialInterface*> CellMaterials;

	//The algorithm used to generate the scene.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(MultiLine))
	FString AlgorithmSrc = TEXT("@markovjunior begin\nend");
	//The resolution of the voxels.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FIntVector Resolution = { 32, 32, 32 };
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int Seed = 654332;

    //Is set to null once generation is done and the voxels exist.
    UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly, Transient)
    UMarkovJuniorAlgorithm* AlgorithmParsed = nullptr;
    //Is set to null once generation is done and the voxels exist.
    UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly, Transient)
    UMarkovJuniorRunState* AlgorithmState = nullptr;
	UPROPERTY(BlueprintReadOnly, EditInstanceOnly, Transient)
	TArray<UInstancedStaticMeshComponent*> ISMsByCellValue;
	UPROPERTY(BlueprintReadOnly, EditInstanceOnly, Transient)
	TMap<UMaterialInterface*, UInstancedStaticMeshComponent*> ISMsByMaterial;


	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsStillGenerating() const { return IsValid(AlgorithmState); }
	

	AJmjDemoVoxelScene();
	virtual void BeginPlay() override;
	virtual void Tick(float deltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type reason) override;

private:

	void MeshGrid(const TArray<uint8>& pixels, const FIntVector& resolution);
	
	UPROPERTY()
	UStaticMesh* EngineCubeMesh;
};