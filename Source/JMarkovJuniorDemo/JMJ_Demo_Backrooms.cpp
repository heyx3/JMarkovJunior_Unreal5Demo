#include "JMJ_Demo_Backrooms.h"

#include <array>
#include <cmath>
#include <numeric>

#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Kismet/KismetMathLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/GameModeBase.h"

#include "JMJ_Grids.h"


namespace BackroomsJmjAlgo
{
	static const FString CharEmpty = TEXT("E"),
						 CharWall = TEXT("I"),
						 CharForcedEmpty = TEXT("w"),
						 CharForcedWall = TEXT("b"),
						 CharLight = TEXT("Y");

	//The algorithm to place walls, given a clean slate with optional forced-walls and forced-spaces.
	static const char* SrcGenWalls = R"JMJ(@markovjunior 'I' begin
		#NOTE: This is a fork of the sample 'Backrooms.jl' scene,
		#        with added support for forced open/wall spaces.
		#      These forced spaces are used to ensure cells connect to each other.

		# Generate a regular old maze.
		@rewrite 1 I=>Y
		@rewrite YII => YEY
		@rewrite Y => E

		# Mangle it
		@rewrite I=>E %(0.3:0.5)

		# Clean up the walls into something coherent.
		@rewrite begin
			PRIORITIZE(earliest)
			[Ib]E[Ib] => _I_  %0.25 \[ x ]
			[Ib]E[Ib] => _I_        \[ y ]
		end
		@rewrite [
			[Ew] [Ew] [Ew]
			[Ew]  I   [Ew]
			[Ew] [Ew] [Ew]
		  ] => [
			_ _ _
			_ E _
			_ _ _
		]	 \[ (x, y)[ (+x, +y) ] ]

		# Ensure areas are well-connected.
		#   1) Pick a home region
		@rewrite 1 [Ew]=>[MS]
		@rewrite [MS][Ew] => _[MS]
		#   2) Keep connecting it to other regions
		@sequence repeat begin
			# The first op of a 'repeat' sequence determines whether it should quit.
			# We want to quit when the whole map is connected.
			@rewrite 1 [Ew] => [TL]
			@rewrite 1 [TL] => [Ew]

			# Grab any low-hanging fruit.
			@rewrite 1 [MS]IIIII[Ew] => _MMMMM[MS]
			@rewrite 1 [MS]IIII[Ew] => _MMMM_
			@rewrite 1 [MS]III[Ew] => _MMM_
			@rewrite 1 [MS]II[Ew] => _MM_
			@rewrite 1 [MS]I[Ew] => _M_
			@rewrite [MS][Ew] => _[MS]

			# Now try a loop-erased random walk along the boundary of the home region.
			@rewrite 1 [MS]I => [MS]R
			@rewrite (area/10) begin
				PRIORITIZE(earliest)

				R[Ew]  => L_    # Make it to a destination!
				RI[Ew] => LE_   # Make it to a destination!

				OGB => IIR # Finish the loop-erasure
				OGG => IIO # Continue the loop-erasure

				RII => GGR # Continue the walk
				RIG => O_B # Nowhere else to go; give up and start erasing
			end
			# If there's an L on the grid, then we got a new path and need to carve it out.
			# Otherwise this search was a dud and needs to be deleted.
			@rewrite L[ROBG] => LL
			@rewrite [ROBG] => I
			@rewrite [MS][LEw] => _[MMS]
		end
		# Finalize the walls and spaces.
		@rewrite [MS]=>E
		@rewrite b=>I

		# Now place lights.
		@rewrite 1 E=>Y
		@sequence begin
			@rewrite area/40 E=>L
			@rewrite area/120 E=>Y
		end field(LY->E)
		@rewrite L=>E
	end)JMJ";
}


void UBackrooms_GM_Component::BeginPlay()
{
	Super::BeginPlay();

	if (!GetOwner()->IsA<AGameModeBase>())
		UE_LOG(LogJMarkovJunior, Error, TEXT("%s attached to a %s instead of a Game Mode!"), *GetClass()->GetName(), *GetOwner()->GetName());

	//Sanitize the inputs.
	if (Seed == 0)
	{
		Seed = FMath::Rand();
		UE_LOG(LogJMarkovJunior, Log,
			   TEXT("Generating a Backrooms level with random InitialSeed of %i"), Seed);
	}
	if (CellResolution < 4)
	{
		CellResolution = 4;
		UE_LOG(LogJMarkovJunior, Warning, TEXT("CellResolution was increased to the min value of 4"));
	}
	
	//Ensure the JMarkovJunior worker process is up and running.
	auto* jmj = GEngine->GetEngineSubsystem<UJmjProcessManager>();
	if (!jmj->WaitForProcess())
		UE_LOG(LogJMarkovJunior, Error, TEXT("JMJ process not found! Backrooms won't generate :("));

	//Parse our algorithm.
	FString jmjErrorMsg;
	if (!jmj->ParseAlgorithm(BackroomsJmjAlgo::SrcGenWalls, jmjErrorMsg, JmjAlgo))
		UE_LOG(LogJMarkovJunior, Error, TEXT("JMJ algorithm failed to parse:\n%s"), *jmjErrorMsg);
	
	LoadCell({ 0, 0 });
}
void UBackrooms_GM_Component::EndPlay(const EEndPlayReason::Type reason)
{
	if (reason == EEndPlayReason::Type::Destroyed)
	{
		for (const auto& kvp : ActiveCells)
			kvp.Value->Destroy();
		for (auto* s : Seals)
			s->Destroy();
	}

	Super::EndPlay(reason);
}

FJmjIntVector2D UBackrooms_GM_Component::GetCell(const FVector& worldPos) const
{
	auto localPos = (worldPos - (Origin ? Origin->GetComponentLocation() : FVector::ZeroVector))
					/ FVector{ CellResolution * PixelLength, CellResolution * PixelLength, 1 }
					/ (Origin ? Origin->GetComponentScale() : FVector::OneVector);
	return {
		static_cast<int>(std::floor(localPos.X)),
		static_cast<int>(std::floor(localPos.Y))
	};
}
bool UBackrooms_GM_Component::GetRandomEmptyPos(const FJmjIntVector2D& cell2D,
												FJmjIntVector2D& outLocalPixel, FVector& outWorldFloorPos) const
{
	auto* tryCell = ActiveCells.Find({ cell2D.X, cell2D.Y });
	if (!tryCell)
		return false;
	auto* cell = *tryCell;

	auto emptyCellID = UJmjConstants::GetCellValueByID(BackroomsJmjAlgo::CharEmpty);
	checkSlow(cell->GeneratedGrid->ForEach([&](int flatIdx, const FJmjIntVector2D& idx, uint8 val)
	{
		return val == emptyCellID;
	}));

	//In practice, at least half of cells are empty, so keep trying them at random.
	do
	{
		outLocalPixel = { FMath::RandRange(0, CellResolution-1), FMath::RandRange(0, CellResolution-1) };
	} while (cell->GeneratedGrid->ByteAt(outLocalPixel) != emptyCellID);

	outWorldFloorPos = PixelIdxToWorldFloorPos(
		{ outLocalPixel.X + (cell2D.X * CellResolution),
			outLocalPixel.Y + (cell2D.Y * CellResolution) },
		false
	);
	return true;
}
FVector UBackrooms_GM_Component::PixelIdxToWorldFloorPos(const FJmjIntVector2D& pixel, bool atMinCorner) const
{
	float pixelOffset = (atMinCorner ? 0.0f : 0.5f);
	auto gridPos = FVector{ pixel.X + pixelOffset, pixel.Y + pixelOffset, 0 };
	auto localPos = gridPos * FVector{ PixelLength, PixelLength, PixelHeight };
	
	auto worldTr = Origin ? Origin->GetComponentTransform() : FTransform::Identity;
	return worldTr.TransformPosition(localPos);
}

bool UBackrooms_GM_Component::LoadCell(const FJmjIntVector2D& cellIdx)
{
	if (ActiveCells.Contains(cellIdx))
		return false;
	UE_LOG(LogJMarkovJunior, Log, TEXT("Generating backrooms cell {%i, %i}..."), cellIdx.X, cellIdx.Y);

	FJmjIntVector2D firstPixelIdx{ cellIdx.X * CellResolution, cellIdx.Y * CellResolution }; 

	auto* world = GetWorld();
	FActorSpawnParameters spawnParams;
	spawnParams.Owner = GetOwner();
	spawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	
	//Seed the level generator.
	auto* jmjGrid = NewObject<UJmjGrid2D>(spawnParams.Owner);
	jmjGrid->InitializeWithResolution({ CellResolution, CellResolution });
	//Most of the grid is indeterminate, but each edge needs to be fixed so cells know how to line up.
	int maxPossibleEntryways = (CellResolution - 2) / 3; //Every other edge pixel excluding corners
	int nMinEntryways = FMath::Clamp(MinEdgeConnections, 1, maxPossibleEntryways),
		nMaxEntryways = FMath::Clamp(MaxEdgeConnections, nMinEntryways, maxPossibleEntryways);
	auto cellValueWall = UJmjConstants::GetCellValueByID(BackroomsJmjAlgo::CharWall),
	     cellValueEmpty = UJmjConstants::GetCellValueByID(BackroomsJmjAlgo::CharEmpty),
		 cellValueForcedWall = UJmjConstants::GetCellValueByID(BackroomsJmjAlgo::CharForcedWall),
		 cellValueForcedEmpty = UJmjConstants::GetCellValueByID(BackroomsJmjAlgo::CharForcedEmpty),
		 cellValueLight = UJmjConstants::GetCellValueByID(BackroomsJmjAlgo::CharLight);
	//First fill the grid with indeterminate values.
	jmjGrid->ForEach([&](int flatIdx, const auto& vIdx, uint8& value) { value = cellValueWall; });
	//Next, fill each edge with forced values.
	bufferIdx2DMap.Empty(); //Maps local index of an entryway to the index of the neighboring cell
	for (bool isVertical : std::to_array({ false, true }))
	{
		for (bool isMinSide : std::to_array({ false, true }))
		{
			auto neighborCellIdx = cellIdx;
			(isVertical ? neighborCellIdx.X : neighborCellIdx.Y) += (isMinSide ? -1 : 1);
			
			auto getEdgeVIdx = [&](int along) -> TTuple<FJmjIntVector2D, bool>
			{
				return MakeTuple(
					FJmjIntVector2D{
						isVertical ? (isMinSide ? 0 : CellResolution-1) : along,
						isVertical ? along : (isMinSide ? 0 : CellResolution-1)
					},
					(along >= 0) && (along < CellResolution)
				);
			};
			
			//First fill the whole edge with walls.
			for (int i = 0; i < CellResolution; ++i)
				jmjGrid->ByteAt(getEdgeVIdx(i).Get<0>()) = cellValueForcedWall;


			//Now add random entryways.
			
			//First pick the unique seed for this edge; for determinism it must
			//   come out to the same value for both cells on either side.
			//We standardize on using the index of the cell after the edge.
			auto seedSrc = cellIdx;
			if (!isMinSide)
				(isVertical ? seedSrc.X : seedSrc.Y) += 1;
			int seed = GetTypeHash(MakeTuple(seedSrc.X, seedSrc.Y, Seed, isVertical));
			FRandomStream rng{ seed };
			
			//Next generate the entryway positions.
			int nEntryways = rng.RandRange(nMinEntryways, nMaxEntryways);
			bufferIntegerSet.Empty();
			for (int i = 1; i < CellResolution-1; ++i)
				bufferIntegerSet.Add(i);
			for (int entryI = 0; entryI < nEntryways; ++entryI)
			{
				//Assert that options are available.
				check(!bufferIntegerSet.IsEmpty());

				//Find the next local position.
				int posAlong;
				if (bufferIntegerSet.Num() == 1)
					posAlong = *bufferIntegerSet.begin();
				else do
				{
					posAlong = rng.RandRange(1, CellResolution - 1);
				} while (!bufferIntegerSet.Contains(posAlong));

				//Add that position as an entryway.
				auto entryPos = getEdgeVIdx(posAlong).Get<0>();
				jmjGrid->ByteAt(entryPos) = cellValueForcedEmpty;
				bufferIdx2DMap.Add(entryPos, neighborCellIdx);
				bufferIntegerSet.Remove(posAlong);
				bufferIntegerSet.Remove(posAlong - 1);
				bufferIntegerSet.Remove(posAlong + 1);
			}
		}
	}
	UE_LOG(LogJMarkovJunior, Log, TEXT("Initial state of cell {%i, %i}: %s"),
		   cellIdx.X, cellIdx.Y, *UJmjConstants::FormatCellGrid(jmjGrid->GetBytes(), { jmjGrid->GetResolution().X, jmjGrid->GetResolution().Y }));
	
	//Generate the cell contents.
	if (!jmjGrid->SeedAndDownloadAlgorithmRun(JmjAlgo, { cellIdx.X, cellIdx.Y, Seed }))
	{
		UE_LOG(LogJMarkovJunior, Error,
			   TEXT("Failed to generate cell {%i, %i}, so it will be left wide open!"),
			   cellIdx.X, cellIdx.Y);
		jmjGrid->ForEach([&](int flatIdx, const auto& gridIdx, auto& gridByte)
		{
			if (FMath::Min(gridIdx.X, gridIdx.Y) == 0 || FMath::Max(gridIdx.X, gridIdx.Y) == CellResolution-1)
				if (bufferIdx2DMap.Contains(gridIdx))
					gridByte = cellValueEmpty;
				else
					gridByte = cellValueWall;
			else
				if (((gridIdx.X + gridIdx.Y) % 3) == 0)
					gridByte = cellValueLight;
				else
					gridByte = cellValueEmpty;
		});
	}
	UE_LOG(LogJMarkovJunior, Log, TEXT("Final state of cell {%i, %i}: %s"),
		   cellIdx.X, cellIdx.Y, *UJmjConstants::FormatCellGrid(jmjGrid->GetBytes(), { jmjGrid->GetResolution().X, jmjGrid->GetResolution().Y }));

	//Spawn the cell actor.
	FTransform cellTr{
		FQuat::Identity,
		PixelIdxToWorldFloorPos(firstPixelIdx, true),
		FVector::OneVector
	};
	auto* cell = world->SpawnActorDeferred<ABackroomsCell>(
		CellPrefab, cellTr,
		GetOwner(), nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn
	);
	cell->GeneratedGrid = jmjGrid;
	cell->Idx = cellIdx;
	cell->FinishSpawning(cellTr, true);
	if (IsValid(Origin))
		cell->AttachToComponent(Origin, FAttachmentTransformRules::KeepRelativeTransform);

	//Spawn the cell pieces.
	jmjGrid->ForEach([&](int flatIdx, const auto& localPixel, auto gridValue)
	{
		if (gridValue == cellValueWall || gridValue == cellValueForcedWall)
		{
			cell->ServerInstanceLocations.Emplace(localPixel.X * PixelLength, localPixel.Y * PixelLength, 0);
		}
		else if (gridValue == cellValueLight && IsValid(CeilingLightPrefab))
		{
			UE_LOG(LogJMarkovJunior, Log, TEXT("Light at local={%i, %i}"), localPixel.X, localPixel.Y);

			//Rotate the light if we're in a hallway going along X.
			float rotDegrees = 0;
			if (localPixel.Y > 0 && localPixel.Y < CellResolution-1 &&
				jmjGrid->ByteAt({ localPixel.X, localPixel.Y - 1 }) == cellValueWall &&
				jmjGrid->ByteAt({ localPixel.X, localPixel.Y + 1 }) == cellValueWall)
			{
				rotDegrees = 90;
			}
			
			auto lightTr = UKismetMathLibrary::ComposeTransforms(
				FTransform{
					FRotator{ 0, rotDegrees, 0 },
					FVector{
						(localPixel.X + 0.5f) * PixelLength,
						(localPixel.Y + 0.5f) * PixelLength,
						PixelHeight - 4.0f
					}
				},
				cellTr
			);
			CeilingLights.Add(world->SpawnActor<AActor>(CeilingLightPrefab, lightTr, spawnParams));
		}
	});
	cell->FinalizeWallMeshInstances();
	ActiveCells.Add(cellIdx, cell); 
	
	//Spawn seals at the connections.
	check(bufferNewSeals.IsEmpty()); //Should be kept empty to avoid stale pointers
	for (const auto& [localPixel, triggeringCellIdx] : bufferIdx2DMap)
	{
		UE_LOG(LogJMarkovJunior, Log, TEXT("Seal at {%i, %i} of {%i, %i}"), localPixel.X, localPixel.Y, cellIdx.X, cellIdx.Y);
		auto sealTr = UKismetMathLibrary::ComposeTransforms(
			FTransform{ FVector{ localPixel.X * PixelLength, localPixel.Y * PixelLength, 0 } },
			cellTr
		);
		auto* seal = world->SpawnActorDeferred<ABackroomsSeal>(
			SealPrefab, sealTr,
			GetOwner(), nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn
		);
		seal->TargetCell = triggeringCellIdx;
		
		seal->FinishSpawning(sealTr, true);
		if (IsValid(Origin))
			seal->AttachToComponent(Origin, FAttachmentTransformRules::KeepRelativeTransform);

		seal->OnUnsealed.AddDynamic(this, &UBackrooms_GM_Component::OnSealBroken);
		bufferNewSeals.Add(seal);
	}
	
	OnNewCellGenerated.Broadcast(cellIdx, cell, { bufferNewSeals });
	bufferNewSeals.Empty();
	return true;
}
void UBackrooms_GM_Component::OnSealBroken(ABackroomsSeal* seal)
{
	LoadCell(seal->TargetCell);
}


ABackroomsSeal::ABackroomsSeal()
{
	bReplicates = true;
}
void ABackroomsSeal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	DOREPLIFETIME_CONDITION(ThisClass, TargetCell, COND_InitialOnly);
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

bool ABackroomsSeal::TriggerUnsealing_AuthorityOnly()
{
	if (!IsStillSealed || !HasAuthority())
		return false;
	IsStillSealed = false; //Redundant with TriggerUnsealingRPC but I'm not certain that's called immediately

	TriggerUnsealingRPC();
	return true;
}
void ABackroomsSeal::TriggerUnsealingRPC_Implementation()
{
	IsStillSealed = false;
	OnUnsealed.Broadcast(this);
}


ABackroomsCell::ABackroomsCell()
{
	bReplicates = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root Component"));
	
	FloorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Floor Mesh"));
	CeilingMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ceiling Mesh"));
	WallMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Walls ISM"));

	FloorMesh->SetupAttachment(RootComponent);
	CeilingMesh->SetupAttachment(RootComponent);
	WallMeshes->SetupAttachment(RootComponent);
}
void ABackroomsCell::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	DOREPLIFETIME(ThisClass, WallMeshes);
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

void ABackroomsCell::FinalizeWallMeshInstances()
{
	check(HasAuthority());

	//Spawn the instances locally.
	//We can't do it through the RPC due to ambiguities about the reused buffer going into those calls.
	ActuallyApplyMeshInstances(ServerInstanceLocations);
	
	//Transmit in pieces to avoid tripping the max RPC packet size.
	//On the other hand, note that there is some max allowed number of reliable RPC's 'in transit!
	constexpr int MaxInstancesPerRpc = 4096;
	int nextFirstIdx = 0;
	while (nextFirstIdx < ServerInstanceLocations.Num())
	{
		int nElementsInPacket = FMath::Min(MaxInstancesPerRpc,
										   ServerInstanceLocations.Num() - nextFirstIdx);
		
		rpcBuffer.Empty();
		for (int i = 0; i < nElementsInPacket; ++i)
			rpcBuffer.Add(ServerInstanceLocations[nextFirstIdx + i]);
		ReceiveMeshInstances(rpcBuffer);

		nextFirstIdx += nElementsInPacket;
	}
}
void ABackroomsCell::ReceiveMeshInstances_Implementation(const TArray<FVector3f>& locations)
{
	//Server must ignore this message, as I don't understand whether 'items'
	//   is a copy, or a true reference to this cell's reused buffer.
	if (HasAuthority())
		return;

	ActuallyApplyMeshInstances(locations);
}
void ABackroomsCell::ActuallyApplyMeshInstances(const TArray<FVector3f>& locations)
{
	transformBuffer.Empty();
	for (const auto& loc : locations)
		transformBuffer.Emplace(static_cast<FVector>(loc));
	WallMeshes->AddInstances(transformBuffer, false);
}
