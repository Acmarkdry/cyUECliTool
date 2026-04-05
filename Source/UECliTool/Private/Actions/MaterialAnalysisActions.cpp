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
// FAnalyzeMaterialComplexityAction
// =========================================================================

bool FAnalyzeMaterialComplexityAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MaterialName;
	return GetRequiredString(Params, TEXT("material_name"), MaterialName, OutError);
}

TSharedPtr<FJsonObject> FAnalyzeMaterialComplexityAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString MaterialName;
	GetRequiredString(Params, TEXT("material_name"), MaterialName, Error);

	UMaterial* Material = FindMaterial(MaterialName, Error);
	if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressionCollection().Expressions;

	// --- Node count and type distribution ---
	int32 NodeCount = 0;
	TMap<FString, int32> TypeDistribution;

	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr || Expr->IsA<UMaterialExpressionComment>())
		{
			continue;
		}
		++NodeCount;
		FString ClassName = Expr->GetClass()->GetName();
		// Strip "MaterialExpression" prefix for readability
		ClassName.RemoveFromStart(TEXT("MaterialExpression"));
		int32& Count = TypeDistribution.FindOrAdd(ClassName);
		++Count;
	}

	// --- Connection count ---
	int32 ConnectionCount = 0;
	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr || Expr->IsA<UMaterialExpressionComment>())
		{
			continue;
		}
		for (int32 InputIdx = 0; ; ++InputIdx)
		{
			FExpressionInput* Input = Expr->GetInput(InputIdx);
			if (!Input)
			{
				break;
			}
			if (Input->Expression != nullptr)
			{
				++ConnectionCount;
			}
		}
	}
	// Also count connections from material output pins
	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (EditorData)
	{
		auto CountOutput = [&](const FExpressionInput& Input)
		{
			if (Input.Expression != nullptr)
			{
				++ConnectionCount;
			}
		};
		CountOutput(EditorData->BaseColor);
		CountOutput(EditorData->EmissiveColor);
		CountOutput(EditorData->Metallic);
		CountOutput(EditorData->Roughness);
		CountOutput(EditorData->Specular);
		CountOutput(EditorData->Normal);
		CountOutput(EditorData->Opacity);
		CountOutput(EditorData->OpacityMask);
		CountOutput(EditorData->AmbientOcclusion);
		CountOutput(EditorData->WorldPositionOffset);
		CountOutput(EditorData->Refraction);
	}

	// --- Shader info ---
	uint32 VSSamplers = 0;
	uint32 PSSamplers = 0;
	bool bCompiled = false;

	const FMaterialResource* MatResource = Material->GetMaterialResource(GMaxRHIFeatureLevel);
	if (MatResource)
	{
#if WITH_EDITOR
		MatResource->GetEstimatedNumTextureSamples(VSSamplers, PSSamplers);
		bCompiled = (MatResource->GetGameThreadShaderMap() != nullptr);
#endif
	}

	// --- Parameters ---
	TArray<TSharedPtr<FJsonValue>> ParametersArray;
	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr)
		{
			continue;
		}

		TSharedPtr<FJsonObject> ParamObj;

		if (UMaterialExpressionScalarParameter* ScalarParam = Cast<UMaterialExpressionScalarParameter>(Expr))
		{
			ParamObj = MakeShared<FJsonObject>();
			ParamObj->SetStringField(TEXT("name"), ScalarParam->ParameterName.ToString());
			ParamObj->SetStringField(TEXT("type"), TEXT("ScalarParameter"));
			ParamObj->SetNumberField(TEXT("default_value"), ScalarParam->DefaultValue);
		}
		else if (UMaterialExpressionVectorParameter* VectorParam = Cast<UMaterialExpressionVectorParameter>(Expr))
		{
			ParamObj = MakeShared<FJsonObject>();
			ParamObj->SetStringField(TEXT("name"), VectorParam->ParameterName.ToString());
			ParamObj->SetStringField(TEXT("type"), TEXT("VectorParameter"));
			TArray<TSharedPtr<FJsonValue>> DefaultArr;
			DefaultArr.Add(MakeShared<FJsonValueNumber>(VectorParam->DefaultValue.R));
			DefaultArr.Add(MakeShared<FJsonValueNumber>(VectorParam->DefaultValue.G));
			DefaultArr.Add(MakeShared<FJsonValueNumber>(VectorParam->DefaultValue.B));
			DefaultArr.Add(MakeShared<FJsonValueNumber>(VectorParam->DefaultValue.A));
			ParamObj->SetArrayField(TEXT("default_value"), DefaultArr);
		}
		else if (UMaterialExpressionTextureSampleParameter2D* TexParam = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
		{
			ParamObj = MakeShared<FJsonObject>();
			ParamObj->SetStringField(TEXT("name"), TexParam->ParameterName.ToString());
			ParamObj->SetStringField(TEXT("type"), TEXT("TextureSampleParameter2D"));
			FString TexPath = TexParam->Texture ? TexParam->Texture->GetPathName() : TEXT("");
			ParamObj->SetStringField(TEXT("default_value"), TexPath);
		}
		else if (UMaterialExpressionStaticSwitchParameter* SwitchParam = Cast<UMaterialExpressionStaticSwitchParameter>(Expr))
		{
			ParamObj = MakeShared<FJsonObject>();
			ParamObj->SetStringField(TEXT("name"), SwitchParam->ParameterName.ToString());
			ParamObj->SetStringField(TEXT("type"), TEXT("StaticSwitchParameter"));
			ParamObj->SetBoolField(TEXT("default_value"), SwitchParam->DefaultValue);
		}

		if (ParamObj.IsValid())
		{
			ParametersArray.Add(MakeShared<FJsonValueObject>(ParamObj));
		}
	}

	// --- Texture samples ---
	TArray<TSharedPtr<FJsonValue>> TextureSamplesArray;
	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr)
		{
			continue;
		}

		// TextureSampleParameter2D is a subclass of TextureSample, check it first
		if (UMaterialExpressionTextureSampleParameter2D* TexParam = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
		{
			TSharedPtr<FJsonObject> SampleObj = MakeShared<FJsonObject>();
			SampleObj->SetStringField(TEXT("node_name"), TexParam->ParameterName.ToString());
			SampleObj->SetStringField(TEXT("type"), TEXT("TextureSampleParameter2D"));
			SampleObj->SetStringField(TEXT("texture_path"), TexParam->Texture ? TexParam->Texture->GetPathName() : TEXT(""));
			TextureSamplesArray.Add(MakeShared<FJsonValueObject>(SampleObj));
		}
		else if (UMaterialExpressionTextureSample* TexSample = Cast<UMaterialExpressionTextureSample>(Expr))
		{
			TSharedPtr<FJsonObject> SampleObj = MakeShared<FJsonObject>();
			// Use Desc if set, otherwise fall back to UObject name
			FString NodeName = !Expr->Desc.IsEmpty() ? Expr->Desc : Expr->GetName();
			SampleObj->SetStringField(TEXT("node_name"), NodeName);
			SampleObj->SetStringField(TEXT("type"), TEXT("TextureSample"));
			SampleObj->SetStringField(TEXT("texture_path"), TexSample->Texture ? TexSample->Texture->GetPathName() : TEXT(""));
			TextureSamplesArray.Add(MakeShared<FJsonValueObject>(SampleObj));
		}
	}

	// --- Build response ---
	TSharedPtr<FJsonObject> NodeTypeDist = MakeShared<FJsonObject>();
	for (const auto& Pair : TypeDistribution)
	{
		NodeTypeDist->SetNumberField(Pair.Key, Pair.Value);
	}

	TSharedPtr<FJsonObject> ShaderInfo = MakeShared<FJsonObject>();
	ShaderInfo->SetNumberField(TEXT("vs_samplers"), VSSamplers);
	ShaderInfo->SetNumberField(TEXT("ps_samplers"), PSSamplers);
	ShaderInfo->SetBoolField(TEXT("compiled"), bCompiled);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("material_name"), MaterialName);
	Result->SetNumberField(TEXT("node_count"), NodeCount);
	Result->SetObjectField(TEXT("node_type_distribution"), NodeTypeDist);
	Result->SetNumberField(TEXT("connection_count"), ConnectionCount);
	Result->SetObjectField(TEXT("shader_info"), ShaderInfo);
	Result->SetArrayField(TEXT("parameters"), ParametersArray);
	Result->SetArrayField(TEXT("texture_samples"), TextureSamplesArray);

	return CreateSuccessResponse(Result);
}

// =========================================================================
// FAnalyzeMaterialDependenciesAction
// =========================================================================

bool FAnalyzeMaterialDependenciesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MaterialName;
	return GetRequiredString(Params, TEXT("material_name"), MaterialName, OutError);
}

TSharedPtr<FJsonObject> FAnalyzeMaterialDependenciesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString MaterialName;
	GetRequiredString(Params, TEXT("material_name"), MaterialName, Error);

	UMaterial* Material = FindMaterial(MaterialName, Error);
	if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressionCollection().Expressions;

	// --- External assets ---
	TArray<TSharedPtr<FJsonValue>> ExternalAssetsArray;
	TSet<FString> AddedPaths;

	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr)
		{
			continue;
		}

		if (UMaterialExpressionTextureSample* TexSample = Cast<UMaterialExpressionTextureSample>(Expr))
		{
			if (TexSample->Texture != nullptr)
			{
				FString Path = TexSample->Texture->GetPathName();
				if (!AddedPaths.Contains(Path))
				{
					AddedPaths.Add(Path);
					FString NodeName = !Expr->Desc.IsEmpty() ? Expr->Desc : Expr->GetName();
					TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
					AssetObj->SetStringField(TEXT("type"), TEXT("Texture"));
					AssetObj->SetStringField(TEXT("path"), Path);
					AssetObj->SetStringField(TEXT("node_name"), NodeName);
					ExternalAssetsArray.Add(MakeShared<FJsonValueObject>(AssetObj));
				}
			}
		}
		else if (UMaterialExpressionMaterialFunctionCall* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
		{
			if (FuncCall->MaterialFunction != nullptr)
			{
				FString Path = FuncCall->MaterialFunction->GetPathName();
				if (!AddedPaths.Contains(Path))
				{
					AddedPaths.Add(Path);
					FString NodeName = !Expr->Desc.IsEmpty() ? Expr->Desc : Expr->GetName();
					TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
					AssetObj->SetStringField(TEXT("type"), TEXT("MaterialFunction"));
					AssetObj->SetStringField(TEXT("path"), Path);
					AssetObj->SetStringField(TEXT("node_name"), NodeName);
					ExternalAssetsArray.Add(MakeShared<FJsonValueObject>(AssetObj));
				}
			}
		}
	}

	// --- Level references ---
	TArray<TSharedPtr<FJsonValue>> LevelReferencesArray;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World)
	{
		for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
		{
			AActor* Actor = *ActorIt;
			if (!Actor)
			{
				continue;
			}

			TArray<UPrimitiveComponent*> PrimComponents;
			Actor->GetComponents<UPrimitiveComponent>(PrimComponents);

			for (UPrimitiveComponent* Comp : PrimComponents)
			{
				if (!Comp)
				{
					continue;
				}

				int32 NumMaterials = Comp->GetNumMaterials();
				for (int32 SlotIdx = 0; SlotIdx < NumMaterials; ++SlotIdx)
				{
					UMaterialInterface* MatInterface = Comp->GetMaterial(SlotIdx);
					if (!MatInterface)
					{
						continue;
					}

					bool bMatches = false;
					if (MatInterface == Material)
					{
						bMatches = true;
					}
					else if (UMaterialInstance* MatInst = Cast<UMaterialInstance>(MatInterface))
					{
						if (MatInst->GetBaseMaterial() == Material)
						{
							bMatches = true;
						}
					}

					if (bMatches)
					{
						TSharedPtr<FJsonObject> RefObj = MakeShared<FJsonObject>();
						RefObj->SetStringField(TEXT("actor_name"), Actor->GetActorLabel());
						RefObj->SetStringField(TEXT("component_name"), Comp->GetName());
						LevelReferencesArray.Add(MakeShared<FJsonValueObject>(RefObj));
						break; // One entry per component is enough
					}
				}
			}
		}
	}

	// --- Build response ---
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("material_name"), MaterialName);
	Result->SetArrayField(TEXT("external_assets"), ExternalAssetsArray);
	Result->SetArrayField(TEXT("level_references"), LevelReferencesArray);
	Result->SetNumberField(TEXT("level_reference_count"), LevelReferencesArray.Num());

	return CreateSuccessResponse(Result);
}

// =========================================================================
// FDiagnoseMaterialAction
// =========================================================================

bool FDiagnoseMaterialAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MaterialName;
	return GetRequiredString(Params, TEXT("material_name"), MaterialName, OutError);
}

TSharedPtr<FJsonObject> FDiagnoseMaterialAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString MaterialName;
	GetRequiredString(Params, TEXT("material_name"), MaterialName, Error);

	UMaterial* Material = FindMaterial(MaterialName, Error);
	if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressionCollection().Expressions;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;

	// --- Rule 1: Domain/BlendMode incompatibility ---
	if (Material->MaterialDomain == MD_PostProcess && Material->BlendMode != BLEND_Opaque)
	{
		TSharedPtr<FJsonObject> DiagObj = MakeShared<FJsonObject>();
		DiagObj->SetStringField(TEXT("severity"), TEXT("error"));
		DiagObj->SetStringField(TEXT("code"), TEXT("domain_blend_incompatible"));
		DiagObj->SetStringField(TEXT("message"), TEXT("PostProcess material must use Opaque blend mode"));
		DiagnosticsArray.Add(MakeShared<FJsonValueObject>(DiagObj));
	}

	// --- Rule 2: Texture sample count > 16 ---
	int32 TextureSampleCount = 0;
	for (UMaterialExpression* Expr : Expressions)
	{
		if (Expr && Expr->IsA<UMaterialExpressionTextureSample>())
		{
			++TextureSampleCount;
		}
	}
	if (TextureSampleCount > 16)
	{
		TSharedPtr<FJsonObject> DiagObj = MakeShared<FJsonObject>();
		DiagObj->SetStringField(TEXT("severity"), TEXT("warning"));
		DiagObj->SetStringField(TEXT("code"), TEXT("texture_sample_limit"));
		DiagObj->SetStringField(TEXT("message"),
			FString::Printf(TEXT("Material has %d texture samples (limit: 16)"), TextureSampleCount));
		DiagnosticsArray.Add(MakeShared<FJsonValueObject>(DiagObj));
	}

	// --- Rule 3: Orphan nodes ---
	// Build set of all expressions referenced by other expressions or material output pins
	TSet<UMaterialExpression*> ReferencedExprs;

	// Scan all expression inputs
	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr || Expr->IsA<UMaterialExpressionComment>())
		{
			continue;
		}
		for (int32 InputIdx = 0; ; ++InputIdx)
		{
			FExpressionInput* Input = Expr->GetInput(InputIdx);
			if (!Input)
			{
				break;
			}
			if (Input->Expression != nullptr)
			{
				ReferencedExprs.Add(Input->Expression);
			}
		}
	}

	// Scan material output pins
	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (EditorData)
	{
		auto MarkReferenced = [&](const FExpressionInput& Input)
		{
			if (Input.Expression != nullptr)
			{
				ReferencedExprs.Add(Input.Expression);
			}
		};
		MarkReferenced(EditorData->BaseColor);
		MarkReferenced(EditorData->EmissiveColor);
		MarkReferenced(EditorData->Metallic);
		MarkReferenced(EditorData->Roughness);
		MarkReferenced(EditorData->Specular);
		MarkReferenced(EditorData->Normal);
		MarkReferenced(EditorData->Opacity);
		MarkReferenced(EditorData->OpacityMask);
		MarkReferenced(EditorData->AmbientOcclusion);
		MarkReferenced(EditorData->WorldPositionOffset);
		MarkReferenced(EditorData->Refraction);
	}

	// Any non-comment expression not in the referenced set is an orphan
	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr || Expr->IsA<UMaterialExpressionComment>())
		{
			continue;
		}
		if (!ReferencedExprs.Contains(Expr))
		{
			FString NodeName = !Expr->Desc.IsEmpty() ? Expr->Desc : Expr->GetName();
			TSharedPtr<FJsonObject> DiagObj = MakeShared<FJsonObject>();
			DiagObj->SetStringField(TEXT("severity"), TEXT("warning"));
			DiagObj->SetStringField(TEXT("code"), TEXT("orphan_node"));
			DiagObj->SetStringField(TEXT("message"),
				FString::Printf(TEXT("Node '%s' is not connected to any output"), *NodeName));
			DiagObj->SetStringField(TEXT("node_name"), NodeName);
			DiagnosticsArray.Add(MakeShared<FJsonValueObject>(DiagObj));
		}
	}

	// --- Rule 4: Custom HLSL nodes ---
	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr)
		{
			continue;
		}
		if (UMaterialExpressionCustom* CustomExpr = Cast<UMaterialExpressionCustom>(Expr))
		{
			FString NodeName = !Expr->Desc.IsEmpty() ? Expr->Desc : Expr->GetName();
			TSharedPtr<FJsonObject> DiagObj = MakeShared<FJsonObject>();
			DiagObj->SetStringField(TEXT("severity"), TEXT("info"));
			DiagObj->SetStringField(TEXT("code"), TEXT("custom_hlsl"));
			DiagObj->SetStringField(TEXT("message"),
				FString::Printf(TEXT("Custom HLSL node '%s' disables some shader optimizations"), *NodeName));
			DiagObj->SetStringField(TEXT("node_name"), NodeName);
			DiagnosticsArray.Add(MakeShared<FJsonValueObject>(DiagObj));
		}
	}

	// --- Build response ---
	FString Status = DiagnosticsArray.Num() == 0 ? TEXT("healthy") : TEXT("has_issues");

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("material_name"), MaterialName);
	Result->SetStringField(TEXT("status"), Status);
	Result->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);

	return CreateSuccessResponse(Result);
}

// =========================================================================
// FDiffMaterialsAction
// =========================================================================

bool FDiffMaterialsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString NameA, NameB;
	if (!GetRequiredString(Params, TEXT("material_name_a"), NameA, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("material_name_b"), NameB, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FDiffMaterialsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString MaterialNameA, MaterialNameB;
	GetRequiredString(Params, TEXT("material_name_a"), MaterialNameA, Error);
	GetRequiredString(Params, TEXT("material_name_b"), MaterialNameB, Error);

	UMaterial* MaterialA = FindMaterial(MaterialNameA, Error);
	if (!MaterialA)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Material '%s' not found"), *MaterialNameA),
			TEXT("material_not_found"));
	}

	UMaterial* MaterialB = FindMaterial(MaterialNameB, Error);
	if (!MaterialB)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Material '%s' not found"), *MaterialNameB),
			TEXT("material_not_found"));
	}

	// Helper lambda: compute node_count, connection_count, and parameter name set for a material
	auto ComputeStats = [](UMaterial* Mat, int32& OutNodeCount, int32& OutConnectionCount, TSet<FString>& OutParamNames)
	{
		OutNodeCount = 0;
		OutConnectionCount = 0;

		const TArray<TObjectPtr<UMaterialExpression>>& Exprs = Mat->GetExpressionCollection().Expressions;

		for (UMaterialExpression* Expr : Exprs)
		{
			if (!Expr || Expr->IsA<UMaterialExpressionComment>())
			{
				continue;
			}
			++OutNodeCount;

			// Count connections from this expression's inputs
			for (int32 InputIdx = 0; ; ++InputIdx)
			{
				FExpressionInput* Input = Expr->GetInput(InputIdx);
				if (!Input)
				{
					break;
				}
				if (Input->Expression != nullptr)
				{
					++OutConnectionCount;
				}
			}

			// Collect parameter names
			if (auto* ScalarP = Cast<UMaterialExpressionScalarParameter>(Expr))
			{
				OutParamNames.Add(ScalarP->ParameterName.ToString());
			}
			else if (auto* VectorP = Cast<UMaterialExpressionVectorParameter>(Expr))
			{
				OutParamNames.Add(VectorP->ParameterName.ToString());
			}
			else if (auto* TexP = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
			{
				OutParamNames.Add(TexP->ParameterName.ToString());
			}
			else if (auto* SwitchP = Cast<UMaterialExpressionStaticSwitchParameter>(Expr))
			{
				OutParamNames.Add(SwitchP->ParameterName.ToString());
			}
		}

		// Count connections from material output pins
		UMaterialEditorOnlyData* EditorData = Mat->GetEditorOnlyData();
		if (EditorData)
		{
			auto CountOutput = [&](const FExpressionInput& Input)
			{
				if (Input.Expression != nullptr)
				{
					++OutConnectionCount;
				}
			};
			CountOutput(EditorData->BaseColor);
			CountOutput(EditorData->EmissiveColor);
			CountOutput(EditorData->Metallic);
			CountOutput(EditorData->Roughness);
			CountOutput(EditorData->Specular);
			CountOutput(EditorData->Normal);
			CountOutput(EditorData->Opacity);
			CountOutput(EditorData->OpacityMask);
			CountOutput(EditorData->AmbientOcclusion);
			CountOutput(EditorData->WorldPositionOffset);
			CountOutput(EditorData->Refraction);
		}
	};

	int32 NodeCountA = 0, NodeCountB = 0;
	int32 ConnectionCountA = 0, ConnectionCountB = 0;
	TSet<FString> ParamNamesA, ParamNamesB;

	ComputeStats(MaterialA, NodeCountA, ConnectionCountA, ParamNamesA);
	ComputeStats(MaterialB, NodeCountB, ConnectionCountB, ParamNamesB);

	// --- Domain string conversion ---
	auto DomainToString = [](EMaterialDomain Domain) -> FString
	{
		switch (Domain)
		{
		case MD_Surface:        return TEXT("Surface");
		case MD_PostProcess:    return TEXT("PostProcess");
		case MD_DeferredDecal:  return TEXT("DeferredDecal");
		case MD_LightFunction:  return TEXT("LightFunction");
		case MD_UI:             return TEXT("UI");
		case MD_Volume:         return TEXT("Volume");
		default:                return TEXT("Surface");
		}
	};

	// --- BlendMode string conversion ---
	auto BlendModeToString = [](EBlendMode BlendMode) -> FString
	{
		switch (BlendMode)
		{
		case BLEND_Opaque:      return TEXT("Opaque");
		case BLEND_Masked:      return TEXT("Masked");
		case BLEND_Translucent: return TEXT("Translucent");
		case BLEND_Additive:    return TEXT("Additive");
		case BLEND_Modulate:    return TEXT("Modulate");
		default:                return TEXT("Opaque");
		}
	};

	// --- Property diffs ---
	TArray<TSharedPtr<FJsonValue>> PropertyDiffsArray;

	if (MaterialA->MaterialDomain != MaterialB->MaterialDomain)
	{
		TSharedPtr<FJsonObject> DiffObj = MakeShared<FJsonObject>();
		DiffObj->SetStringField(TEXT("property"), TEXT("domain"));
		DiffObj->SetStringField(TEXT("value_a"), DomainToString(MaterialA->MaterialDomain));
		DiffObj->SetStringField(TEXT("value_b"), DomainToString(MaterialB->MaterialDomain));
		PropertyDiffsArray.Add(MakeShared<FJsonValueObject>(DiffObj));
	}

	if (MaterialA->BlendMode != MaterialB->BlendMode)
	{
		TSharedPtr<FJsonObject> DiffObj = MakeShared<FJsonObject>();
		DiffObj->SetStringField(TEXT("property"), TEXT("blend_mode"));
		DiffObj->SetStringField(TEXT("value_a"), BlendModeToString(MaterialA->BlendMode));
		DiffObj->SetStringField(TEXT("value_b"), BlendModeToString(MaterialB->BlendMode));
		PropertyDiffsArray.Add(MakeShared<FJsonValueObject>(DiffObj));
	}

	// --- Parameter set differences ---
	TArray<TSharedPtr<FJsonValue>> ParamsOnlyInA;
	TArray<TSharedPtr<FJsonValue>> ParamsOnlyInB;

	for (const FString& ParamName : ParamNamesA)
	{
		if (!ParamNamesB.Contains(ParamName))
		{
			ParamsOnlyInA.Add(MakeShared<FJsonValueString>(ParamName));
		}
	}
	for (const FString& ParamName : ParamNamesB)
	{
		if (!ParamNamesA.Contains(ParamName))
		{
			ParamsOnlyInB.Add(MakeShared<FJsonValueString>(ParamName));
		}
	}

	// --- Build summary ---
	TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
	Summary->SetNumberField(TEXT("node_count_a"), NodeCountA);
	Summary->SetNumberField(TEXT("node_count_b"), NodeCountB);
	Summary->SetNumberField(TEXT("node_count_diff"), NodeCountA - NodeCountB);
	Summary->SetNumberField(TEXT("connection_count_diff"), ConnectionCountA - ConnectionCountB);

	// --- Build response ---
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("material_a"), MaterialNameA);
	Result->SetStringField(TEXT("material_b"), MaterialNameB);
	Result->SetObjectField(TEXT("summary"), Summary);
	Result->SetArrayField(TEXT("property_diffs"), PropertyDiffsArray);
	Result->SetArrayField(TEXT("parameters_only_in_a"), ParamsOnlyInA);
	Result->SetArrayField(TEXT("parameters_only_in_b"), ParamsOnlyInB);

	return CreateSuccessResponse(Result);
}


// =========================================================================
// FExtractMaterialParametersAction
// =========================================================================

bool FExtractMaterialParametersAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MaterialName;
	return GetRequiredString(Params, TEXT("material_name"), MaterialName, OutError);
}

TSharedPtr<FJsonObject> FExtractMaterialParametersAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString MaterialName;
	GetRequiredString(Params, TEXT("material_name"), MaterialName, Error);

	UMaterial* Material = FindMaterial(MaterialName, Error);
	if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressionCollection().Expressions;
	TArray<TSharedPtr<FJsonValue>> ParametersArray;

	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr) { continue; }

		// Cast to base parameter class to access Group and SortPriority
		UMaterialExpressionParameter* BaseParam = Cast<UMaterialExpressionParameter>(Expr);
		if (!BaseParam) { continue; }

		TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
		ParamObj->SetStringField(TEXT("name"), BaseParam->ParameterName.ToString());
		ParamObj->SetStringField(TEXT("group"), BaseParam->Group.ToString());
		ParamObj->SetNumberField(TEXT("sort_priority"), BaseParam->SortPriority);

		if (auto* ScalarP2 = Cast<UMaterialExpressionScalarParameter>(Expr))
		{
			ParamObj->SetStringField(TEXT("type"), TEXT("ScalarParameter"));
			ParamObj->SetNumberField(TEXT("default_value"), ScalarP2->DefaultValue);
		}
		else if (auto* VectorP2 = Cast<UMaterialExpressionVectorParameter>(Expr))
		{
			ParamObj->SetStringField(TEXT("type"), TEXT("VectorParameter"));
			TArray<TSharedPtr<FJsonValue>> ColorArr;
			ColorArr.Add(MakeShared<FJsonValueNumber>(VectorP2->DefaultValue.R));
			ColorArr.Add(MakeShared<FJsonValueNumber>(VectorP2->DefaultValue.G));
			ColorArr.Add(MakeShared<FJsonValueNumber>(VectorP2->DefaultValue.B));
			ColorArr.Add(MakeShared<FJsonValueNumber>(VectorP2->DefaultValue.A));
			ParamObj->SetArrayField(TEXT("default_value"), ColorArr);
		}
		else if (auto* TexP2 = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
		{
			ParamObj->SetStringField(TEXT("type"), TEXT("TextureSampleParameter2D"));
			ParamObj->SetStringField(TEXT("default_value"), TexP2->Texture ? TexP2->Texture->GetPathName() : TEXT(""));
		}
		else if (auto* TexObjP2 = Cast<UMaterialExpressionTextureObjectParameter>(Expr))
		{
			(void)TexObjP2;
			ParamObj->SetStringField(TEXT("type"), TEXT("TextureObjectParameter"));
			ParamObj->SetStringField(TEXT("default_value"), TEXT(""));
		}
		else if (auto* SwitchP2 = Cast<UMaterialExpressionStaticSwitchParameter>(Expr))
		{
			ParamObj->SetStringField(TEXT("type"), TEXT("StaticSwitchParameter"));
			ParamObj->SetBoolField(TEXT("default_value"), SwitchP2->DefaultValue);
		}
		else
		{
			continue; // Unknown parameter subtype
		}

		ParametersArray.Add(MakeShared<FJsonValueObject>(ParamObj));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("material_name"), MaterialName);
	Result->SetArrayField(TEXT("parameters"), ParametersArray);

	return CreateSuccessResponse(Result);
}

// =========================================================================
// FBatchCreateMaterialInstancesAction
// =========================================================================

bool FBatchCreateMaterialInstancesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ParentMaterial;
	if (!GetRequiredString(Params, TEXT("parent_material"), ParentMaterial, OutError)) return false;

	const TArray<TSharedPtr<FJsonValue>>* InstancesArray;
	if (!Params->TryGetArrayField(TEXT("instances"), InstancesArray) || InstancesArray->Num() == 0)
	{
		OutError = TEXT("'instances' array is required and must be non-empty");
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FBatchCreateMaterialInstancesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString ParentMaterialName;
	GetRequiredString(Params, TEXT("parent_material"), ParentMaterialName, Error);

	UMaterial* ParentMaterial = FindMaterial(ParentMaterialName, Error);
	if (!ParentMaterial)
	{
		return CreateErrorResponse(Error, TEXT("parent_not_found"));
	}

	const TArray<TSharedPtr<FJsonValue>>* InstancesArray;
	Params->TryGetArrayField(TEXT("instances"), InstancesArray);

	int32 CreatedCount = 0;
	int32 FailedCount = 0;
	TArray<TSharedPtr<FJsonValue>> ResultsArray;

	for (const TSharedPtr<FJsonValue>& InstanceVal : *InstancesArray)
	{
		const TSharedPtr<FJsonObject>* InstanceObjPtr;
		if (!InstanceVal->TryGetObject(InstanceObjPtr) || !InstanceObjPtr->IsValid())
		{
			++FailedCount;
			TSharedPtr<FJsonObject> FailObj = MakeShared<FJsonObject>();
			FailObj->SetStringField(TEXT("name"), TEXT(""));
			FailObj->SetBoolField(TEXT("success"), false);
			FailObj->SetStringField(TEXT("error"), TEXT("Instance entry is not a valid JSON object"));
			ResultsArray.Add(MakeShared<FJsonValueObject>(FailObj));
			continue;
		}

		const TSharedPtr<FJsonObject>& InstanceObj = *InstanceObjPtr;

		FString InstanceName;
		if (!InstanceObj->TryGetStringField(TEXT("name"), InstanceName) || InstanceName.IsEmpty())
		{
			++FailedCount;
			TSharedPtr<FJsonObject> FailObj = MakeShared<FJsonObject>();
			FailObj->SetStringField(TEXT("name"), TEXT(""));
			FailObj->SetBoolField(TEXT("success"), false);
			FailObj->SetStringField(TEXT("error"), TEXT("Instance 'name' is required"));
			ResultsArray.Add(MakeShared<FJsonValueObject>(FailObj));
			continue;
		}

		FString InstancePath = TEXT("/Game/Materials");
		InstanceObj->TryGetStringField(TEXT("path"), InstancePath);

		// Validate scalar_parameters: values must be JSON numbers
		FString ValidationError;
		if (InstanceObj->HasField(TEXT("scalar_parameters")))
		{
			const TSharedPtr<FJsonObject> ScalarParams = InstanceObj->GetObjectField(TEXT("scalar_parameters"));
			for (const auto& Pair : ScalarParams->Values)
			{
				if (Pair.Value->Type != EJson::Number)
				{
					ValidationError = FString::Printf(
						TEXT("Invalid scalar parameter '%s': expected number, got %s"),
						*Pair.Key,
						Pair.Value->Type == EJson::String ? TEXT("string") :
						Pair.Value->Type == EJson::Array  ? TEXT("array")  :
						Pair.Value->Type == EJson::Object ? TEXT("object") : TEXT("non-number"));
					break;
				}
			}
		}

		// Validate vector_parameters: values must be 4-element arrays
		if (ValidationError.IsEmpty() && InstanceObj->HasField(TEXT("vector_parameters")))
		{
			const TSharedPtr<FJsonObject> VectorParams = InstanceObj->GetObjectField(TEXT("vector_parameters"));
			for (const auto& Pair : VectorParams->Values)
			{
				const TArray<TSharedPtr<FJsonValue>>* Arr;
				if (!Pair.Value->TryGetArray(Arr) || Arr->Num() != 4)
				{
					ValidationError = FString::Printf(
						TEXT("Invalid vector parameter '%s': expected 4-element array"), *Pair.Key);
					break;
				}
			}
		}

if (!ValidationError.IsEmpty())
{
++FailedCount;
TSharedPtr<FJsonObject> FailObj = MakeShared<FJsonObject>();
FailObj->SetStringField(TEXT("name"), InstanceName);
FailObj->SetBoolField(TEXT("success"), false);
FailObj->SetStringField(TEXT("error"), ValidationError);
ResultsArray.Add(MakeShared<FJsonValueObject>(FailObj));
continue;
}

// Create the instance
FString InstancePackagePath = InstancePath / InstanceName;

// Clean up existing
UPackage* ExistingPackage = FindPackage(nullptr, *InstancePackagePath);
if (ExistingPackage)
{
UMaterialInstanceConstant* ExistingInstance = FindObject<UMaterialInstanceConstant>(ExistingPackage, *InstanceName);
if (ExistingInstance)
{
FString TempName = FString::Printf(TEXT("%s_TEMP_%d"), *InstanceName, FMath::Rand());
ExistingInstance->Rename(*TempName, GetTransientPackage(),
REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
ExistingInstance->MarkAsGarbage();
ExistingPackage->MarkAsGarbage();
}
}
if (UEditorAssetLibrary::DoesAssetExist(InstancePackagePath))
{
UEditorAssetLibrary::DeleteAsset(InstancePackagePath);
}

UPackage* Package = CreatePackage(*InstancePackagePath);
if (!Package)
{
++FailedCount;
TSharedPtr<FJsonObject> FailObj = MakeShared<FJsonObject>();
FailObj->SetStringField(TEXT("name"), InstanceName);
FailObj->SetBoolField(TEXT("success"), false);
FailObj->SetStringField(TEXT("error"), TEXT("Failed to create package"));
ResultsArray.Add(MakeShared<FJsonValueObject>(FailObj));
continue;
}
Package->FullyLoad();

UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
Factory->InitialParent = ParentMaterial;

UMaterialInstanceConstant* NewInstance = Cast<UMaterialInstanceConstant>(Factory->FactoryCreateNew(
UMaterialInstanceConstant::StaticClass(), Package, *InstanceName,
RF_Public | RF_Standalone, nullptr, GWarn));

if (!NewInstance)
{
++FailedCount;
TSharedPtr<FJsonObject> FailObj = MakeShared<FJsonObject>();
FailObj->SetStringField(TEXT("name"), InstanceName);
FailObj->SetBoolField(TEXT("success"), false);
FailObj->SetStringField(TEXT("error"), TEXT("Failed to create material instance"));
ResultsArray.Add(MakeShared<FJsonValueObject>(FailObj));
continue;
}

// Set scalar parameters
if (InstanceObj->HasField(TEXT("scalar_parameters")))
{
const TSharedPtr<FJsonObject> ScalarParams = InstanceObj->GetObjectField(TEXT("scalar_parameters"));
for (const auto& Pair : ScalarParams->Values)
{
NewInstance->SetScalarParameterValueEditorOnly(FName(*Pair.Key), Pair.Value->AsNumber());
}
}

// Set vector parameters
if (InstanceObj->HasField(TEXT("vector_parameters")))
{
const TSharedPtr<FJsonObject> VectorParams = InstanceObj->GetObjectField(TEXT("vector_parameters"));
for (const auto& Pair : VectorParams->Values)
{
const TArray<TSharedPtr<FJsonValue>>* Arr;
if (Pair.Value->TryGetArray(Arr) && Arr->Num() >= 3)
{
FLinearColor Color(
(*Arr)[0]->AsNumber(),
(*Arr)[1]->AsNumber(),
(*Arr)[2]->AsNumber(),
Arr->Num() > 3 ? (*Arr)[3]->AsNumber() : 1.0f);
NewInstance->SetVectorParameterValueEditorOnly(FName(*Pair.Key), Color);
}
}
}

// Set texture parameters
if (InstanceObj->HasField(TEXT("texture_parameters")))
{
const TSharedPtr<FJsonObject> TextureParams = InstanceObj->GetObjectField(TEXT("texture_parameters"));
for (const auto& Pair : TextureParams->Values)
{
FString TexturePath = Pair.Value->AsString();
UTexture* Texture = LoadObject<UTexture>(nullptr, *TexturePath);
if (!Texture)
{
Texture = LoadObject<UTexture>(nullptr, *(TEXT("/Game/") + TexturePath));
}
if (Texture)
{
NewInstance->SetTextureParameterValueEditorOnly(FName(*Pair.Key), Texture);
}
}
}

// Set static switch parameters
if (InstanceObj->HasField(TEXT("static_switch_parameters")))
{
const TSharedPtr<FJsonObject> SwitchParams = InstanceObj->GetObjectField(TEXT("static_switch_parameters"));
for (const auto& Pair : SwitchParams->Values)
{
NewInstance->SetStaticSwitchParameterValueEditorOnly(FName(*Pair.Key), Pair.Value->AsBool());
}
}

Package->SetDirtyFlag(true);
NewInstance->MarkPackageDirty();
FAssetRegistryModule::AssetCreated(NewInstance);
Context.MarkPackageDirty(Package);

++CreatedCount;
TSharedPtr<FJsonObject> SuccessObj = MakeShared<FJsonObject>();
SuccessObj->SetStringField(TEXT("name"), InstanceName);
SuccessObj->SetStringField(TEXT("path"), InstancePackagePath);
SuccessObj->SetBoolField(TEXT("success"), true);
ResultsArray.Add(MakeShared<FJsonValueObject>(SuccessObj));
}

TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
Result->SetNumberField(TEXT("created_count"), CreatedCount);
Result->SetNumberField(TEXT("failed_count"), FailedCount);
Result->SetArrayField(TEXT("results"), ResultsArray);

return CreateSuccessResponse(Result);
}

