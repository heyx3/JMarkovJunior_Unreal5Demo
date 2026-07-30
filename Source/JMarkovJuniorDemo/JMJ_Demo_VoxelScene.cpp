#include "JMJ_Demo_VoxelScene.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Kismet/KismetMathLibrary.h"

#include "JMJ_Grids.h"


AJmjDemoVoxelScene::AJmjDemoVoxelScene()
{
	PrimaryActorTick.bCanEverTick = true;
	
	static ConstructorHelpers::FObjectFinder<UStaticMesh> engineCubeFinder(TEXT("StaticMesh'/Engine/BasicShapes/Cube.Cube'"));
	EngineCubeMesh = engineCubeFinder.Object.Get();
}
void AJmjDemoVoxelScene::BeginPlay()
{
	Super::BeginPlay();

	//Set up the instanced static meshes.
	ISMsByCellValue.Reset();
	ISMsByCellValue.SetNumZeroed(16);
	for (const auto& [cellID, mat] : CellMaterials)
	{
		auto cellIdx = UJmjConstants::GetCellValueByID(cellID);

		UInstancedStaticMeshComponent* ism;
		if (ISMsByMaterial.Contains(mat))
		{
			ism = ISMsByMaterial[mat];
		}
		else
		{
			ism = NewObject<UInstancedStaticMeshComponent>(this);
			
			ism->SetMobility(EComponentMobility::Type::Movable);
			ism->SetStaticMesh(EngineCubeMesh);
			ism->SetMaterial(0, mat);
			ism->CastShadow = true;
			ism->SetGenerateOverlapEvents(false);
			ism->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

			AddInstanceComponent(ism);
			ism->SetupAttachment(GetRootComponent());
			ism->RegisterComponent();
			
			ISMsByMaterial.Add(mat, ism);
		}

		ISMsByCellValue[cellIdx] = ism;
	}

	//Start running the JMJ algorithm.
	auto* jmjProc = GEngine->GetEngineSubsystem<UJmjProcessManager>();
	check(jmjProc);
	FString parseError;
	if (!jmjProc->ParseAlgorithm(AlgorithmSrc, parseError, AlgorithmParsed))
	{
		UE_LOG(LogJMarkovJunior, Error, TEXT("Failed to compile algorithm from %s: %s"),
			   *GetFullName(), *parseError);
	}
	else
	{
		if (!jmjProc->StartAlgorithm(AlgorithmParsed, { Resolution.X, Resolution.Y, Resolution.Z },
									 { Seed }, AlgorithmTicksPerSecond, AlgorithmState))
		{
			UE_LOG(LogJMarkovJunior, Error, TEXT("Failed to start algorithm from %s"), *GetFullName());
		}
	}
}
void AJmjDemoVoxelScene::EndPlay(const EEndPlayReason::Type reason)
{
	//Clean up JMJ resources.
	auto* jmjProc = GEngine->GetEngineSubsystem<UJmjProcessManager>();
	check(jmjProc);
	if (!AlgorithmState.IsNull())
		jmjProc->DestroyAlgoState(AlgorithmState);
	if (!AlgorithmParsed.IsNull())
		jmjProc->DestroyAlgorithm(AlgorithmParsed);
	
	Super::EndPlay(reason);
}
void AJmjDemoVoxelScene::Tick(float deltaSeconds)
{
	Super::Tick(deltaSeconds);

	if (!AlgorithmState.IsNull())
	{
		auto* jmjProc = GEngine->GetEngineSubsystem<UJmjProcessManager>();
		check(jmjProc);

		if (jmjProc->CheckAlgorithmFinished(AlgorithmState))
		{
			auto* grid = UJmjGrid3D::CreateFromAlgorithmState(AlgorithmState);
			jmjProc->DestroyAlgoState(AlgorithmState);
			jmjProc->DestroyAlgorithm(AlgorithmParsed);
			MeshGrid(grid);
		}
	}
}

void AJmjDemoVoxelScene::MeshGrid(UJmjGrid3D* grid)
{
	//Strings by default are 
	TMap<FString, UMaterialInterface*, FDefaultSetAllocator, FLocKeyMapFuncs<UMaterialInterface*>> cellMaterials;
	for (const auto& [k, v] : CellMaterials)
		cellMaterials.Add(k, v);
	
	//Track cell materials.
	TArray<UMaterialInterface*, TInlineAllocator<UJmjConstants::NCellTypes>> cellTypeMaterials;
	TArray<bool, TInlineAllocator<UJmjConstants::NCellTypes>> isCellTypeOpaque;
	cellTypeMaterials.SetNumUninitialized(UJmjConstants::NCellTypes);
	isCellTypeOpaque.SetNumUninitialized(UJmjConstants::NCellTypes);
	for (int i = 0; i < UJmjConstants::NCellTypes; ++i)
	{
		auto* foundMaterial = cellMaterials.Find(UJmjConstants::GetCellTypes()[i].Char);
		if (!foundMaterial)
		{
			foundMaterial = cellMaterials.Find(UJmjConstants::GetCellTypes()[i].Name);
			if (!foundMaterial)
			{
				foundMaterial = cellMaterials.Find(UJmjConstants::GetCellTypes()[i].Name.ToLower());
			}
		}

		cellTypeMaterials[i] = foundMaterial ? *foundMaterial : nullptr;
		isCellTypeOpaque[i] = foundMaterial && (*foundMaterial)->GetBlendMode() == BLEND_Opaque;
	}
	auto isCellOpaque = [&](const FIntVector& vIdx) { return isCellTypeOpaque[grid->ByteAt(vIdx)]; };

	//Track which cells are irrelevant -- eligible to be taken by any part of the greedy mesher.
	TArray<bool> isCellIrrelevant;
	isCellIrrelevant.SetNumUninitialized(grid->GetBytes().Num());
	grid->ForEach([&](int flatIdx, const FIntVector& vIdx, uint8 val)
	{
		isCellIrrelevant[flatIdx] =
			vIdx.X > 0 && vIdx.Y > 0 && vIdx.Z > 0 &&
			vIdx.X < Resolution.X-1 && vIdx.Y < Resolution.Y-1 && vIdx.Z < Resolution.Z-1 &&
			isCellOpaque({ vIdx.X - 1, vIdx.Y, vIdx.Z }) && isCellOpaque({ vIdx.X + 1, vIdx.Y, vIdx.Z }) &&
			isCellOpaque({ vIdx.X, vIdx.Y - 1, vIdx.Z }) && isCellOpaque({ vIdx.X, vIdx.Y + 1, vIdx.Z }) &&
			isCellOpaque({ vIdx.X, vIdx.Y, vIdx.Z - 1 }) && isCellOpaque({ vIdx.X, vIdx.Y, vIdx.Z + 1 });
	});

	//Set up the coordinate math for placing blocks.
	TMap<UInstancedStaticMeshComponent*, TArray<FTransform>> blockInstances;
	for (const auto [mat, ism] : ISMsByMaterial)
		blockInstances.Emplace(ism);
	auto getVoxelTr = [&](FIntVector3 areaMin, FIntVector3 areaMax) -> FTransform
	{
		return UKismetMathLibrary::ComposeTransforms(
			//Transform the engine cube to be a unit cube centered on 0.5:
			{
				 FQuat::Identity,
				 FVector{ 0.5, 0.5, 0.5 },
				 FVector::OneVector / 100
			},
			//Put it in the requested local space:
			{
				FQuat::Identity,
				FVector{ areaMin },
				FVector{ areaMax + FIntVector{ 1, 1, 1 } - areaMin }
			}
		);
	};

	//Use greedy meshing: find each new spot, grow it as much as possible,
	//  and draw that space as one big cube instance.
	TArray<bool> isCellProcessed;
	isCellProcessed.SetNumZeroed(grid->GetBytes().Num());
	grid->ForEach([&](int flatIdx, const FIntVector& idx3D, uint8 cellValue)
	{
		if (isCellProcessed[flatIdx] || isCellIrrelevant[flatIdx] || cellTypeMaterials[cellValue] == nullptr)
			return;
		auto* ism = ISMsByMaterial[cellTypeMaterials[cellValue]];
		
		FIntVector boxMin = idx3D,
				   boxMax = idx3D;
		auto isSliceValid = [&](int axis, int axisHorz1, int axisHorz2, int validAxisPos) -> bool
		{
			for (int iHorz1 = boxMin[axisHorz1]; iHorz1 <= boxMax[axisHorz1]; ++iHorz1)
			{
				for (int iHorz2 = boxMin[axisHorz2]; iHorz2 <= boxMax[axisHorz2]; ++iHorz2)
				{
					FIntVector v;
					v[axis] = validAxisPos;
					v[axisHorz1] = iHorz1;
					v[axisHorz2] = iHorz2;
					
					int i = grid->GetFlatIdx(v);
					if (isCellProcessed[i] || (grid->ByteAt(v) != cellValue || !isCellIrrelevant[i]))
						return false;
				}
			}
			return true;
		};
		
		//Walk backwards as much as possible.
		for (int axis = 0; axis < 3; ++axis)
		{
			int h1 = (axis + 1) % 3,
				h2 = (axis + 2) % 3;
			
			int prevPos = boxMin[axis];
			while (prevPos > 0 && isSliceValid(axis, h1, h2, prevPos - 1))
				prevPos -= 1;

			boxMin[axis] = prevPos;
		}
		//Walk forwards as much as possible.
		for (int axis = 0; axis < 3; ++axis)
		{
			int h1 = (axis + 1) % 3,
				h2 = (axis + 2) % 3;
			
			int nextPos = boxMin[axis];
			while (nextPos < Resolution[axis]-1 && isSliceValid(axis, h1, h2, nextPos + 1))
				nextPos += 1;

			boxMax[axis] = nextPos;
		}

		//Generate a mesh instance for this area.
		for (int z = boxMin.Z; z <= boxMax.Z; ++z)
			for (int y = boxMin.Y; y <= boxMax.Y; ++y)
				for (int x = boxMin.X; x <= boxMax.X; ++x)
				{
					int i = grid->GetFlatIdx({ x, y, z });
					checkSlow(!isCellProcessed[i]);
					isCellProcessed[i] = true;
				}
		blockInstances[ism].Add(getVoxelTr(boxMin, boxMax));
	});

	//Submit all the instances.
	for (const auto& [ism, newInstances] : blockInstances)
		ism->AddInstances(newInstances, false);
}
