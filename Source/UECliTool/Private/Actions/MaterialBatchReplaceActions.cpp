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
// FReplaceMaterialNodeAction
// =========================================================================

bool FReplaceMaterialNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MaterialName, NodeName, NewClass;
	if (!GetRequiredString(Params, TEXT("material_name"), MaterialName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("node_name"), NodeName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("new_expression_class"), NewClass, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FReplaceMaterialNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString MaterialName, NodeName, NewExpressionClass;
	GetRequiredString(Params, TEXT("material_name"), MaterialName, Error);
	GetRequiredString(Params, TEXT("node_name"), NodeName, Error);
	GetRequiredString(Params, TEXT("new_expression_class"), NewExpressionClass, Error);

	// 1. Find material
	UMaterial* Material = FindMaterial(MaterialName, Error);
	if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressionCollection().Expressions;

	// 2. Find target node by Desc, GetName(), or parameter name
	UMaterialExpression* TargetExpr = nullptr;
	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr || Expr->IsA<UMaterialExpressionComment>()) { continue; }
		if (Expr->Desc == NodeName || Expr->GetName() == NodeName)
		{
			TargetExpr = Expr;
			break;
		}
		if (UMaterialExpressionParameter* Param = Cast<UMaterialExpressionParameter>(Expr))
		{
			if (Param->ParameterName.ToString() == NodeName)
			{
				TargetExpr = Expr;
				break;
			}
		}
	}

	if (!TargetExpr)
	{
		// Build available_nodes list and embed in error response
		TArray<TSharedPtr<FJsonValue>> AvailableNodes;
		for (UMaterialExpression* Expr : Expressions)
		{
			if (!Expr || Expr->IsA<UMaterialExpressionComment>()) { continue; }
			FString Name = !Expr->Desc.IsEmpty() ? Expr->Desc : Expr->GetName();
			AvailableNodes.Add(MakeShared<FJsonValueString>(Name));
		}
		TSharedPtr<FJsonObject> ErrResp = CreateErrorResponse(
			FString::Printf(TEXT("Node '%s' not found in material '%s'"), *NodeName, *MaterialName),
			TEXT("node_not_found"));
		ErrResp->SetArrayField(TEXT("available_nodes"), AvailableNodes);
		return ErrResp;
	}

	// 3. Record output connections: scan all other expressions for inputs pointing to TargetExpr
	struct FOutputConn { UMaterialExpression* OtherExpr; int32 InputIndex; int32 OutputIndex; };
	TArray<FOutputConn> OutputConns;
	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr || Expr == TargetExpr || Expr->IsA<UMaterialExpressionComment>()) { continue; }
		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* Input = Expr->GetInput(i);
			if (!Input) { break; }
			if (Input->Expression == TargetExpr)
			{
				OutputConns.Add({ Expr, i, Input->OutputIndex });
			}
		}
	}

	// 4. Record input connections on target node
	struct FInputConn { int32 InputIndex; UMaterialExpression* SourceExpr; int32 SourceOutputIndex; };
	TArray<FInputConn> InputConns;
	for (int32 i = 0; ; ++i)
	{
		FExpressionInput* Input = TargetExpr->GetInput(i);
		if (!Input) { break; }
		if (Input->Expression != nullptr)
		{
			InputConns.Add({ i, Input->Expression, Input->OutputIndex });
		}
	}

	// 5. Create new node
	UClass* NewClass = ResolveExpressionClass(NewExpressionClass);
	if (!NewClass)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Unknown expression class '%s'"), *NewExpressionClass),
			TEXT("invalid_expression_class"));
	}

	UMaterialExpression* NewExpr = NewObject<UMaterialExpression>(Material, NewClass);
	if (!NewExpr)
	{
		return CreateErrorResponse(TEXT("Failed to create new expression node"), TEXT("creation_failed"));
	}

	// Same editor position as old node
	NewExpr->MaterialExpressionEditorX = TargetExpr->MaterialExpressionEditorX;
	NewExpr->MaterialExpressionEditorY = TargetExpr->MaterialExpressionEditorY;
	NewExpr->Desc = TargetExpr->Desc;

	// 6. Value migration
	if (UMaterialExpressionConstant* OldConst = Cast<UMaterialExpressionConstant>(TargetExpr))
	{
		if (UMaterialExpressionScalarParameter* NewScalar = Cast<UMaterialExpressionScalarParameter>(NewExpr))
		{
			NewScalar->DefaultValue = OldConst->R;
			NewScalar->ParameterName = FName(*NodeName);
		}
	}
	else if (UMaterialExpressionConstant3Vector* OldConst3 = Cast<UMaterialExpressionConstant3Vector>(TargetExpr))
	{
		if (UMaterialExpressionVectorParameter* NewVec = Cast<UMaterialExpressionVectorParameter>(NewExpr))
		{
			NewVec->DefaultValue = FLinearColor(OldConst3->Constant.R, OldConst3->Constant.G, OldConst3->Constant.B, 1.0f);
			NewVec->ParameterName = FName(*NodeName);
		}
	}

	// 7. Apply new_properties if provided
	if (Params->HasField(TEXT("new_properties")))
	{
		const TSharedPtr<FJsonObject> NewProps = Params->GetObjectField(TEXT("new_properties"));
		if (auto* ScalarP3 = Cast<UMaterialExpressionScalarParameter>(NewExpr))
		{
			FString Val;
			double Num;
			if (NewProps->TryGetStringField(TEXT("ParameterName"), Val)) ScalarP3->ParameterName = FName(*Val);
			if (NewProps->TryGetNumberField(TEXT("DefaultValue"), Num)) ScalarP3->DefaultValue = (float)Num;
		}
		else if (auto* VectorP3 = Cast<UMaterialExpressionVectorParameter>(NewExpr))
		{
			FString Val;
			if (NewProps->TryGetStringField(TEXT("ParameterName"), Val)) VectorP3->ParameterName = FName(*Val);
		}
	}

	// 8. Add new node to material
	Material->GetExpressionCollection().AddExpression(NewExpr);

	// 9. Rebuild connections
	TArray<TSharedPtr<FJsonValue>> MigratedConns;
	TArray<TSharedPtr<FJsonValue>> FailedConns;

	// Output connections: other exprs that pointed to old node now point to new node
	for (const FOutputConn& Conn : OutputConns)
	{
		FExpressionInput* Input = Conn.OtherExpr->GetInput(Conn.InputIndex);
		if (Input)
		{
			Input->Expression = NewExpr;
			Input->OutputIndex = Conn.OutputIndex;
			TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
			ConnObj->SetStringField(TEXT("target_node"), !Conn.OtherExpr->Desc.IsEmpty() ? Conn.OtherExpr->Desc : Conn.OtherExpr->GetName());
			ConnObj->SetNumberField(TEXT("target_input"), Conn.InputIndex);
			MigratedConns.Add(MakeShared<FJsonValueObject>(ConnObj));
		}
		else
		{
			TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
			ConnObj->SetStringField(TEXT("target_node"), !Conn.OtherExpr->Desc.IsEmpty() ? Conn.OtherExpr->Desc : Conn.OtherExpr->GetName());
			ConnObj->SetNumberField(TEXT("target_input"), Conn.InputIndex);
			ConnObj->SetStringField(TEXT("reason"), TEXT("Input index no longer valid on new node"));
			FailedConns.Add(MakeShared<FJsonValueObject>(ConnObj));
		}
	}

	// Input connections: old node's inputs -> new node's inputs (if compatible)
	for (const FInputConn& Conn : InputConns)
	{
		FExpressionInput* Input = NewExpr->GetInput(Conn.InputIndex);
		if (Input)
		{
			Input->Expression = Conn.SourceExpr;
			Input->OutputIndex = Conn.SourceOutputIndex;
			TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
			ConnObj->SetStringField(TEXT("source_node"), !Conn.SourceExpr->Desc.IsEmpty() ? Conn.SourceExpr->Desc : Conn.SourceExpr->GetName());
			ConnObj->SetNumberField(TEXT("input_index"), Conn.InputIndex);
			MigratedConns.Add(MakeShared<FJsonValueObject>(ConnObj));
		}
		else
		{
			TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
			ConnObj->SetStringField(TEXT("source_node"), !Conn.SourceExpr->Desc.IsEmpty() ? Conn.SourceExpr->Desc : Conn.SourceExpr->GetName());
			ConnObj->SetNumberField(TEXT("input_index"), Conn.InputIndex);
			ConnObj->SetStringField(TEXT("reason"), TEXT("Input index not available on new node type"));
			FailedConns.Add(MakeShared<FJsonValueObject>(ConnObj));
		}
	}

	// 10. Remove old node
	Material->GetExpressionCollection().RemoveExpression(TargetExpr);

	// 11. Compile (same logic as FCompileMaterialAction)
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->ForceRecompileForRendering();

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 7
	FMaterialResource* MatResource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
#else
	FMaterialResource* MatResource = Material->GetMaterialResource(GMaxRHIFeatureLevel);
#endif
	if (MatResource)
	{
		MatResource->FinishCompilation();
	}

	TArray<FString> CompileErrors;
	if (MatResource)
	{
		CompileErrors = MatResource->GetCompileErrors();
	}
	bool bCompileSuccess = (MatResource != nullptr) && (CompileErrors.Num() == 0);

	// 12. Mark modified
	MarkMaterialModified(Material, Context);

	// 13. Build response
	FString NewNodeName = !NewExpr->Desc.IsEmpty() ? NewExpr->Desc : NewExpr->GetName();

	TSharedPtr<FJsonObject> CompileResult = MakeShared<FJsonObject>();
	CompileResult->SetBoolField(TEXT("success"), bCompileSuccess);
	CompileResult->SetNumberField(TEXT("error_count"), CompileErrors.Num());

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("replaced_node"), NodeName);
	Result->SetStringField(TEXT("new_node"), NewNodeName);
	Result->SetStringField(TEXT("new_expression_class"), NewExpressionClass);
	Result->SetArrayField(TEXT("migrated_connections"), MigratedConns);
	Result->SetArrayField(TEXT("failed_connections"), FailedConns);
	Result->SetObjectField(TEXT("compile_result"), CompileResult);

	return CreateSuccessResponse(Result);
}
