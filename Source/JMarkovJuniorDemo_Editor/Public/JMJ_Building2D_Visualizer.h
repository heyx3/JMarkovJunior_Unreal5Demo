#pragma once

#include "JMarkovJuniorDemo_Editor.h"
#include "ComponentVisualizer.h"
#include "JMarkovJuniorDemo/JMJ_Demo_Buildings2D.h"


class JMARKOVJUNIORDEMO_EDITOR_API FComponentViz_JmjBuilding2D : public FComponentVisualizer
{
public:

	virtual void DrawVisualization(const UActorComponent* _component, const FSceneView* view, FPrimitiveDrawInterface* pdi) override;
};
