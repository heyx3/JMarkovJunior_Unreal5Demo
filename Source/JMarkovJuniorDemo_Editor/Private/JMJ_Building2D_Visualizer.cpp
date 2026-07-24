#include "JMJ_Building2D_Visualizer.h"



void FComponentViz_JmjBuilding2D::DrawVisualization(const UActorComponent* _component,
                                                    const FSceneView* view,
                                                    FPrimitiveDrawInterface* pdi)
{
	if (!IsValid(_component) || !_component->IsA<UJmjBuilding2DEditorViz>())
		return;
	auto* component = CastChecked<UJmjBuilding2DEditorViz>(_component);
	auto* actor = CastChecked<AJmjBuilding2D>(component->GetOwner());

	FBox localBounds{ FVector{ -0.5, -0.5, 0 }, FVector{ 0.5, 0.5, 1 } };
	DrawWireBox(pdi, actor->GetTransform().ToMatrixWithScale(), localBounds,
	      	    component->BoundsColor, SDPG_World, component->LineThickness);
}