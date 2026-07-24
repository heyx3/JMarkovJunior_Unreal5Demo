#include "JMarkovJuniorDemo.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, JMarkovJuniorDemo, "JMarkovJuniorDemo" );

FBox UJmjDemoUtils::BoundingBoxForPoints(const TArray<FVector>& points)
{
	return FBoxSphereBounds{ points.GetData(), static_cast<uint32>(points.Num()) }.GetBox();
}
