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
// FSetMaterialPropertyAction
// =========================================================================

bool FSetMaterialPropertyAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MaterialName, PropertyName, PropertyValue;
	if (!GetRequiredString(Params, TEXT("material_name"), MaterialName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("property_name"), PropertyName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("property_value"), PropertyValue, OutError)) return false;
	return true;
}

TOptional<EMaterialShadingModel> FSetMaterialPropertyAction::ResolveShadingModel(const FString& ShadingModelString) const
{
	InitShadingModelMap();
	if (EMaterialShadingModel* Found = ShadingModelMap.Find(ShadingModelString))
	{
		return *Found;
	}
	return TOptional<EMaterialShadingModel>();
}

TSharedPtr<FJsonObject> FSetMaterialPropertyAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString MaterialName, PropertyName, PropertyValue;
	GetRequiredString(Params, TEXT("material_name"), MaterialName, Error);
	GetRequiredString(Params, TEXT("property_name"), PropertyName, Error);
	GetRequiredString(Params, TEXT("property_value"), PropertyValue, Error);

	// Find material
	UMaterial* Material = FindMaterial(MaterialName, Error);
	if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	// Property handlers map (property name -> handler lambda)
	using FPropertyHandler = TFunction<bool(UMaterial*, const FString&, FString&)>;
	static TMap<FString, FPropertyHandler> PropertyHandlers;

	if (PropertyHandlers.Num() == 0)
	{
		PropertyHandlers.Add(TEXT("ShadingModel"), [this](UMaterial* Mat, const FString& Value, FString& OutErr) -> bool
		{
			TOptional<EMaterialShadingModel> Model = ResolveShadingModel(Value);
			if (!Model.IsSet())
			{
				OutErr = FString::Printf(TEXT("Invalid ShadingModel '%s'. Valid: Unlit, DefaultLit, Subsurface, PreintegratedSkin, ClearCoat, SubsurfaceProfile, TwoSidedFoliage, Hair, Cloth, Eye"), *Value);
				return false;
			}
			Mat->SetShadingModel(Model.GetValue());
			return true;
		});

		PropertyHandlers.Add(TEXT("TwoSided"), [](UMaterial* Mat, const FString& Value, FString& OutErr) -> bool
		{
			Mat->TwoSided = Value.ToBool() || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1");
			return true;
		});

		PropertyHandlers.Add(TEXT("BlendMode"), [](UMaterial* Mat, const FString& Value, FString& OutErr) -> bool
		{
			InitBlendModeMap();
			if (EBlendMode* Found = BlendModeMap.Find(Value))
			{
				Mat->BlendMode = *Found;
				return true;
			}
			OutErr = FString::Printf(TEXT("Invalid BlendMode '%s'. Valid: Opaque, Masked, Translucent, Additive, Modulate, AlphaComposite, AlphaHoldout"), *Value);
			return false;
		});

		PropertyHandlers.Add(TEXT("DitheredLODTransition"), [](UMaterial* Mat, const FString& Value, FString& OutErr) -> bool
		{
			Mat->DitheredLODTransition = Value.ToBool() || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1");
			return true;
		});

		PropertyHandlers.Add(TEXT("AllowNegativeEmissiveColor"), [](UMaterial* Mat, const FString& Value, FString& OutErr) -> bool
		{
			Mat->bAllowNegativeEmissiveColor = Value.ToBool() || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1");
			return true;
		});

		PropertyHandlers.Add(TEXT("OpacityMaskClipValue"), [](UMaterial* Mat, const FString& Value, FString& OutErr) -> bool
		{
			Mat->OpacityMaskClipValue = FCString::Atof(*Value);
			return true;
		});

		PropertyHandlers.Add(TEXT("TangentSpaceNormal"), [](UMaterial* Mat, const FString& Value, FString& OutErr) -> bool
		{
			Mat->bTangentSpaceNormal = Value.ToBool() || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1");
			return true;
		});
	}

	// Find and execute the property handler
	FPropertyHandler* Handler = PropertyHandlers.Find(PropertyName);
	if (!Handler)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Unknown material property '%s'. Supported: ShadingModel, TwoSided, BlendMode, DitheredLODTransition, AllowNegativeEmissiveColor, OpacityMaskClipValue, TangentSpaceNormal"), *PropertyName),
			TEXT("unknown_property"));
	}

	FString HandlerError;
	if (!(*Handler)(Material, PropertyValue, HandlerError))
	{
		return CreateErrorResponse(HandlerError, TEXT("property_set_failed"));
	}

	// Mark material as modified and trigger recompilation
	MarkMaterialModified(Material, Context);

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("material_name"), MaterialName);
	Result->SetStringField(TEXT("property_name"), PropertyName);
	Result->SetStringField(TEXT("property_value"), PropertyValue);

	return CreateSuccessResponse(Result);
}


// =========================================================================
// FCreatePostProcessVolumeAction
// =========================================================================

bool FCreatePostProcessVolumeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	return GetRequiredString(Params, TEXT("name"), Name, OutError);
}

FVector FCreatePostProcessVolumeAction::GetVectorFromParams(const TSharedPtr<FJsonObject>& Params, const FString& FieldName) const
{
	FVector Result = FVector::ZeroVector;
	const TArray<TSharedPtr<FJsonValue>>* Arr = GetOptionalArray(Params, FieldName);
	if (Arr && Arr->Num() >= 3)
	{
		Result.X = (*Arr)[0]->AsNumber();
		Result.Y = (*Arr)[1]->AsNumber();
		Result.Z = (*Arr)[2]->AsNumber();
	}
	return Result;
}

TSharedPtr<FJsonObject> FCreatePostProcessVolumeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString ActorName;
	GetRequiredString(Params, TEXT("name"), ActorName, Error);

	FVector Location = GetVectorFromParams(Params, TEXT("location"));
	bool bInfiniteExtent = GetOptionalBool(Params, TEXT("infinite_extent"), true);
	float Priority = GetOptionalNumber(Params, TEXT("priority"), 0.0);

	// Get world
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No world available"), TEXT("no_world"));
	}

	// Find and delete existing actor with same name using safe method
	TArray<AActor*> AllPPVs;
	UGameplayStatics::GetAllActorsOfClass(World, APostProcessVolume::StaticClass(), AllPPVs);
	for (AActor* Actor : AllPPVs)
	{
		if (Actor && (Actor->GetActorLabel() == ActorName || Actor->GetName() == ActorName))
		{
			// Deselect before destroying to avoid editor issues
			GEditor->SelectNone(true, true);
			World->DestroyActor(Actor);
			break;
		}
	}

	// Spawn post process volume
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(*ActorName);
	SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	APostProcessVolume* Volume = World->SpawnActor<APostProcessVolume>(Location, FRotator::ZeroRotator, SpawnParams);
	if (!Volume)
	{
		return CreateErrorResponse(TEXT("Failed to spawn post process volume"), TEXT("spawn_failed"));
	}

	// Set properties
	Volume->bUnbound = bInfiniteExtent;
	Volume->Priority = Priority;
	Volume->SetActorLabel(ActorName);

	// Add materials
	const TArray<TSharedPtr<FJsonValue>>* MaterialsArray = GetOptionalArray(Params, TEXT("post_process_materials"));
	if (MaterialsArray)
	{
		for (const TSharedPtr<FJsonValue>& MatValue : *MaterialsArray)
		{
			FString MatName = MatValue->AsString();
			FString MatError;
			UMaterial* Mat = FindMaterial(MatName, MatError);
			if (Mat)
			{
				// Add as weighted blendable
				Volume->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, Mat));
			}
			else
			{
				UE_LOG(LogMCP, Warning, TEXT("FCreatePostProcessVolumeAction: Material '%s' not found"), *MatName);
			}
		}
	}

	// Update context
	Context.LastCreatedActorName = ActorName;

	// Mark level dirty
	World->MarkPackageDirty();

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), ActorName);

	TArray<TSharedPtr<FJsonValue>> LocationArray;
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.X));
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Y));
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Z));
	Result->SetArrayField(TEXT("location"), LocationArray);

	Result->SetBoolField(TEXT("infinite_extent"), bInfiniteExtent);
	Result->SetNumberField(TEXT("priority"), Priority);

	return CreateSuccessResponse(Result);
}


