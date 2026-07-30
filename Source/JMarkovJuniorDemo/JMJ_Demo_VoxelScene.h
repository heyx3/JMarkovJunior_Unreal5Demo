#pragma once

#include "CoreMinimal.h"

#include "JMJ_Constants.h"
#include "JMJ_ProcessManager.h"
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
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(ClampMin=1))
	float AlgorithmTicksPerSecond = 2000.0f;

	
	//Is set to null (handle == 0) once generation is done and the voxels exist.
	UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly, Transient)
	FJmjParsedAlgo AlgorithmParsed;
	//Is set to null (handle == 0) once generation is done and the voxels exist.
	UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly, Transient)
	FJmjAlgoState AlgorithmState;
	UPROPERTY(BlueprintReadOnly, EditInstanceOnly, Transient)
	TArray<UInstancedStaticMeshComponent*> ISMsByCellValue;
	UPROPERTY(BlueprintReadOnly, EditInstanceOnly, Transient)
	TMap<UMaterialInterface*, UInstancedStaticMeshComponent*> ISMsByMaterial;


	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsStillGenerating() const { return !AlgorithmState.IsNull(); }
	

	AJmjDemoVoxelScene();
	virtual void BeginPlay() override;
	virtual void Tick(float deltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type reason) override;

private:

	void MeshGrid(class UJmjGrid3D* grid);
	
	UPROPERTY()
	UStaticMesh* EngineCubeMesh;
};