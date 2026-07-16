#include "JMJ_Demo_Backrooms.h"

#include "JMJ_Grids.h"
#include "Algo/AnyOf.h"


namespace BackroomsJmjAlgo
{
	static constexpr FString CharEmpty = TEXT("E"),
						     CharWall = TEXT("I"),
						     CharForcedEmpty = TEXT("w"),
						     CharForcedWall = TEXT("b");
	static const char* Src = R"JMJ(@markovjunior 'I' begin
		#NOTE: This is a fork of the sample 'Backrooms.jl' scene,
		#        with added support for forced open/wall spaces.

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
		# Finalize the output.
		@rewrite [Mw]=>E
		@rewrite b=>I
	end)JMJ";
}


void UBackrooms_GM_Component::BeginPlay()
{
	Super::BeginPlay();

	//Ensure the JMarkovJunior worker process is up and running.
	auto* jmj = GEngine->GetEngineSubsystem<UJmjProcessManager>();
	if (!jmj->WaitForProcess())
		UE_LOG(LogJMarkovJunior, Error, TEXT("JMJ process not found! Backrooms won't generate :("));

	//Parse our algorithm.
	FString jmjErrorMsg;
	if (!jmj->ParseAlgorithm(BackroomsJmjAlgo::Src, jmjErrorMsg, JmjAlgo))
		UE_LOG(LogJMarkovJunior, Error, TEXT("JMJ algorithm failed to parse:\n%s"), *jmjErrorMsg);
	
	LoadCell({ 0, 0, 0 });
}
void UBackrooms_GM_Component::EndPlay(const EEndPlayReason::Type reason)
{
	if (reason == EEndPlayReason::Type::Destroyed)
	{
		for (const auto& kvp : ActiveCells)
			kvp.Value->Destroy();
		for (auto* s : Seals)
			s->Destroy();
		for (auto* f : Floors)
			f->Destroy();
		for (auto* c : Ceilings)
			c->Destroy();
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
	checkSlow(cell->GeneratedGrid->ForEach([&](const FJmjIntVector2D& idx, uint8 val)
	{
		return val == emptyCellID;
	}));

	//In practice, at least half of cells are empty, so keep trying them at random.
	do
	{
		outLocalPixel = { FMath::RandRange(0, CellResolution-1), FMath::RandRange(0, CellResolution-1) };
	} while (cell->GeneratedGrid->ByteAt(outLocalPixel) != emptyCellID);

	//Convert that cell to world space.
	outWorldFloorPos = (Origin ? Origin->GetComponentLocation() : FVector::ZeroVector) +
		((Origin ? Origin->GetComponentScale() : FVector::OneVector) *
		 FVector{ PixelLength, PixelLength, PixelHeight } *
		 FVector{
			outLocalPixel.X + 0.5,
			outLocalPixel.Y + 0.5,
			0
		 });
	
	return true;
}

//TODO: Finish