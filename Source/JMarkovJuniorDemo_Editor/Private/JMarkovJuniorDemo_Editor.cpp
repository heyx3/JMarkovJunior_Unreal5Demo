#include "JMarkovJuniorDemo_Editor.h"

#include "JMJ_Building2D_Visualizer.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"

#define LOCTEXT_NAMESPACE "FJMarkovJuniorDemo_EditorModule"

void FJMarkovJuniorDemo_EditorModule::StartupModule()
{
    if (GUnrealEd)
    {
        GUnrealEd->RegisterComponentVisualizer(UJmjBuilding2DEditorViz::StaticClass()->GetFName(),
											   MakeShareable(new FComponentViz_JmjBuilding2D));
    }
}

void FJMarkovJuniorDemo_EditorModule::ShutdownModule()
{
    if (GUnrealEd)
    {
        GUnrealEd->UnregisterComponentVisualizer(UJmjBuilding2DEditorViz::StaticClass()->GetFName());
    }
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FJMarkovJuniorDemo_EditorModule, JMarkovJuniorDemo_Editor)