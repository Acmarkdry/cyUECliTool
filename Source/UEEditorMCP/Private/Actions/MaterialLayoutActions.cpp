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
// FAutoLayoutMaterialAction (P4.4)
// =========================================================================

bool FAutoLayoutMaterialAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MaterialName;
	return GetRequiredString(Params, TEXT("material_name"), MaterialName, OutError);
}

void FAutoLayoutMaterialAction::BuildDependencyGraph(UMaterial* Material,
	TMap<UMaterialExpression*, TArray<UMaterialExpression*>>& OutDeps,
	TMap<UMaterialExpression*, int32>& OutLayers) const
{
	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressionCollection().Expressions;
	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr || Expr->IsA<UMaterialExpressionComment>())
		{
			continue;
		}

		TArray<UMaterialExpression*> Dependencies;
		for (int32 InputIdx = 0; ; ++InputIdx)
		{
			FExpressionInput* Input = Expr->GetInput(InputIdx);
			if (!Input)
			{
				break;
			}
			if (Input->Expression && !Input->Expression->IsA<UMaterialExpressionComment>())
			{
				Dependencies.AddUnique(Input->Expression);
			}
		}
		OutDeps.Add(Expr, Dependencies);
		OutLayers.Add(Expr, 0);
	}
}

void FAutoLayoutMaterialAction::AssignLayers(UMaterial* Material,
	const TMap<UMaterialExpression*, TArray<UMaterialExpression*>>& Deps,
	TMap<UMaterialExpression*, int32>& OutLayers) const
{
	// Find root-connected expressions (connected to material outputs)
	TSet<UMaterialExpression*> RootSet;
	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (EditorData)
	{
		auto CollectRoot = [&RootSet](const FExpressionInput& Input)
		{
			if (Input.Expression)
			{
				RootSet.Add(Input.Expression);
			}
		};
		CollectRoot(EditorData->BaseColor);
		CollectRoot(EditorData->EmissiveColor);
		CollectRoot(EditorData->Metallic);
		CollectRoot(EditorData->Roughness);
		CollectRoot(EditorData->Specular);
		CollectRoot(EditorData->Normal);
		CollectRoot(EditorData->Opacity);
		CollectRoot(EditorData->OpacityMask);
		CollectRoot(EditorData->AmbientOcclusion);
		CollectRoot(EditorData->WorldPositionOffset);
		CollectRoot(EditorData->Refraction);
	}

	// If no root connections found, treat all leaf nodes (no downstream consumers) as roots
	if (RootSet.Num() == 0)
	{
		TSet<UMaterialExpression*> HasConsumer;
		for (const auto& Pair : Deps)
		{
			for (UMaterialExpression* Dep : Pair.Value)
			{
				HasConsumer.Add(Dep);
			}
		}
		for (const auto& Pair : Deps)
		{
			if (!HasConsumer.Contains(Pair.Key))
			{
				RootSet.Add(Pair.Key);
			}
		}
	}

	// BFS from root: layer 0 = rightmost (closest to output), higher = leftward
	TQueue<UMaterialExpression*> Queue;
	for (UMaterialExpression* Root : RootSet)
	{
		OutLayers.FindOrAdd(Root) = 0;
		Queue.Enqueue(Root);
	}

	while (!Queue.IsEmpty())
	{
		UMaterialExpression* Current = nullptr;
		Queue.Dequeue(Current);

		int32 CurrentLayer = OutLayers.FindRef(Current);
		const TArray<UMaterialExpression*>* DepList = Deps.Find(Current);
		if (DepList)
		{
			for (UMaterialExpression* Dep : *DepList)
			{
				int32& DepLayer = OutLayers.FindOrAdd(Dep);
				if (DepLayer < CurrentLayer + 1)
				{
					DepLayer = CurrentLayer + 1;
					Queue.Enqueue(Dep);
				}
			}
		}
	}
}

// Helper: Estimate material expression node size (code-based for Custom nodes)
static FVector2D EstimateMaterialExprNodeSize(UMaterialExpression* Expr)
{
	if (!Expr)
	{
		return FVector2D(280.0, 100.0);
	}

	// Pin counts
	int32 InputCount = 0;
	for (int32 i = 0; ; ++i)
	{
		if (!Expr->GetInput(i)) break;
		++InputCount;
	}
	TArray<FExpressionOutput>& Outputs = Expr->GetOutputs();
	int32 OutputCount = Outputs.Num();
	int32 MaxPins = FMath::Max(InputCount, OutputCount);
	// Note: bCollapsed in material nodes hides preview/description, NOT pins.
	// All input/output pins are always rendered.

	const double TitleH = 32.0;
	const double PinRowH = 26.0;
	const double BottomPad = 8.0;
	double PinsH = FMath::Max(MaxPins, 1) * PinRowH;
	double Height = TitleH + PinsH + BottomPad;
	double Width = 280.0;

	// Custom node: always compute code-based size as minimum
	// (MCP can't reliably read ShowCode from preview material, so be conservative)
	const UMaterialExpressionCustom* CustomExpr = Cast<UMaterialExpressionCustom>(Expr);
	if (CustomExpr && !CustomExpr->Code.IsEmpty())
	{
		// Count code lines and find max line length
		int32 LineCount = 1;
		int32 MaxLineLen = 0;
		int32 CurLineLen = 0;
		for (TCHAR Ch : CustomExpr->Code)
		{
			if (Ch == TEXT('\n'))
			{
				++LineCount;
				MaxLineLen = FMath::Max(MaxLineLen, CurLineLen);
				CurLineLen = 0;
			}
			else if (Ch != TEXT('\r'))
			{
				++CurLineLen;
			}
		}
		MaxLineLen = FMath::Max(MaxLineLen, CurLineLen);

		int32 ExtraOutputs = CustomExpr->AdditionalOutputs.Num();
		int32 TotalPins = FMath::Max(InputCount, OutputCount + ExtraOutputs);

		// Code area (SMultiLineEditableTextBox below pins)
		const double CodeLineH = 16.0;
		const double CodePad = 36.0;
		double CodeH = FMath::Max(LineCount, 3) * CodeLineH + CodePad;
		double CodePinsH = TotalPins * PinRowH;
		Height = TitleH + CodePinsH + CodeH;

		// Width: code chars + margins + preview area
		const double CharW = 7.2;
		const double Margins = 140.0; // left/right pad + scrollbar + preview thumbnail
		Width = FMath::Max(MaxLineLen * CharW + Margins, 420.0);

		// Safety factor for heuristic (no Slate bounds available)
		Width *= 1.1;
		Height *= 1.1;
	}
	else if (CustomExpr)
	{
		Width = 400.0;
	}
	else
	{
		// Non-Custom nodes: many node types show a preview thumbnail below pins
		// (TextureSample, VectorParameter, ScalarParameter, etc.)
		// Add preview height for nodes that typically render one.
		const double PreviewH = 90.0;
		if (MaxPins <= 3)
		{
			// Low-pin nodes often have a dominant preview area
			Height = FMath::Max(Height, 100.0 + PreviewH);
		}
		// Safety factor for widget chrome
		Width *= 1.12;
		Height *= 1.15;
	}

	return FVector2D(Width, Height);
}

TSharedPtr<FJsonObject> FAutoLayoutMaterialAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString MaterialName;
	GetRequiredString(Params, TEXT("material_name"), MaterialName, Error);

	UMaterial* Material = FindMaterial(MaterialName, Error);
	if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	// Build dependency graph and assign layers
	TMap<UMaterialExpression*, TArray<UMaterialExpression*>> Deps;
	TMap<UMaterialExpression*, int32> Layers;
	BuildDependencyGraph(Material, Deps, Layers);
	AssignLayers(Material, Deps, Layers);

	if (Layers.Num() == 0)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("nodes_moved"), 0);
		Result->SetNumberField(TEXT("layer_count"), 0);
		return CreateSuccessResponse(Result);
	}

	// ---- Phase A: Group by layer and compute per-node sizes ----
	int32 MaxLayer = 0;
	TMap<int32, TArray<UMaterialExpression*>> LayerGroups;
	TMap<UMaterialExpression*, FVector2D> NodeSizes;

	for (const auto& Pair : Layers)
	{
		LayerGroups.FindOrAdd(Pair.Value).Add(Pair.Key);
		MaxLayer = FMath::Max(MaxLayer, Pair.Value);
		NodeSizes.Add(Pair.Key, EstimateMaterialExprNodeSize(Pair.Key));
	}

	// Build downstream consumer map for barycenter ordering
	TMap<UMaterialExpression*, TArray<UMaterialExpression*>> Consumers;
	for (const auto& Pair : Deps)
	{
		Consumers.FindOrAdd(Pair.Key);
		for (UMaterialExpression* Dep : Pair.Value)
		{
			Consumers.FindOrAdd(Dep).AddUnique(Pair.Key);
		}
	}

	// Build pin-index map, root-connected set, and root pin order using shared utility
	TMap<UMaterialExpression*, TMap<UMaterialExpression*, int32>> PinIndexMap;
	MaterialLayoutUtils::BuildPinIndexMap(Deps, PinIndexMap);

	TSet<UMaterialExpression*> RootConnectedSet;
	TMap<UMaterialExpression*, int32> RootPinOrder;
	MaterialLayoutUtils::BuildRootMaps(Material->GetEditorOnlyData(), nullptr, RootConnectedSet, RootPinOrder);

	// ---- Phase B: Pin-aware layer sorting (shared utility) ----
	MaterialLayoutUtils::SortLayersByPinOrder(
		LayerGroups, MaxLayer, Deps, Consumers, PinIndexMap, RootConnectedSet, RootPinOrder);

	// ---- Phase C: Compute per-layer max width ----
	const double HGap = 80.0;
	const double VGap = 40.0;

	TMap<int32, double> LayerMaxWidth;
	for (const auto& LP : LayerGroups)
	{
		double MaxW = 0.0;
		for (UMaterialExpression* Expr : LP.Value)
		{
			MaxW = FMath::Max(MaxW, NodeSizes[Expr].X);
		}
		LayerMaxWidth.Add(LP.Key, MaxW);
	}

	// ---- Phase D: X coordinates (right to left from root) ----
	TMap<int32, double> LayerX;
	double CurX = 0.0;
	for (int32 L = 0; L <= MaxLayer; ++L)
	{
		double W = LayerMaxWidth.FindRef(L);
		LayerX.Add(L, CurX - W);
		CurX -= (W + HGap);
	}

	// ---- Phase E: Y coordinates (per-node height stacking, center-aligned) ----
	TMap<UMaterialExpression*, FVector2D> Positions;
	TMap<int32, double> LayerTotalH;

	for (int32 L = 0; L <= MaxLayer; ++L)
	{
		auto* Group = LayerGroups.Find(L);
		if (!Group) continue;

		double Y = 0.0;
		for (UMaterialExpression* Expr : *Group)
		{
			Positions.Add(Expr, FVector2D(LayerX[L], Y));
			Y += NodeSizes[Expr].Y + VGap;
		}
		LayerTotalH.Add(L, Y - VGap);
	}

	// Center-align layers vertically to Layer 0 height
	double L0H = LayerTotalH.Contains(0) ? LayerTotalH[0] : 0.0;
	for (int32 L = 0; L <= MaxLayer; ++L)
	{
		double LH = LayerTotalH.Contains(L) ? LayerTotalH[L] : 0.0;
		double OffY = (L0H - LH) * 0.5;
		if (FMath::Abs(OffY) < 1.0) continue;
		if (auto* Group = LayerGroups.Find(L))
		{
			for (UMaterialExpression* Expr : *Group)
			{
				Positions[Expr].Y += OffY;
			}
		}
	}

	// Single-node layers: align to downstream consumer center
	for (int32 L = 1; L <= MaxLayer; ++L)
	{
		auto* Group = LayerGroups.Find(L);
		if (!Group || Group->Num() != 1) continue;
		UMaterialExpression* Expr = (*Group)[0];

		double SumCY = 0.0; int32 Cnt = 0;
		if (auto* CL = Consumers.Find(Expr))
		{
			for (UMaterialExpression* C : *CL)
			{
				if (FVector2D* CP = Positions.Find(C))
				{
					SumCY += CP->Y + NodeSizes[C].Y * 0.5;
					++Cnt;
				}
			}
		}
		if (Cnt > 0)
		{
			Positions[Expr].Y = SumCY / Cnt - NodeSizes[Expr].Y * 0.5;
		}
	}

	// ---- Phase F: Minimum-gap enforcement (8 iterations) ----
	// For X-overlapping nodes, enforce minimum VGap between bottom of A and top of B
	TArray<UMaterialExpression*> AllExprs;
	Positions.GetKeys(AllExprs);

	for (int32 Iter = 0; Iter < 8; ++Iter)
	{
		bool bCollision = false;
		AllExprs.Sort([&Positions](UMaterialExpression& A, UMaterialExpression& B)
		{
			return Positions[&A].Y < Positions[&B].Y;
		});

		for (int32 i = 0; i < AllExprs.Num(); ++i)
		{
			UMaterialExpression* EA = AllExprs[i];
			FVector2D PA = Positions[EA];
			FVector2D SA = NodeSizes[EA];

			for (int32 j = i + 1; j < AllExprs.Num(); ++j)
			{
				UMaterialExpression* EB = AllExprs[j];
				FVector2D PB = Positions[EB];
				FVector2D SB = NodeSizes[EB];

				if (PB.Y > PA.Y + SA.Y + VGap) break; // sorted, gap already sufficient

				// Check horizontal overlap
				const double Tol = 4.0;
				bool bOverX = (PA.X < PB.X + SB.X - Tol) && (PB.X < PA.X + SA.X - Tol);
				if (!bOverX) continue;

				// Enforce minimum gap: B's top must be at least VGap below A's bottom
				double GapY = PB.Y - (PA.Y + SA.Y);
				if (GapY < VGap)
				{
					double Push = VGap - GapY;
					Positions[EB].Y += Push;
					bCollision = true;
				}
			}
		}
		if (!bCollision) break;
	}

	// ---- Phase G: Apply positions ----
	int32 NodesMoved = 0;
	for (auto& Pair : Positions)
	{
		UMaterialExpression* Expr = Pair.Key;
		int32 NewX = FMath::RoundToInt(Pair.Value.X);
		int32 NewY = FMath::RoundToInt(Pair.Value.Y);

		if (Expr->MaterialExpressionEditorX != NewX || Expr->MaterialExpressionEditorY != NewY)
		{
			Expr->Modify();
			Expr->MaterialExpressionEditorX = NewX;
			Expr->MaterialExpressionEditorY = NewY;
			++NodesMoved;
		}
	}

	// Sync positions to graph nodes and rebuild
	if (Material->MaterialGraph)
	{
		// First: directly update UMaterialGraphNode positions to match expression positions
		// (RebuildGraph alone may not reliably refresh SGraphEditor widgets)
		for (UEdGraphNode* Node : Material->MaterialGraph->Nodes)
		{
			UMaterialGraphNode* MatNode = Cast<UMaterialGraphNode>(Node);
			if (MatNode && MatNode->MaterialExpression)
			{
				MatNode->NodePosX = MatNode->MaterialExpression->MaterialExpressionEditorX;
				MatNode->NodePosY = MatNode->MaterialExpression->MaterialExpressionEditorY;
			}
		}
		// Then rebuild and notify to ensure SGraphEditor picks up changes
		Material->MaterialGraph->RebuildGraph();
		Material->MaterialGraph->NotifyGraphChanged();
	}

	// Mark modified
	MarkMaterialModified(Material, Context);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("nodes_moved"), NodesMoved);
	Result->SetNumberField(TEXT("layer_count"), MaxLayer + 1);

	// Debug: emit layer sort order and pin index map
	TArray<TSharedPtr<FJsonValue>> DebugLayers;
	for (int32 L = 0; L <= MaxLayer; ++L)
	{
		auto* Group = LayerGroups.Find(L);
		if (!Group) continue;
		TSharedPtr<FJsonObject> LayerObj = MakeShared<FJsonObject>();
		LayerObj->SetNumberField(TEXT("layer"), L);
		TArray<TSharedPtr<FJsonValue>> NodesArr;
		for (int32 i = 0; i < Group->Num(); ++i)
		{
			UMaterialExpression* Expr = (*Group)[i];
			TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
			NodeObj->SetNumberField(TEXT("sort_index"), i);
			NodeObj->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
			NodeObj->SetNumberField(TEXT("final_x"), Expr->MaterialExpressionEditorX);
			NodeObj->SetNumberField(TEXT("final_y"), Expr->MaterialExpressionEditorY);
			// If we have PinIndexMap data for this node as consumer
			if (auto* PM = PinIndexMap.Find(Expr))
			{
				TSharedPtr<FJsonObject> PinMap = MakeShared<FJsonObject>();
				for (auto& PinPair : *PM)
				{
					PinMap->SetNumberField(PinPair.Key->GetClass()->GetName() + TEXT("_") + PinPair.Key->GetName(), PinPair.Value);
				}
				NodeObj->SetObjectField(TEXT("pin_index_map"), PinMap);
			}
			NodesArr.Add(MakeShared<FJsonValueObject>(NodeObj));
		}
		LayerObj->SetArrayField(TEXT("nodes"), NodesArr);
		DebugLayers.Add(MakeShared<FJsonValueObject>(LayerObj));
	}
	Result->SetArrayField(TEXT("debug_layers"), DebugLayers);

	return CreateSuccessResponse(Result);
}


// =========================================================================
// FAutoCommentMaterialAction (P4.5)
// =========================================================================

bool FAutoCommentMaterialAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString CommentText;
	return GetRequiredString(Params, TEXT("comment_text"), CommentText, OutError);
}

TSharedPtr<FJsonObject> FAutoCommentMaterialAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;

	// Check use_selected early so we can auto-detect material from editor
	bool bEarlyUseSelected = GetOptionalBool(Params, TEXT("use_selected"), false);
	if (!bEarlyUseSelected)
	{
		const TArray<TSharedPtr<FJsonValue>>* EarlyNodeNames = GetOptionalArray(Params, TEXT("node_names"));
		if (EarlyNodeNames)
		{
			for (const auto& Val : *EarlyNodeNames)
			{
				if (Val->AsString() == TEXT("$selected")) { bEarlyUseSelected = true; break; }
			}
		}
	}

	UMaterial* Material = GetMaterialByNameOrCurrent(Params, Context, Error);

	// If no material found and use_selected is requested, auto-detect from open material editor
	if (!Material && bEarlyUseSelected)
	{
		UAssetEditorSubsystem* DetectEditorSS = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (DetectEditorSS)
		{
			TArray<UObject*> EditedAssets = DetectEditorSS->GetAllEditedAssets();
			for (UObject* Asset : EditedAssets)
			{
				UMaterial* Mat = Cast<UMaterial>(Asset);
				if (Mat && Mat->GetOutermost() != GetTransientPackage())
				{
					Material = Mat;
					Context.SetCurrentMaterial(Material);
					UE_LOG(LogMCP, Log, TEXT("auto_comment: auto-detected material '%s' from editor"), *Material->GetName());
					break;
				}
			}
		}
		if (!Material)
		{
			return CreateErrorResponse(TEXT("No material editor is currently open. Open a material or specify material_name."), TEXT("material_not_found"));
		}
	}
	else if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	FString CommentText;
	GetRequiredString(Params, TEXT("comment_text"), CommentText, Error);
	float Padding = GetOptionalNumber(Params, TEXT("padding"), 40.0);
	bool bOverwrite = GetOptionalBool(Params, TEXT("overwrite"), false);
	bool bClearAll = GetOptionalBool(Params, TEXT("clear_all"), false);

	// If clear_all, remove ALL existing comments first
	if (bClearAll)
	{
		TArray<TObjectPtr<UMaterialExpressionComment>>& Comments = Material->GetExpressionCollection().EditorComments;
		for (int32 i = Comments.Num() - 1; i >= 0; --i)
		{
			if (Comments[i])
			{
				if (Material->MaterialGraph)
				{
					UEdGraphNode* GraphNode = Comments[i]->GraphNode;
					if (GraphNode)
					{
						Material->MaterialGraph->RemoveNode(GraphNode);
					}
				}
			}
		}
		Comments.Empty();
	}
	// If overwrite, remove existing comments with the same text
	else if (bOverwrite)
	{
		TArray<TObjectPtr<UMaterialExpressionComment>>& Comments = Material->GetExpressionCollection().EditorComments;
		for (int32 i = Comments.Num() - 1; i >= 0; --i)
		{
			if (Comments[i] && Comments[i]->Text == CommentText)
			{
				if (Material->MaterialGraph)
				{
					// Remove graph node first
					UEdGraphNode* GraphNode = Comments[i]->GraphNode;
					if (GraphNode)
					{
						Material->MaterialGraph->RemoveNode(GraphNode);
					}
				}
				Comments.RemoveAt(i);
			}
		}
	}

	// Collect target expressions
	TArray<UMaterialExpression*> TargetExpressions;
	const TArray<TSharedPtr<FJsonValue>>* NodeNamesArray = GetOptionalArray(Params, TEXT("node_names"));
	TArray<TSharedPtr<FJsonValue>> MissingNodes;
	bool bUseSelected = false;

	// Check for "$selected" keyword in node_names
	if (NodeNamesArray && NodeNamesArray->Num() > 0)
	{
		for (const auto& Val : *NodeNamesArray)
		{
			if (Val->AsString() == TEXT("$selected"))
			{
				bUseSelected = true;
				break;
			}
		}
	}

	// Also support use_selected boolean param
	if (!bUseSelected)
	{
		bUseSelected = GetOptionalBool(Params, TEXT("use_selected"), false);
	}

	if (bUseSelected)
	{
		// ---- Robust material editor lookup ----
		// The material editor uses a preview material copy (UPreviewMaterial in TransientPackage).
		// The SGraphEditor and selected nodes reference the preview material's graph.
		//
		// IMPORTANT: Engine modules (UnrealEd, MaterialEditor) are compiled WITHOUT RTTI (/GR-),
		// while this plugin has bUseRTTI=true. Using dynamic_cast on engine types across DLL
		// boundaries is undefined behavior and will crash. We must use only:
		//   - FMaterialEditorUtilities::GetIMaterialEditorForObject 鈫?TSharedPtr<IMaterialEditor>
		//     (uses StaticCastSharedPtr internally, no RTTI needed)
		//   - UAssetEditorSubsystem for discovering edited assets (UObject Cast<> for UObjects)
		//   - IMaterialEditor inherits FAssetEditorToolkit, so GetObjectsCurrentlyBeingEdited()
		//     and other FAssetEditorToolkit methods are directly callable.

		TSharedPtr<IMaterialEditor> MatEditorPtr;
		UMaterial* PreviewMaterial = nullptr;

		// Method 1: Try original material's graph 鈫?FMaterialEditorUtilities
		if (Material->MaterialGraph)
		{
			MatEditorPtr = FMaterialEditorUtilities::GetIMaterialEditorForObject(Material->MaterialGraph);
			if (MatEditorPtr.IsValid())
			{
				UE_LOG(LogMCP, Log, TEXT("auto_comment: Found editor for '%s' via GetIMaterialEditorForObject (original graph)"), *Material->GetName());
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
									UE_LOG(LogMCP, Log, TEXT("auto_comment: Found editor for '%s' via preview material '%s'"),
										*Material->GetName(), *Mat->GetName());
									break;
								}
							}
						}
					}
					if (MatEditorPtr.IsValid()) break;
				}
			}
		}

		// Extract PreviewMaterial from editing objects if not already found
		if (MatEditorPtr.IsValid() && !PreviewMaterial)
		{
			const TArray<UObject*>* EditingObjs = MatEditorPtr->GetObjectsCurrentlyBeingEdited();
			if (EditingObjs && EditingObjs->Num() >= 2)
			{
				PreviewMaterial = Cast<UMaterial>((*EditingObjs)[1]);
			}
		}

		IMaterialEditor* MatEditorRaw = MatEditorPtr.IsValid() ? MatEditorPtr.Get() : nullptr;

		if (MatEditorRaw)
		{
			TSet<UObject*> SelectedNodes = MatEditorRaw->GetSelectedNodes();
			UE_LOG(LogMCP, Log, TEXT("auto_comment: IMaterialEditor::GetSelectedNodes count=%d for '%s'"),
				SelectedNodes.Num(), *Material->GetName());

			bool bHasRootOnly = true;
			for (UObject* SelObj : SelectedNodes)
			{
				UMaterialGraphNode* MatGraphNode = Cast<UMaterialGraphNode>(SelObj);
				if (!MatGraphNode)
				{
					if (Cast<UMaterialGraphNode_Root>(SelObj))
					{
						UE_LOG(LogMCP, Log, TEXT("auto_comment: Root node selected (skipped)"));
					}
					continue;
				}
				bHasRootOnly = false;
				if (MatGraphNode->MaterialExpression
					&& !MatGraphNode->MaterialExpression->IsA<UMaterialExpressionComment>())
				{
					// Map preview expression back to original material expression by index
					UMaterialExpression* PreviewExpr = MatGraphNode->MaterialExpression;
					if (PreviewMaterial && PreviewExpr)
					{
						const TArray<TObjectPtr<UMaterialExpression>>& PreviewExprs = PreviewMaterial->GetExpressionCollection().Expressions;
						int32 Idx = PreviewExprs.IndexOfByKey(PreviewExpr);
						if (Idx != INDEX_NONE)
						{
							const TArray<TObjectPtr<UMaterialExpression>>& OrigExprs = Material->GetExpressionCollection().Expressions;
							if (OrigExprs.IsValidIndex(Idx))
							{
								TargetExpressions.AddUnique(OrigExprs[Idx]);
								continue;
							}
						}
					}
					// Fallback: use the expression directly
					TargetExpressions.AddUnique(PreviewExpr);
				}
			}

			if (TargetExpressions.Num() == 0)
			{
				if (SelectedNodes.Num() > 0 && bHasRootOnly)
				{
					return CreateErrorResponse(TEXT("Only the material output (Root) node is selected. Select expression nodes to wrap."), TEXT("root_only"));
				}
				return CreateErrorResponse(TEXT("No material nodes are currently selected in the editor"), TEXT("no_selection"));
			}
		}
		else
		{
			// Fallback: Neither UAssetEditorSubsystem nor GetIMaterialEditorForObject found the editor.
			// Try SGraphEditor via preview material's graph from GetAllEditedAssets().
			UE_LOG(LogMCP, Log, TEXT("auto_comment: IMaterialEditor lookup failed for '%s', trying SGraphEditor fallback"), *Material->GetName());

			TSharedPtr<SGraphEditor> FallbackGraphEditor;

			// Scan ALL edited assets (including transient preview copies).
			// The SGraphEditor widget uses the preview material's MaterialGraph, not the original's.
			// We must NOT filter by name because the preview material has a different name (e.g. "PreviewMaterial_0").
			if (EditorSS)
			{
				TArray<UObject*> EditedAssets = EditorSS->GetAllEditedAssets();
				for (UObject* Asset : EditedAssets)
				{
					UMaterial* Mat = Cast<UMaterial>(Asset);
					if (Mat && Mat->MaterialGraph)
					{
						TSharedPtr<SGraphEditor> TestEditor = SGraphEditor::FindGraphEditorForGraph(Mat->MaterialGraph);
						if (TestEditor.IsValid())
						{
							// Verify this is the right editor: check if Mat is the preview copy of our Material,
							// or if Mat IS our Material, or if there's only one material editor open.
							bool bIsMatch = (Mat == Material);
							if (!bIsMatch && Mat->GetOutermost() == GetTransientPackage())
							{
								// This is a preview material. Check if the originating editor also has our Material
								// by looking for our material in the edited assets for the same editor.
								IAssetEditorInstance* TestEditorInst = EditorSS->FindEditorForAsset(Mat, false);
								if (TestEditorInst)
								{
									// Check if this same editor also edits our target material
									TArray<IAssetEditorInstance*> OurEditors = EditorSS->FindEditorsForAsset(Material);
									for (IAssetEditorInstance* OurEditor : OurEditors)
									{
										if (OurEditor == TestEditorInst)
										{
											bIsMatch = true;
											PreviewMaterial = Mat;
											break;
										}
									}
								}
							}

							if (bIsMatch)
							{
								FallbackGraphEditor = TestEditor;
								break;
							}
						}
					}
				}
			}

			// Last resort: try original material's graph directly
			if (!FallbackGraphEditor.IsValid() && Material->MaterialGraph)
			{
				FallbackGraphEditor = SGraphEditor::FindGraphEditorForGraph(Material->MaterialGraph);
			}

			if (FallbackGraphEditor.IsValid())
			{
				const FGraphPanelSelectionSet& SelectedNodes = FallbackGraphEditor->GetSelectedNodes();
				UE_LOG(LogMCP, Log, TEXT("auto_comment: SGraphEditor fallback found %d selected nodes for '%s'"),
					SelectedNodes.Num(), *Material->GetName());

				bool bHasRootOnly = true;
				for (UObject* SelObj : SelectedNodes)
				{
					UMaterialGraphNode* MatGraphNode = Cast<UMaterialGraphNode>(SelObj);
					if (!MatGraphNode)
					{
						if (Cast<UMaterialGraphNode_Root>(SelObj))
						{
							UE_LOG(LogMCP, Log, TEXT("auto_comment: Root node selected (skipped)"));
						}
						continue;
					}
					bHasRootOnly = false;
					if (MatGraphNode->MaterialExpression
						&& !MatGraphNode->MaterialExpression->IsA<UMaterialExpressionComment>())
					{
						UMaterialExpression* SelExpr = MatGraphNode->MaterialExpression;
						// Check if expression belongs to the original material directly
						const TArray<TObjectPtr<UMaterialExpression>>& OrigExprs = Material->GetExpressionCollection().Expressions;
						int32 DirectIdx = OrigExprs.IndexOfByKey(SelExpr);
						if (DirectIdx != INDEX_NONE)
						{
							TargetExpressions.AddUnique(SelExpr);
						}
						else
						{
							// Expression is from a preview/transient copy; map by index
							UMaterial* ExprOwner = Cast<UMaterial>(SelExpr->GetOuter());
							if (ExprOwner && ExprOwner != Material)
							{
								const TArray<TObjectPtr<UMaterialExpression>>& OwnerExprs = ExprOwner->GetExpressionCollection().Expressions;
								int32 OwnerIdx = OwnerExprs.IndexOfByKey(SelExpr);
								if (OwnerIdx != INDEX_NONE && OrigExprs.IsValidIndex(OwnerIdx))
								{
									TargetExpressions.AddUnique(OrigExprs[OwnerIdx]);
								}
								else
								{
									TargetExpressions.AddUnique(SelExpr);
								}
							}
							else
							{
								TargetExpressions.AddUnique(SelExpr);
							}
						}
					}
				}

				if (TargetExpressions.Num() == 0)
				{
					if (SelectedNodes.Num() > 0 && bHasRootOnly)
					{
						return CreateErrorResponse(TEXT("Only the material output (Root) node is selected. Select expression nodes to wrap."), TEXT("root_only"));
					}
					return CreateErrorResponse(TEXT("No material nodes are currently selected in the editor"), TEXT("no_selection"));
				}
			}
			else
			{
				UE_LOG(LogMCP, Warning, TEXT("auto_comment: Could not find IMaterialEditor or SGraphEditor for '%s'. Is the material editor open?"), *Material->GetName());
				return CreateErrorResponse(TEXT("Material editor not found. Open the material in the editor first."), TEXT("editor_not_found"));
			}
		}
	}
	else if (NodeNamesArray && NodeNamesArray->Num() > 0)
	{
		for (const auto& Val : *NodeNamesArray)
		{
			FString NodeName = Val->AsString();
			UMaterialExpression* Expr = Context.GetMaterialNode(NodeName);
			if (Expr)
			{
				TargetExpressions.AddUnique(Expr);
			}
			else
			{
				MissingNodes.Add(MakeShared<FJsonValueString>(NodeName));
			}
		}
	}
	else
	{
		// All non-comment expressions
		for (UMaterialExpression* Expr : Material->GetExpressionCollection().Expressions)
		{
			if (Expr && !Expr->IsA<UMaterialExpressionComment>())
			{
				TargetExpressions.Add(Expr);
			}
		}
	}

	if (TargetExpressions.Num() == 0)
	{
		return CreateErrorResponse(TEXT("No expressions found to wrap"), TEXT("no_expressions"));
	}

	// Calculate bounding box using actual node sizes when possible
	float MinX = TNumericLimits<float>::Max();
	float MinY = TNumericLimits<float>::Max();
	float MaxX = TNumericLimits<float>::Lowest();
	float MaxY = TNumericLimits<float>::Lowest();

	// Try to get material editor for accurate node bounds
	// The SGraphEditor displays the PreviewMaterial's MaterialGraph, not the original.
	// We must map original expressions 鈫?preview expressions by index to get Slate bounds.
	//
	// NOTE: No dynamic_cast 鈥?engine modules compiled without RTTI (/GR-).
	// Use FMaterialEditorUtilities::GetIMaterialEditorForObject (returns TSharedPtr via StaticCastSharedPtr).
	// IMaterialEditor inherits FAssetEditorToolkit, so GetObjectsCurrentlyBeingEdited() is accessible directly.
	TSharedPtr<IMaterialEditor> BoundsMatEditorPtr;
	IMaterialEditor* BoundsMatEditorRaw = nullptr;
	UMaterial* BoundsPreviewMaterial = nullptr;
	UMaterialGraph* BoundsGraph = nullptr;
	{
		// Method 1: Try original material's graph
		if (Material->MaterialGraph)
		{
			BoundsMatEditorPtr = FMaterialEditorUtilities::GetIMaterialEditorForObject(Material->MaterialGraph);
		}

		// Method 2: Find preview material's graph through UAssetEditorSubsystem
		if (!BoundsMatEditorPtr.IsValid())
		{
			UAssetEditorSubsystem* BoundsEditorSS = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
			if (BoundsEditorSS)
			{
				TArray<UObject*> EditedAssets = BoundsEditorSS->GetAllEditedAssets();
				for (UObject* Asset : EditedAssets)
				{
					UMaterial* Mat = Cast<UMaterial>(Asset);
					if (Mat && Mat != Material && Mat->MaterialGraph && Mat->GetOutermost() == GetTransientPackage())
					{
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
										BoundsMatEditorPtr = TestEditor;
										BoundsPreviewMaterial = Mat;
										break;
									}
								}
							}
						}
						if (BoundsMatEditorPtr.IsValid()) break;
					}
				}
			}
		}

		if (BoundsMatEditorPtr.IsValid())
		{
			BoundsMatEditorRaw = BoundsMatEditorPtr.Get();

			// Extract PreviewMaterial and its graph
			if (!BoundsPreviewMaterial)
			{
				const TArray<UObject*>* EditObjs = BoundsMatEditorRaw->GetObjectsCurrentlyBeingEdited();
				if (EditObjs && EditObjs->Num() >= 2)
				{
					UMaterial* BoundsPreview = Cast<UMaterial>((*EditObjs)[1]);
					if (BoundsPreview && BoundsPreview->MaterialGraph)
					{
						BoundsPreviewMaterial = BoundsPreview;
					}
				}
			}

			if (BoundsPreviewMaterial && BoundsPreviewMaterial->MaterialGraph)
			{
				BoundsGraph = BoundsPreviewMaterial->MaterialGraph;
			}
			else if (Material->MaterialGraph)
			{
				BoundsGraph = Material->MaterialGraph;
			}

			UE_LOG(LogMCP, Log, TEXT("auto_comment: Bounds editor found for '%s' (PreviewMaterial=%s, Graph=%s)"),
				*Material->GetName(),
				BoundsPreviewMaterial ? *BoundsPreviewMaterial->GetName() : TEXT("none"),
				BoundsGraph ? TEXT("yes") : TEXT("no"));
		}
	}

	for (UMaterialExpression* Expr : TargetExpressions)
	{
		float NodeX = static_cast<float>(Expr->MaterialExpressionEditorX);
		float NodeY = static_cast<float>(Expr->MaterialExpressionEditorY);
		float NodeW = 280.f;
		float NodeH = 80.f;

		// Step 1: Try Slate actual bounds via IMaterialEditor + PreviewMaterial graph
		bool bGotBounds = false;
		if (BoundsMatEditorRaw)
		{
			// Expr is from the original material; the editor displays the PreviewMaterial's graph.
			// We must map original expression 鈫?preview expression by index to find the correct
			// graph node that has an active Slate widget in the SGraphEditor.
			UEdGraphNode* BoundsNode = nullptr;
			if (BoundsGraph)
			{
				// Step 1a: Direct pointer match (works when BoundsGraph == original graph)
				for (UEdGraphNode* GN : BoundsGraph->Nodes)
				{
					UMaterialGraphNode* MGN = Cast<UMaterialGraphNode>(GN);
					if (MGN && MGN->MaterialExpression == Expr)
					{
						BoundsNode = GN;
						break;
					}
				}

				// Step 1b: Index-based mapping when BoundsGraph is from PreviewMaterial
				// (Direct match fails because preview expressions are different UObject instances)
				if (!BoundsNode && BoundsPreviewMaterial && BoundsPreviewMaterial != Material)
				{
					const TArray<TObjectPtr<UMaterialExpression>>& OrigExprs = Material->GetExpressionCollection().Expressions;
					int32 ExprIdx = OrigExprs.IndexOfByKey(Expr);
					if (ExprIdx != INDEX_NONE)
					{
						const TArray<TObjectPtr<UMaterialExpression>>& PreviewExprs = BoundsPreviewMaterial->GetExpressionCollection().Expressions;
						if (PreviewExprs.IsValidIndex(ExprIdx))
						{
							UMaterialExpression* PreviewExpr = PreviewExprs[ExprIdx];
							if (PreviewExpr && PreviewExpr->GraphNode)
							{
								BoundsNode = PreviewExpr->GraphNode;
							}
						}
					}
				}
			}
			// Fallback: try Expr->GraphNode directly (may not have Slate widget if preview graph is active)
			if (!BoundsNode)
			{
				BoundsNode = Expr->GraphNode;
			}

			if (BoundsNode)
			{
				FSlateRect Rect;
				BoundsMatEditorRaw->GetBoundsForNode(BoundsNode, Rect, 0.f);
				float W = static_cast<float>(Rect.GetSize().X);
				float H = static_cast<float>(Rect.GetSize().Y);
				if (W > 10.f && H > 10.f)
				{
					NodeW = W;
					NodeH = H;
					bGotBounds = true;
				}
			}
		}

		// Step 2: Heuristic fallback using shared size estimator
		if (!bGotBounds)
		{
			FVector2D EstSize = EstimateMaterialExprNodeSize(Expr);
			NodeW = static_cast<float>(EstSize.X);
			NodeH = static_cast<float>(EstSize.Y);
			UE_LOG(LogMCP, Log, TEXT("auto_comment: node '%s' bounds via heuristic: %.0fx%.0f"),
				*Expr->GetName(), NodeW, NodeH);
		}
		else
		{
			UE_LOG(LogMCP, Log, TEXT("auto_comment: node '%s' bounds via Slate: %.0fx%.0f"),
				*Expr->GetName(), NodeW, NodeH);
		}

		MinX = FMath::Min(MinX, NodeX);
		MinY = FMath::Min(MinY, NodeY);
		MaxX = FMath::Max(MaxX, NodeX + NodeW);
		MaxY = FMath::Max(MaxY, NodeY + NodeH);
	}

	// Apply padding
	float CommentX = MinX - Padding;
	float CommentY = MinY - Padding - 40.f; // Extra space for comment title
	float CommentW = (MaxX - MinX) + 2.f * Padding;
	float CommentH = (MaxY - MinY) + 2.f * Padding + 40.f;

	// Minimum width based on text length
	float MinWidth = static_cast<float>(CommentText.Len()) * 8.f + 40.f;
	CommentW = FMath::Max(CommentW, MinWidth);

	// Note: Collision avoidance with existing comments was intentionally removed.
	// Material editor comments are designed to overlap/nest (e.g. a large comment wrapping
	// multiple smaller commented groups). Displacing comments breaks the "wrap target nodes"
	// contract and pushes comments far from their intended position.

	// Create UMaterialExpressionComment
	UMaterialExpressionComment* Comment = NewObject<UMaterialExpressionComment>(Material);
	Comment->Text = CommentText;
	Comment->MaterialExpressionEditorX = static_cast<int32>(CommentX);
	Comment->MaterialExpressionEditorY = static_cast<int32>(CommentY);
	Comment->SizeX = static_cast<int32>(CommentW);
	Comment->SizeY = static_cast<int32>(CommentH);
	Comment->MaterialExpressionGuid = FGuid::NewGuid();

	// Set color if provided
	const TArray<TSharedPtr<FJsonValue>>* ColorArray = GetOptionalArray(Params, TEXT("color"));
	if (ColorArray && ColorArray->Num() >= 3)
	{
		Comment->CommentColor = FLinearColor(
			(*ColorArray)[0]->AsNumber(),
			(*ColorArray)[1]->AsNumber(),
			(*ColorArray)[2]->AsNumber(),
			ColorArray->Num() > 3 ? (*ColorArray)[3]->AsNumber() : 1.0f);
	}

	// Add to material's EditorComments (NOT Expressions 鈥?Comments have a separate collection)
	Material->GetExpressionCollection().AddComment(Comment);

	// Add graph node if material graph is available (ensures comment appears in the editor)
	if (Material->MaterialGraph)
	{
		Material->MaterialGraph->AddComment(Comment);
	}

	// Mark modified
	MarkMaterialModified(Material, Context);

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("comment_text"), CommentText);

	TArray<TSharedPtr<FJsonValue>> PosArr;
	PosArr.Add(MakeShared<FJsonValueNumber>(CommentX));
	PosArr.Add(MakeShared<FJsonValueNumber>(CommentY));
	Result->SetArrayField(TEXT("position"), PosArr);

	TArray<TSharedPtr<FJsonValue>> SizeArr;
	SizeArr.Add(MakeShared<FJsonValueNumber>(CommentW));
	SizeArr.Add(MakeShared<FJsonValueNumber>(CommentH));
	Result->SetArrayField(TEXT("size"), SizeArr);

	Result->SetNumberField(TEXT("nodes_wrapped"), TargetExpressions.Num());
	if (MissingNodes.Num() > 0)
	{
		Result->SetArrayField(TEXT("missing_nodes"), MissingNodes);
	}

	return CreateSuccessResponse(Result);
}


// =========================================================================
// FGetMaterialSelectedNodesAction (P5.5)
// =========================================================================

bool FGetMaterialSelectedNodesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// No required params 鈥?material is auto-detected if not specified
	return true;
}

TSharedPtr<FJsonObject> FGetMaterialSelectedNodesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;

	// Try to get material by name, or auto-detect from open editor
	UMaterial* Material = GetMaterialByNameOrCurrent(Params, Context, Error);

	if (!Material)
	{
		// Auto-detect from any open material editor
		UAssetEditorSubsystem* AssetEditorSS = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (AssetEditorSS)
		{
			TArray<UObject*> EditedAssets = AssetEditorSS->GetAllEditedAssets();
			for (UObject* Asset : EditedAssets)
			{
				UMaterial* Mat = Cast<UMaterial>(Asset);
				if (Mat && Mat->GetOutermost() != GetTransientPackage())
				{
					Material = Mat;
					Context.SetCurrentMaterial(Material);
					break;
				}
			}
		}
		if (!Material)
		{
			return CreateErrorResponse(TEXT("No material editor is currently open. Open a material or specify material_name."), TEXT("material_not_found"));
		}
	}

	// Find the material graph and its SGraphEditor
	UMaterialGraph* MatGraph = Material->MaterialGraph;
	if (!MatGraph)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Material '%s' has no graph (not open in editor?)"), *Material->GetName()), TEXT("no_graph"));
	}

	TSharedPtr<SGraphEditor> GraphEditor = SGraphEditor::FindGraphEditorForGraph(MatGraph);
	if (!GraphEditor.IsValid())
	{
		// Fallback: scan all edited assets for any graph editor
		UAssetEditorSubsystem* AssetEditorSS = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (AssetEditorSS)
		{
			TArray<UObject*> EditedAssets = AssetEditorSS->GetAllEditedAssets();
			for (UObject* Asset : EditedAssets)
			{
				UMaterial* Mat = Cast<UMaterial>(Asset);
				if (Mat && Mat->MaterialGraph)
				{
					TSharedPtr<SGraphEditor> TestEditor = SGraphEditor::FindGraphEditorForGraph(Mat->MaterialGraph);
					if (TestEditor.IsValid())
					{
						GraphEditor = TestEditor;
						MatGraph = Mat->MaterialGraph;
						// Update material reference if different
						if (Mat != Material && Mat->GetOutermost() != GetTransientPackage())
						{
							Material = Mat;
							Context.SetCurrentMaterial(Material);
						}
						break;
					}
				}
			}
		}
	}

	if (!GraphEditor.IsValid())
	{
		return CreateErrorResponse(TEXT("Material editor SGraphEditor not found. Make sure the material is open and visible."), TEXT("no_editor"));
	}

	// Get selected nodes
	const FGraphPanelSelectionSet& SelectedNodes = GraphEditor->GetSelectedNodes();

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	bool bHasRootSelected = false;

	for (UObject* SelObj : SelectedNodes)
	{
		if (Cast<UMaterialGraphNode_Root>(SelObj))
		{
			bHasRootSelected = true;
			continue;
		}

		UMaterialGraphNode* MatGraphNode = Cast<UMaterialGraphNode>(SelObj);
		if (!MatGraphNode || !MatGraphNode->MaterialExpression) continue;
		if (MatGraphNode->MaterialExpression->IsA<UMaterialExpressionComment>()) continue;

		UMaterialExpression* Expr = MatGraphNode->MaterialExpression;

		// Determine the expression's index and name in the original material
		const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressionCollection().Expressions;
		int32 ExprIndex = Expressions.IndexOfByKey(Expr);

		// If expression is from a different material (e.g. transient preview), map by index
		if (ExprIndex == INDEX_NONE)
		{
			// Try to match by index if this is a preview copy
			UMaterial* PreviewOwner = Cast<UMaterial>(Expr->GetOuter());
			if (PreviewOwner && PreviewOwner != Material)
			{
				const TArray<TObjectPtr<UMaterialExpression>>& PreviewExprs = PreviewOwner->GetExpressionCollection().Expressions;
				int32 PreviewIdx = PreviewExprs.IndexOfByKey(Expr);
				if (PreviewIdx != INDEX_NONE && Expressions.IsValidIndex(PreviewIdx))
				{
					Expr = Expressions[PreviewIdx];
					ExprIndex = PreviewIdx;
				}
			}
		}

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();

		// Build node name: check session map, Desc, parameter name, then fallback
		FString NodeName;
		for (const auto& Pair : Context.MaterialNodeMap)
		{
			if (Pair.Value.IsValid() && Pair.Value.Get() == Expr)
			{
				NodeName = Pair.Key;
				break;
			}
		}

		if (NodeName.IsEmpty() && !Expr->Desc.IsEmpty())
		{
			NodeName = Expr->Desc;
		}
		if (NodeName.IsEmpty())
		{
			if (auto* ScalarParam = Cast<UMaterialExpressionScalarParameter>(Expr))
			{
				NodeName = ScalarParam->ParameterName.ToString();
			}
			else if (auto* VectorParam = Cast<UMaterialExpressionVectorParameter>(Expr))
			{
				NodeName = VectorParam->ParameterName.ToString();
			}
		}
		if (NodeName.IsEmpty())
		{
			NodeName = Expr->GetName();
		}

		NodeObj->SetStringField(TEXT("node_name"), NodeName);
		NodeObj->SetStringField(TEXT("expression_class"), Expr->GetClass()->GetName());
		NodeObj->SetNumberField(TEXT("pos_x"), Expr->MaterialExpressionEditorX);
		NodeObj->SetNumberField(TEXT("pos_y"), Expr->MaterialExpressionEditorY);
		if (ExprIndex != INDEX_NONE)
		{
			NodeObj->SetNumberField(TEXT("index"), ExprIndex);
		}

		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("material_name"), Material->GetName());
	ResultData->SetNumberField(TEXT("selected_count"), NodesArray.Num());
	ResultData->SetArrayField(TEXT("nodes"), NodesArray);
	if (bHasRootSelected)
	{
		ResultData->SetBoolField(TEXT("root_selected"), true);
	}

	return CreateSuccessResponse(ResultData);
}


