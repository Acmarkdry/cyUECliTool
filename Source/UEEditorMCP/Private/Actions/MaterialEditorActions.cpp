// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/MaterialActions.h"
#include "MCPContext.h"
#include "MCPCommonUtils.h"

// Material system headers
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionSquareRoot.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionFloor.h"
#include "Materials/MaterialExpressionCeil.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionCosine.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionCrossProduct.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionSceneDepth.h"
#include "Materials/MaterialExpressionDDX.h"
#include "Materials/MaterialExpressionDDY.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionStep.h"
#include "Materials/MaterialExpressionSmoothStep.h"
#include "Materials/MaterialExpressionMin.h"
#include "Materials/MaterialExpressionMax.h"
#include "Materials/MaterialExpressionTextureSample.h"
// P4.3: TextureParameter
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
// P4.7: StaticSwitchParameter
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionStaticComponentMaskParameter.h"
// P4.8: MaterialFunction
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
// P4.5: Material comment
#include "Materials/MaterialExpressionComment.h"
// P4.4: Material graph
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "MaterialGraph/MaterialGraphNode_Root.h"
// P4.5+: Material editor graph selection
#include "GraphEditor.h"
// Layout utilities (P4.4 reuse)
#include "Actions/LayoutActions.h"
// Shared pin-aware layer sorting (P4.4 unified)
#include "MaterialLayoutUtils.h"

// Factory and editing
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "MaterialEditingLibrary.h"

// Editor and asset utilities
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "UObject/SavePackage.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Kismet/GameplayStatics.h"

// Post process volume
#include "Engine/PostProcessVolume.h"
#include "Components/PostProcessComponent.h"
#include "EngineUtils.h"  // For TActorIterator
#include "ComponentReregisterContext.h"  // For FGlobalComponentReregisterContext

// P5.1: Shader compilation manager (for material compile error retrieval)
#include "ShaderCompiler.h"
// P5.3: AssetEditorSubsystem (for finding preview material in editor)
#include "Subsystems/AssetEditorSubsystem.h"
// P5.3+: IMaterialEditor public API for selection queries across multiple open editors
#include "IMaterialEditor.h"
#include "MaterialEditorUtilities.h"
// P5.2/P5.4: Material apply to component/actor
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"


// =========================================================================
// FApplyMaterialToComponentAction (P5.2)
// =========================================================================

bool FApplyMaterialToComponentAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ActorName, MaterialPath;
	if (!GetRequiredString(Params, TEXT("actor_name"), ActorName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("material_path"), MaterialPath, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FApplyMaterialToComponentAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString ActorName, MaterialPath;
	GetRequiredString(Params, TEXT("actor_name"), ActorName, Error);
	GetRequiredString(Params, TEXT("material_path"), MaterialPath, Error);

	FString ComponentName = GetOptionalString(Params, TEXT("component_name"), TEXT(""));
	int32 SlotIndex = static_cast<int32>(GetOptionalNumber(Params, TEXT("slot_index"), 0.0));

	// Find actor in the editor world
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorName || It->GetName() == ActorName)
		{
			TargetActor = *It;
			break;
		}
	}

	if (!TargetActor)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Actor '%s' not found in level"), *ActorName), TEXT("actor_not_found"));
	}

	// Load material
	UMaterialInterface* MaterialToApply = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!MaterialToApply)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Material '%s' not found"), *MaterialPath), TEXT("material_not_found"));
	}

	// Find the target primitive component
	UPrimitiveComponent* TargetComponent = nullptr;

	if (!ComponentName.IsEmpty())
	{
		// Find by component name
		TArray<UActorComponent*> Components;
		TargetActor->GetComponents(Components);
		for (UActorComponent* Comp : Components)
		{
			if (Comp && (Comp->GetName() == ComponentName || Comp->GetFName().ToString() == ComponentName))
			{
				TargetComponent = Cast<UPrimitiveComponent>(Comp);
				if (TargetComponent)
				{
					break;
				}
			}
		}

		if (!TargetComponent)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Component '%s' not found or not a PrimitiveComponent on actor '%s'"), *ComponentName, *ActorName), TEXT("component_not_found"));
		}
	}
	else
	{
		// Find first PrimitiveComponent
		TArray<UPrimitiveComponent*> PrimitiveComponents;
		TargetActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
		if (PrimitiveComponents.Num() > 0)
		{
			TargetComponent = PrimitiveComponents[0];
		}

		if (!TargetComponent)
		{
			return CreateErrorResponse(FString::Printf(TEXT("No PrimitiveComponent found on actor '%s'"), *ActorName), TEXT("no_primitive_component"));
		}
	}

	// Get previous material for reporting
	FString PreviousMaterialPath = TEXT("None");
	UMaterialInterface* PreviousMaterial = TargetComponent->GetMaterial(SlotIndex);
	if (PreviousMaterial)
	{
		PreviousMaterialPath = PreviousMaterial->GetPathName();
	}

	// Apply material
	TargetComponent->SetMaterial(SlotIndex, MaterialToApply);

	// Mark modified
	TargetActor->MarkPackageDirty();

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("actor_name"), ActorName);
	Result->SetStringField(TEXT("component_name"), TargetComponent->GetName());
	Result->SetNumberField(TEXT("slot_index"), SlotIndex);
	Result->SetStringField(TEXT("material_path"), MaterialPath);
	Result->SetStringField(TEXT("previous_material"), PreviousMaterialPath);

	return CreateSuccessResponse(Result);
}


// =========================================================================
// FApplyMaterialToActorAction (P5.4)
// =========================================================================

bool FApplyMaterialToActorAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ActorName, MaterialPath;
	if (!GetRequiredString(Params, TEXT("actor_name"), ActorName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("material_path"), MaterialPath, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FApplyMaterialToActorAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString ActorName, MaterialPath;
	GetRequiredString(Params, TEXT("actor_name"), ActorName, Error);
	GetRequiredString(Params, TEXT("material_path"), MaterialPath, Error);

	int32 SlotIndex = static_cast<int32>(GetOptionalNumber(Params, TEXT("slot_index"), 0.0));

	// Find actor in the editor world
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorName || It->GetName() == ActorName)
		{
			TargetActor = *It;
			break;
		}
	}

	if (!TargetActor)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Actor '%s' not found in level"), *ActorName), TEXT("actor_not_found"));
	}

	// Load material
	UMaterialInterface* MaterialToApply = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!MaterialToApply)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Material '%s' not found"), *MaterialPath), TEXT("material_not_found"));
	}

	// Find all PrimitiveComponents on the actor
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	TargetActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	if (PrimitiveComponents.Num() == 0)
	{
		return CreateErrorResponse(FString::Printf(TEXT("No PrimitiveComponent found on actor '%s'"), *ActorName), TEXT("no_primitive_component"));
	}

	// Apply material to all components
	TArray<TSharedPtr<FJsonValue>> ComponentNames;
	int32 UpdatedCount = 0;

	for (UPrimitiveComponent* Comp : PrimitiveComponents)
	{
		if (!Comp)
		{
			continue;
		}

		int32 NumMaterials = Comp->GetNumMaterials();
		if (SlotIndex < NumMaterials)
		{
			Comp->SetMaterial(SlotIndex, MaterialToApply);
			ComponentNames.Add(MakeShared<FJsonValueString>(Comp->GetName()));
			++UpdatedCount;
		}
	}

	// Mark modified
	TargetActor->MarkPackageDirty();

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("actor_name"), ActorName);
	Result->SetStringField(TEXT("material_path"), MaterialPath);
	Result->SetNumberField(TEXT("slot_index"), SlotIndex);
	Result->SetNumberField(TEXT("components_updated"), UpdatedCount);
	Result->SetArrayField(TEXT("component_names"), ComponentNames);

	return CreateSuccessResponse(Result);
}

// =========================================================================
// FRefreshMaterialEditorAction
// =========================================================================

bool FRefreshMaterialEditorAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MaterialName;
	return GetRequiredString(Params, TEXT("material_name"), MaterialName, OutError);
}

TSharedPtr<FJsonObject> FRefreshMaterialEditorAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString MaterialName = Params->GetStringField(TEXT("material_name"));
	FString FindError;
	UMaterial* Material = FindMaterial(MaterialName, FindError);
	if (!Material)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Material '%s' not found: %s"), *MaterialName, *FindError));
	}

	bool bGraphRebuilt = false;
	bool bEditorFound = false;
	bool bPreviewsRefreshed = false;

	// ---- Robust material editor lookup ----
	// The material editor uses a preview material copy (UPreviewMaterial in TransientPackage).
	// The SGraphEditor references the preview material's graph, not the original.
	// We must find the preview material to get a valid IMaterialEditor handle.
	//
	// IMPORTANT: Engine modules compiled WITHOUT RTTI (/GR-), so no dynamic_cast.
	// Use only FMaterialEditorUtilities::GetIMaterialEditorForObject (StaticCastSharedPtr).

	TSharedPtr<IMaterialEditor> MatEditorPtr;
	UMaterial* PreviewMaterial = nullptr;

	// Method 1: Try original material's graph 鈫?FMaterialEditorUtilities
	if (Material->MaterialGraph)
	{
		MatEditorPtr = FMaterialEditorUtilities::GetIMaterialEditorForObject(Material->MaterialGraph);
		if (MatEditorPtr.IsValid())
		{
			UE_LOG(LogMCP, Log, TEXT("refresh_material_editor: Found editor for '%s' via GetIMaterialEditorForObject (original graph)"), *MaterialName);
		}
	}

	// Method 2: Find preview material through UAssetEditorSubsystem, then use its graph
	UAssetEditorSubsystem* EditorSS = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	if (!MatEditorPtr.IsValid() && EditorSS)
	{
		TArray<UObject*> EditedAssets = EditorSS->GetAllEditedAssets();
		for (UObject* Asset : EditedAssets)
		{
			UMaterial* Mat = Cast<UMaterial>(Asset);
			if (Mat && Mat != Material && Mat->MaterialGraph && Mat->GetOutermost() == GetTransientPackage())
			{
				// Candidate preview material 鈥?verify it's paired with our material
				TSharedPtr<IMaterialEditor> TestEditor = FMaterialEditorUtilities::GetIMaterialEditorForObject(Mat->MaterialGraph);
				if (TestEditor.IsValid())
				{
					const TArray<UObject*>* EditObjs = TestEditor->GetObjectsCurrentlyBeingEdited();
					if (EditObjs)
					{
						for (UObject* Obj : *EditObjs)
						{
							if (Obj == Material)
							{
								MatEditorPtr = TestEditor;
								PreviewMaterial = Mat;
								UE_LOG(LogMCP, Log, TEXT("refresh_material_editor: Found editor for '%s' via preview material '%s'"),
									*MaterialName, *Mat->GetName());
								break;
							}
						}
					}
				}
				if (MatEditorPtr.IsValid()) break;
			}
		}
	}

	// Method 3: Also check non-transient edited assets that match the name directly
	if (!MatEditorPtr.IsValid() && EditorSS)
	{
		TArray<UObject*> EditedAssets = EditorSS->GetAllEditedAssets();
		for (UObject* Asset : EditedAssets)
		{
			UMaterial* Mat = Cast<UMaterial>(Asset);
			if (Mat && Mat->GetName() == MaterialName && Mat->MaterialGraph)
			{
				TSharedPtr<IMaterialEditor> TestEditor = FMaterialEditorUtilities::GetIMaterialEditorForObject(Mat->MaterialGraph);
				if (TestEditor.IsValid())
				{
					MatEditorPtr = TestEditor;
					UE_LOG(LogMCP, Log, TEXT("refresh_material_editor: Found editor for '%s' via name-matched edited asset"), *MaterialName);
					break;
				}
			}
		}
	}

	// Step 1: Rebuild the material graph(s)
	// Rebuild the original material's graph
	if (Material->MaterialGraph)
	{
		Material->MaterialGraph->RebuildGraph();
		Material->MaterialGraph->NotifyGraphChanged();
		bGraphRebuilt = true;
	}
	// Also rebuild the preview material's graph if found (this is what the editor displays)
	if (PreviewMaterial && PreviewMaterial->MaterialGraph)
	{
		PreviewMaterial->MaterialGraph->RebuildGraph();
		PreviewMaterial->MaterialGraph->NotifyGraphChanged();
	}

	// Step 2: Refresh the open Material Editor via IMaterialEditor
	if (MatEditorPtr.IsValid())
	{
		bEditorFound = true;
		MatEditorPtr->UpdateMaterialAfterGraphChange();
		MatEditorPtr->ForceRefreshExpressionPreviews();
		bPreviewsRefreshed = true;
		UE_LOG(LogMCP, Log, TEXT("refresh_material_editor: Refreshed editor UI for '%s'"), *MaterialName);
	}
	else
	{
		UE_LOG(LogMCP, Log, TEXT("refresh_material_editor: No open editor found for '%s' (graph was still rebuilt)"), *MaterialName);
	}

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("material_name"), MaterialName);
	Result->SetBoolField(TEXT("editor_found"), bEditorFound);
	Result->SetBoolField(TEXT("graph_rebuilt"), bGraphRebuilt);
	Result->SetBoolField(TEXT("previews_refreshed"), bPreviewsRefreshed);

	return CreateSuccessResponse(Result);
}

