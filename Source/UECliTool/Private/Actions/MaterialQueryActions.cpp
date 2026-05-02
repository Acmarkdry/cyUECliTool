// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/MaterialActions.h"
#include "MCPContext.h"
#include "MCPCommonUtils.h"
#include "MaterialCommonHelpers.h"

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
// FGetMaterialSummaryAction (P4.1)
// =========================================================================

bool FGetMaterialSummaryAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MaterialName;
	return GetRequiredString(Params, TEXT("material_name"), MaterialName, OutError);
}

FString FGetMaterialSummaryAction::GetExpressionClassName(UMaterialExpression* Expr) const
{
	if (!Expr)
	{
		return TEXT("Unknown");
	}

	// Reverse lookup from ExpressionClassMap
	InitExpressionClassMap();
	UClass* ExprClass = Expr->GetClass();
	for (const auto& Pair : ExpressionClassMap)
	{
		if (Pair.Value == ExprClass)
		{
			return Pair.Key;
		}
	}

	// Fallback: strip "MaterialExpression" prefix
	FString ClassName = ExprClass->GetName();
	ClassName.RemoveFromStart(TEXT("MaterialExpression"));
	return ClassName;
}

TSharedPtr<FJsonObject> FGetMaterialSummaryAction::GetExpressionProperties(UMaterialExpression* Expr) const
{
	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
	if (!Expr)
	{
		return Props;
	}

	// ScalarParameter
	if (UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expr))
	{
		Props->SetStringField(TEXT("ParameterName"), Scalar->ParameterName.ToString());
		Props->SetNumberField(TEXT("DefaultValue"), Scalar->DefaultValue);
	}
	// VectorParameter
	else if (UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(Expr))
	{
		Props->SetStringField(TEXT("ParameterName"), Vector->ParameterName.ToString());
		TArray<TSharedPtr<FJsonValue>> ColorArr;
		ColorArr.Add(MakeShared<FJsonValueNumber>(Vector->DefaultValue.R));
		ColorArr.Add(MakeShared<FJsonValueNumber>(Vector->DefaultValue.G));
		ColorArr.Add(MakeShared<FJsonValueNumber>(Vector->DefaultValue.B));
		ColorArr.Add(MakeShared<FJsonValueNumber>(Vector->DefaultValue.A));
		Props->SetArrayField(TEXT("DefaultValue"), ColorArr);
	}
	// Constant
	else if (UMaterialExpressionConstant* Const = Cast<UMaterialExpressionConstant>(Expr))
	{
		Props->SetNumberField(TEXT("R"), Const->R);
	}
	// Constant3Vector
	else if (UMaterialExpressionConstant3Vector* Const3 = Cast<UMaterialExpressionConstant3Vector>(Expr))
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShared<FJsonValueNumber>(Const3->Constant.R));
		Arr.Add(MakeShared<FJsonValueNumber>(Const3->Constant.G));
		Arr.Add(MakeShared<FJsonValueNumber>(Const3->Constant.B));
		Props->SetArrayField(TEXT("Constant"), Arr);
	}
	// Custom HLSL
	else if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expr))
	{
		Props->SetStringField(TEXT("Code"), Custom->Code);
		Props->SetStringField(TEXT("Description"), Custom->Description);
		// Output Inputs array for diagnosis
		TArray<TSharedPtr<FJsonValue>> InputsArr;
		for (int32 i = 0; i < Custom->Inputs.Num(); ++i)
		{
			TSharedPtr<FJsonObject> InputObj = MakeShared<FJsonObject>();
			InputObj->SetNumberField(TEXT("index"), i);
			InputObj->SetStringField(TEXT("name"), Custom->Inputs[i].InputName.ToString());
			InputObj->SetBoolField(TEXT("connected"), Custom->Inputs[i].Input.Expression != nullptr);
			if (Custom->Inputs[i].Input.Expression)
			{
				InputObj->SetStringField(TEXT("source_class"), Custom->Inputs[i].Input.Expression->GetClass()->GetName());
			}
			InputsArr.Add(MakeShared<FJsonValueObject>(InputObj));
		}
		Props->SetArrayField(TEXT("Inputs"), InputsArr);
		// Output type
		Props->SetNumberField(TEXT("OutputType"), (int32)Custom->OutputType);
	}
	// Noise
	else if (UMaterialExpressionNoise* Noise = Cast<UMaterialExpressionNoise>(Expr))
	{
		Props->SetNumberField(TEXT("Scale"), Noise->Scale);
		Props->SetNumberField(TEXT("Levels"), Noise->Levels);
	}
	// SceneTexture
	else if (UMaterialExpressionSceneTexture* SceneTex = Cast<UMaterialExpressionSceneTexture>(Expr))
	{
		Props->SetNumberField(TEXT("SceneTextureId"), static_cast<int32>(SceneTex->SceneTextureId));
	}
	// TextureCoordinate
	else if (UMaterialExpressionTextureCoordinate* TexCoord = Cast<UMaterialExpressionTextureCoordinate>(Expr))
	{
		Props->SetNumberField(TEXT("CoordinateIndex"), TexCoord->CoordinateIndex);
		Props->SetNumberField(TEXT("UTiling"), TexCoord->UTiling);
		Props->SetNumberField(TEXT("VTiling"), TexCoord->VTiling);
	}

	return Props;
}

TArray<TSharedPtr<FJsonValue>> FGetMaterialSummaryAction::BuildConnectionsArray(
	UMaterial* Material, const TMap<UMaterialExpression*, FString>& ExprToName) const
{
	TArray<TSharedPtr<FJsonValue>> Connections;

	// Scan all expression inputs
	for (const auto& Pair : ExprToName)
	{
		UMaterialExpression* TargetExpr = Pair.Key;
		const FString& TargetName = Pair.Value;

		for (int32 InputIndex = 0; ; ++InputIndex)
		{
			FExpressionInput* Input = TargetExpr->GetInput(InputIndex);
			if (!Input)
			{
				break;
			}
			if (Input->Expression)
			{
				const FString* SourceName = ExprToName.Find(Input->Expression);
				if (SourceName)
				{
					TSharedPtr<FJsonObject> Conn = MakeShared<FJsonObject>();
					Conn->SetStringField(TEXT("source"), *SourceName);
					Conn->SetNumberField(TEXT("source_output"), Input->OutputIndex);
					Conn->SetStringField(TEXT("target"), TargetName);
					Conn->SetStringField(TEXT("target_input"), TargetExpr->GetInputName(InputIndex).ToString());
					Connections.Add(MakeShared<FJsonValueObject>(Conn));
				}
			}
		}
	}

	// Scan material output connections
	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (EditorData)
	{
		auto CheckOutput = [&](const FExpressionInput& Input, const FString& PropName)
		{
			if (Input.Expression)
			{
				const FString* SourceName = ExprToName.Find(Input.Expression);
				if (SourceName)
				{
					TSharedPtr<FJsonObject> Conn = MakeShared<FJsonObject>();
					Conn->SetStringField(TEXT("source"), *SourceName);
					Conn->SetNumberField(TEXT("source_output"), Input.OutputIndex);
					Conn->SetStringField(TEXT("target"), TEXT("$output"));
					Conn->SetStringField(TEXT("target_input"), PropName);
					Connections.Add(MakeShared<FJsonValueObject>(Conn));
				}
			}
		};

		CheckOutput(EditorData->BaseColor, TEXT("BaseColor"));
		CheckOutput(EditorData->EmissiveColor, TEXT("EmissiveColor"));
		CheckOutput(EditorData->Metallic, TEXT("Metallic"));
		CheckOutput(EditorData->Roughness, TEXT("Roughness"));
		CheckOutput(EditorData->Specular, TEXT("Specular"));
		CheckOutput(EditorData->Normal, TEXT("Normal"));
		CheckOutput(EditorData->Opacity, TEXT("Opacity"));
		CheckOutput(EditorData->OpacityMask, TEXT("OpacityMask"));
		CheckOutput(EditorData->AmbientOcclusion, TEXT("AmbientOcclusion"));
		CheckOutput(EditorData->WorldPositionOffset, TEXT("WorldPositionOffset"));
		CheckOutput(EditorData->Refraction, TEXT("Refraction"));
	}

	return Connections;
}

TSharedPtr<FJsonObject> FGetMaterialSummaryAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString MaterialName;
	GetRequiredString(Params, TEXT("material_name"), MaterialName, Error);

	UMaterial* Material = FindMaterial(MaterialName, Error);
	if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	// Build expression-to-name map (reverse lookup from Context + auto-naming)
	TMap<UMaterialExpression*, FString> ExprToName;

	// First, populate from Context's MaterialNodeMap
	for (const auto& CtxPair : Context.MaterialNodeMap)
	{
		if (CtxPair.Value.IsValid())
		{
			ExprToName.Add(CtxPair.Value.Get(), CtxPair.Key);
		}
	}

	// Then, assign names to unregistered expressions
	int32 UnnamedIndex = 0;
	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressionCollection().Expressions;
	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr || Expr->IsA<UMaterialExpressionComment>())
		{
			continue;
		}
		if (!ExprToName.Contains(Expr))
		{
			ExprToName.Add(Expr, FString::Printf(TEXT("$expr_%d"), UnnamedIndex++));
		}
	}

	// Build expressions array
	TArray<TSharedPtr<FJsonValue>> ExpressionsArray;
	for (const auto& Pair : ExprToName)
	{
		UMaterialExpression* Expr = Pair.Key;
		TSharedPtr<FJsonObject> ExprObj = MakeShared<FJsonObject>();
		ExprObj->SetStringField(TEXT("node_name"), Pair.Value);
		ExprObj->SetStringField(TEXT("class"), GetExpressionClassName(Expr));
		ExprObj->SetNumberField(TEXT("pos_x"), Expr->MaterialExpressionEditorX);
		ExprObj->SetNumberField(TEXT("pos_y"), Expr->MaterialExpressionEditorY);
		ExprObj->SetObjectField(TEXT("properties"), GetExpressionProperties(Expr));
		ExpressionsArray.Add(MakeShared<FJsonValueObject>(ExprObj));
	}

	// Build connections array
	TArray<TSharedPtr<FJsonValue>> Connections = BuildConnectionsArray(Material, ExprToName);

	// Build comments array
	TArray<TSharedPtr<FJsonValue>> CommentsArray;
	const TArray<TObjectPtr<UMaterialExpressionComment>>& Comments = Material->GetExpressionCollection().EditorComments;
	for (UMaterialExpressionComment* Comment : Comments)
	{
		if (!Comment)
		{
			continue;
		}
		TSharedPtr<FJsonObject> CommentObj = MakeShared<FJsonObject>();
		CommentObj->SetStringField(TEXT("text"), Comment->Text);
		CommentObj->SetNumberField(TEXT("pos_x"), Comment->MaterialExpressionEditorX);
		CommentObj->SetNumberField(TEXT("pos_y"), Comment->MaterialExpressionEditorY);
		CommentObj->SetNumberField(TEXT("size_x"), Comment->SizeX);
		CommentObj->SetNumberField(TEXT("size_y"), Comment->SizeY);
		CommentsArray.Add(MakeShared<FJsonValueObject>(CommentObj));
	}

	// Resolve domain/blend/shading strings
	auto DomainToString = [](EMaterialDomain D) -> FString
	{
		switch (D)
		{
		case MD_Surface: return TEXT("Surface");
		case MD_PostProcess: return TEXT("PostProcess");
		case MD_DeferredDecal: return TEXT("DeferredDecal");
		case MD_LightFunction: return TEXT("LightFunction");
		case MD_UI: return TEXT("UI");
		case MD_Volume: return TEXT("Volume");
		default: return TEXT("Unknown");
		}
	};
	auto BlendModeToString = [](EBlendMode B) -> FString
	{
		switch (B)
		{
		case BLEND_Opaque: return TEXT("Opaque");
		case BLEND_Masked: return TEXT("Masked");
		case BLEND_Translucent: return TEXT("Translucent");
		case BLEND_Additive: return TEXT("Additive");
		case BLEND_Modulate: return TEXT("Modulate");
		default: return TEXT("Unknown");
		}
	};
	auto ShadingModelToString = [](EMaterialShadingModel SM) -> FString
	{
		switch (SM)
		{
		case MSM_Unlit: return TEXT("Unlit");
		case MSM_DefaultLit: return TEXT("DefaultLit");
		case MSM_Subsurface: return TEXT("Subsurface");
		case MSM_ClearCoat: return TEXT("ClearCoat");
		case MSM_Hair: return TEXT("Hair");
		case MSM_Cloth: return TEXT("Cloth");
		case MSM_Eye: return TEXT("Eye");
		default: return TEXT("DefaultLit");
		}
	};

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), MaterialName);
	Result->SetStringField(TEXT("path"), Material->GetPathName());
	Result->SetStringField(TEXT("domain"), DomainToString(Material->MaterialDomain));
	Result->SetStringField(TEXT("blend_mode"), BlendModeToString(Material->BlendMode));
	Result->SetStringField(TEXT("shading_model"), ShadingModelToString(Material->GetShadingModels().GetFirstShadingModel()));
	Result->SetBoolField(TEXT("two_sided"), Material->IsTwoSided());
	Result->SetNumberField(TEXT("expression_count"), ExprToName.Num());
	Result->SetArrayField(TEXT("expressions"), ExpressionsArray);
	Result->SetArrayField(TEXT("connections"), Connections);
	Result->SetArrayField(TEXT("comments"), CommentsArray);

	return CreateSuccessResponse(Result);
}


// =========================================================================
// FRemoveMaterialExpressionAction (P4.6)
// =========================================================================

bool FRemoveMaterialExpressionAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// At least one of node_name or node_names must be provided
	FString NodeName = GetOptionalString(Params, TEXT("node_name"));
	const TArray<TSharedPtr<FJsonValue>>* NodeNames = GetOptionalArray(Params, TEXT("node_names"));
	if (NodeName.IsEmpty() && (!NodeNames || NodeNames->Num() == 0))
	{
		OutError = TEXT("Either 'node_name' or 'node_names' is required");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FRemoveMaterialExpressionAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;

	UMaterial* Material = GetMaterialByNameOrCurrent(Params, Context, Error);
	if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	// Collect node names to remove
	TArray<FString> NamesToRemove;
	FString SingleName = GetOptionalString(Params, TEXT("node_name"));
	if (!SingleName.IsEmpty())
	{
		NamesToRemove.Add(SingleName);
	}
	const TArray<TSharedPtr<FJsonValue>>* NodeNames = GetOptionalArray(Params, TEXT("node_names"));
	if (NodeNames)
	{
		for (const auto& Val : *NodeNames)
		{
			NamesToRemove.AddUnique(Val->AsString());
		}
	}

	TArray<TSharedPtr<FJsonValue>> Removed;
	TArray<TSharedPtr<FJsonValue>> NotFound;

	for (const FString& NodeName : NamesToRemove)
	{
		UMaterialExpression* Expr = Context.GetMaterialNode(NodeName);
		if (!Expr)
		{
			NotFound.Add(MakeShared<FJsonValueString>(NodeName));
			continue;
		}

		// Disconnect all inputs on this expression
		for (int32 InputIdx = 0; ; ++InputIdx)
		{
			FExpressionInput* Input = Expr->GetInput(InputIdx);
			if (!Input)
			{
				break;
			}
			Input->Expression = nullptr;
			Input->OutputIndex = 0;
		}

		// Disconnect all other expressions that reference this one
		for (UMaterialExpression* OtherExpr : Material->GetExpressionCollection().Expressions)
		{
			if (!OtherExpr || OtherExpr == Expr)
			{
				continue;
			}
			for (int32 InputIdx = 0; ; ++InputIdx)
			{
				FExpressionInput* OtherInput = OtherExpr->GetInput(InputIdx);
				if (!OtherInput)
				{
					break;
				}
				if (OtherInput->Expression == Expr)
				{
					OtherInput->Expression = nullptr;
					OtherInput->OutputIndex = 0;
				}
			}
		}

		// Disconnect from material outputs
		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData)
		{
			auto DisconnectOutput = [Expr](FExpressionInput& Input)
			{
				if (Input.Expression == Expr)
				{
					Input.Expression = nullptr;
					Input.OutputIndex = 0;
				}
			};
			DisconnectOutput(EditorData->BaseColor);
			DisconnectOutput(EditorData->EmissiveColor);
			DisconnectOutput(EditorData->Metallic);
			DisconnectOutput(EditorData->Roughness);
			DisconnectOutput(EditorData->Specular);
			DisconnectOutput(EditorData->Normal);
			DisconnectOutput(EditorData->Opacity);
			DisconnectOutput(EditorData->OpacityMask);
			DisconnectOutput(EditorData->AmbientOcclusion);
			DisconnectOutput(EditorData->WorldPositionOffset);
			DisconnectOutput(EditorData->Refraction);
		}

		// Remove from expression collection
		Material->GetExpressionCollection().RemoveExpression(Expr);

		// Remove from context
		Context.MaterialNodeMap.Remove(NodeName);

		Removed.Add(MakeShared<FJsonValueString>(NodeName));
	}

	// Mark modified
	MarkMaterialModified(Material, Context);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("removed"), Removed);
	Result->SetArrayField(TEXT("not_found"), NotFound);

	return CreateSuccessResponse(Result);
}


