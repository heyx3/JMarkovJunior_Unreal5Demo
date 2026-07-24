#include "JMJ_Demo_Buildings2D.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Kismet/KismetMathLibrary.h"


FJmjParsedAlgo AJmjBuilding2DSchema::GetAlgorithmHandle()
{
	if (algorithmHandle.ID == 0)
	{
		auto* jmjProcess = GEngine->GetEngineSubsystem<UJmjProcessManager>();
		FString errMsg;
		if (!jmjProcess->ParseAlgorithm(AlgorithmSrc, errMsg, algorithmHandle))
		{
			UE_LOG(LogJMarkovJunior, Error, TEXT("Failed to parse algorithm from %s: %s"),
				   *GetName(), *errMsg);
		}
	}

	return algorithmHandle;
}
void AJmjBuilding2DSchema::BeginDestroy()
{
	if (algorithmHandle.ID != 0)
	{
		auto* jmjProcess = GEngine->GetEngineSubsystem<UJmjProcessManager>();
		jmjProcess->DestroyAlgorithm(algorithmHandle);
	}
	
	Super::BeginDestroy();
}

AJmjBuilding2D::AJmjBuilding2D()
{
	PrimaryActorTick.bCanEverTick = true;
	
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root Component"));
	
	EditorViz = CreateDefaultSubobject<UJmjBuilding2DEditorViz>(TEXT("Editor Bounds Viz"));
	

	static ConstructorHelpers::FObjectFinder<UStaticMesh> engineCubeFinder(TEXT("StaticMesh'/Engine/BasicShapes/Cube.Cube'"));
	auto* engineCube = engineCubeFinder.Object.Get();
	auto setupIsm = [&](UInstancedStaticMeshComponent*& handle, const TCHAR* name)
	{
		handle = CreateDefaultSubobject<UInstancedStaticMeshComponent>(name);
		handle->SetupAttachment(RootComponent);
		handle->SetStaticMesh(engineCube);
		handle->SetCollisionEnabled(ECollisionEnabled::Type::QueryAndPhysics);
		handle->SetCollisionObjectType(ECC_WorldStatic);
		return handle;
	};
	setupIsm(VoxelsMetalBody, TEXT("Voxels: Metal Body"));
	setupIsm(VoxelsMetalDetails, TEXT("Voxels: Metal Details"));
	setupIsm(VoxelsSignalLights, TEXT("Voxels: Signal Lights"));
	setupIsm(VoxelsWindowLights, TEXT("Voxels: Window Lights"));
}
void AJmjBuilding2D::BeginPlay()
{
	Super::BeginPlay();

	if (IsValid(BuildingSchema))
	{
		auto* jmjProcess = GEngine->GetEngineSubsystem<UJmjProcessManager>();
		jmjProcess->StartAlgorithm(BuildingSchema->GetAlgorithmHandle(),
								   { FMath::Max(BuildingResolutionHorizontal, 4),
								     FMath::Min(BuildingResolutionVertical, 4096) },
								  { (Seed == 0) ? FMath::Rand() : Seed },
								  AlgoTicksPerSecond, AlgorithmState);
	}
}
void AJmjBuilding2D::BeginDestroy()
{
	if (AlgorithmState.ID != 0)
	{
		auto* jmjProcess = GEngine->GetEngineSubsystem<UJmjProcessManager>();
		jmjProcess->DestroyAlgoState(AlgorithmState);
	}
	
	Super::BeginDestroy();
}
void AJmjBuilding2D::Tick(float deltaSeconds)
{
	Super::Tick(deltaSeconds);

	if (!IsFinishedGenerating && AlgorithmState.ID != 0)
	{
		auto* jmjProcess = GEngine->GetEngineSubsystem<UJmjProcessManager>();
		if (jmjProcess->CheckAlgorithmFinished(AlgorithmState))
		{
			static TArray<uint8> voxelBytes;
			static TArray<int> voxelResolution;
			jmjProcess->DownloadGrid(AlgorithmState, voxelResolution, voxelBytes);
			check(voxelResolution.Num() == 2);

			IsFinishedGenerating = true;
			MeshVoxels(voxelBytes, { voxelResolution[0], voxelResolution[1] });
		}
	}
}
void AJmjBuilding2D::MeshVoxels(const TArray<uint8>& bytes, const FIntPoint& resolution)
{
	//Allocate buffers for the instance transforms.
	static TArray<FTransform> trMetalBody, trMetalDetails, trSignalLights, trWindowLights;
	trMetalBody.Reset(); trMetalDetails.Reset(); trSignalLights.Reset(); trWindowLights.Reset();

	//Compute transform math for the voxels.
	auto texelSize = FVector::One() / FVector(resolution.X, resolution.X, resolution.Y);
	FVector sceneOffset{
		-FVector2D{
			static_cast<double>(resolution.X),
			static_cast<double>(resolution.X)
		} / 2,
		0.0
	};
	auto getVoxelTr = [&](FIntVector3 areaMin, FIntVector3 areaMax) -> FTransform
	{
		//Flip the vertical axis.
		auto mi = areaMin,
			 ma = areaMax;
		areaMin.Z = resolution.Y - 1 - ma.Z;
		areaMax.Z = resolution.Y - 1 - mi.Z;
		
		auto areaCenter = FVector{ areaMax + areaMin } / 2.0;
		return UKismetMathLibrary::ComposeTransforms(
			//Transform the engine cube to be a unit cube centered on 0:
		 {
		 		FQuat::Identity,
		 		FVector::ZeroVector,
		 		FVector::OneVector / 100
			},
			//Move the unit cube into its local voxel region:
			{
				FQuat::Identity,
				(areaCenter + sceneOffset) * texelSize,
				static_cast<FVector>(areaMax + FIntVector{ 1, 1, 1 } - areaMin) * texelSize
			}
		);
	};

	//Gather and submit voxels.
	//TODO: Greedy-meshing
	auto cMetalBody = UJmjConstants::GetCellValueByID(BuildingSchema->AlgorithmCharMetalBody),
		 cMetalDetails = UJmjConstants::GetCellValueByID(BuildingSchema->AlgorithmCharMetalDetails),
		 cSignalLights = UJmjConstants::GetCellValueByID(BuildingSchema->AlgorithmCharSignalLights),
		 cWindowLights = UJmjConstants::GetCellValueByID(BuildingSchema->AlgorithmCharWindowLights);
	for (int y = 0; y < resolution.Y; ++y)
	{
		for (int x = 0; x < resolution.X; ++x)
		{
			int i = x + (resolution.X * y);
			
			TArray<FTransform>* instanceList = nullptr;
			if (bytes[i] == cMetalBody)
				instanceList = &trMetalBody;
			else if (bytes[i] == cMetalDetails)
				instanceList = &trMetalDetails;
			else if (bytes[i] == cSignalLights)
				instanceList = &trSignalLights;
			else if (bytes[i] == cWindowLights)
				instanceList = &trWindowLights;

			if (instanceList == &trSignalLights)
				instanceList->Add(getVoxelTr({ x, resolution.X/2, y }, { x, resolution.X/2, y }));
			else if (instanceList)
				instanceList->Add(getVoxelTr({ x, 0, y }, { x, resolution.X-1, y }));
		}
	}

	//Upload the mesh.
	VoxelsMetalBody->AddInstances(trMetalBody, false);
	VoxelsMetalDetails->AddInstances(trMetalDetails, false);
	VoxelsSignalLights->AddInstances(trSignalLights, false);
	VoxelsWindowLights->AddInstances(trWindowLights, false);

	OnDoneGenerating.Broadcast(this);
}
