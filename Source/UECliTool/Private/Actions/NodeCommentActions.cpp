// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/NodeActions.h"
#include "MCPCommonUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_InputAction.h"
#include "K2Node_Self.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_EnhancedInputAction.h"
#include "K2Node_GetSubsystem.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_ExecutionSequence.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/EngineSubsystem.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "GameFramework/GameUserSettings.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundClass.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_SwitchString.h"
#include "K2Node_SwitchInteger.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "EdGraphNode_Comment.h"
#include "K2Node_Knot.h"
#include "Actions/LayoutActions.h"
#include "GraphEditorActions.h"
#include "MCPBridge.h"
#include "GraphEditor.h"


// ============================================================================
// Comment Nodes
// ============================================================================

bool FAddBlueprintCommentAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString CommentText;
	if (!GetRequiredString(Params, TEXT("comment_text"), CommentText, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintCommentAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString CommentText = Params->GetStringField(TEXT("comment_text"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Get graph — empty string triggers FindGraph's default (EventGraph) fallback
	FString GraphName = GetOptionalString(Params, TEXT("graph_name"));
	FString GraphError;
	UEdGraph* Graph = FindGraph(Blueprint, GraphName, GraphError);
	if (!Graph)
	{
		return CreateErrorResponse(GraphError);
	}

	// Create the comment node
	UEdGraphNode_Comment* CommentNode = NewObject<UEdGraphNode_Comment>(Graph);
	CommentNode->CreateNewGuid();
	CommentNode->NodeComment = CommentText;

	// Position
	FVector2D NodePos = GetNodePosition(Params);
	CommentNode->NodePosX = NodePos.X;
	CommentNode->NodePosY = NodePos.Y;

	// Size (optional)
	const TArray<TSharedPtr<FJsonValue>>* SizeArray = nullptr;
	if (Params->TryGetArrayField(TEXT("size"), SizeArray) && SizeArray->Num() >= 2)
	{
		CommentNode->NodeWidth = static_cast<int32>((*SizeArray)[0]->AsNumber());
		CommentNode->NodeHeight = static_cast<int32>((*SizeArray)[1]->AsNumber());
	}
	else
	{
		CommentNode->NodeWidth = 400;
		CommentNode->NodeHeight = 200;
	}

	// Color (optional RGBA)
	const TArray<TSharedPtr<FJsonValue>>* ColorArray = nullptr;
	if (Params->TryGetArrayField(TEXT("color"), ColorArray) && ColorArray->Num() >= 3)
	{
		float R = static_cast<float>((*ColorArray)[0]->AsNumber());
		float G = static_cast<float>((*ColorArray)[1]->AsNumber());
		float B = static_cast<float>((*ColorArray)[2]->AsNumber());
		float A = ColorArray->Num() >= 4 ? static_cast<float>((*ColorArray)[3]->AsNumber()) : 1.0f;
		CommentNode->CommentColor = FLinearColor(R, G, B, A);
	}

	Graph->AddNode(CommentNode, true, false);
	CommentNode->SetFlags(RF_Transactional);

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(CommentNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), CommentNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("comment_text"), CommentText);
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Auto Comment — auto-sized comment wrapping specified nodes
// ============================================================================

bool FAutoCommentAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString CommentText;
	if (!GetRequiredString(Params, TEXT("comment_text"), CommentText, OutError)) return false;

	// node_ids is optional — when omitted, wraps all non-comment nodes in the graph

	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAutoCommentAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString CommentText = Params->GetStringField(TEXT("comment_text"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Get graph — empty string triggers FindGraph's default (EventGraph) fallback
	FString GraphName = GetOptionalString(Params, TEXT("graph_name"));
	FString GraphError;
	UEdGraph* Graph = FindGraph(Blueprint, GraphName, GraphError);
	if (!Graph)
	{
		return CreateErrorResponse(GraphError);
	}

	// Parse node_ids (optional — when omitted, wraps all non-comment nodes)
	const TArray<TSharedPtr<FJsonValue>>* NodeIdsArray = nullptr;
	bool bHasNodeIds = Params->TryGetArrayField(TEXT("node_ids"), NodeIdsArray) && NodeIdsArray && NodeIdsArray->Num() > 0;

	// Padding and title height
	float Padding = 40.f;
	if (Params->HasField(TEXT("padding")))
	{
		Padding = static_cast<float>(Params->GetNumberField(TEXT("padding")));
	}

	float TitleHeight = 36.f;
	if (Params->HasField(TEXT("title_height")))
	{
		TitleHeight = static_cast<float>(Params->GetNumberField(TEXT("title_height")));
	}

	// Layout settings for GetNodeSize fallback
	FBlueprintLayoutSettings LayoutSettings;

	// Build list of nodes to wrap
	TArray<UEdGraphNode*> NodesToWrap;
	TArray<FString> MissingNodes;

	if (bHasNodeIds)
	{
		// Explicit node_ids mode — find each specified node
		for (const TSharedPtr<FJsonValue>& NodeIdValue : *NodeIdsArray)
		{
			FString NodeIdStr = NodeIdValue->AsString();
			FGuid NodeGuid;
			if (!FGuid::Parse(NodeIdStr, NodeGuid))
			{
				MissingNodes.Add(NodeIdStr + TEXT(" (invalid GUID)"));
				continue;
			}

			UEdGraphNode* FoundNode = nullptr;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node && Node->NodeGuid == NodeGuid)
				{
					FoundNode = Node;
					break;
				}
			}

			if (!FoundNode)
			{
				MissingNodes.Add(NodeIdStr);
				continue;
			}

			// Skip comment nodes in bounding box calculation
			if (!Cast<UEdGraphNode_Comment>(FoundNode))
			{
				NodesToWrap.Add(FoundNode);
			}
		}
	}
	else
	{
		// wrap_all mode — include every non-comment node in the graph
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && !Cast<UEdGraphNode_Comment>(Node))
			{
				NodesToWrap.Add(Node);
			}
		}
	}

	if (NodesToWrap.Num() == 0)
	{
		FString ErrorMsg = TEXT("No valid nodes found to wrap.");
		if (MissingNodes.Num() > 0)
		{
			ErrorMsg += TEXT(" Missing: ") + FString::Join(MissingNodes, TEXT(", "));
		}
		return CreateErrorResponse(ErrorMsg);
	}

	// Calculate bounding box
	float MinX = TNumericLimits<float>::Max();
	float MinY = TNumericLimits<float>::Max();
	float MaxX = TNumericLimits<float>::Lowest();
	float MaxY = TNumericLimits<float>::Lowest();

	for (UEdGraphNode* WrapNode : NodesToWrap)
	{
		float NodeX = static_cast<float>(WrapNode->NodePosX);
		float NodeY = static_cast<float>(WrapNode->NodePosY);
		FVector2D NodeSize = FBlueprintAutoLayout::GetNodeSize(WrapNode, LayoutSettings);

		MinX = FMath::Min(MinX, NodeX);
		MinY = FMath::Min(MinY, NodeY);
		MaxX = FMath::Max(MaxX, NodeX + NodeSize.X);
		MaxY = FMath::Max(MaxY, NodeY + NodeSize.Y);
	}

	// Calculate comment position and size
	float CommentX = MinX - Padding;
	float CommentY = MinY - Padding - TitleHeight;
	float CommentBottom = MaxY + Padding;
	float CommentRight = MaxX + Padding;

	// ================================================================
	// Collision avoidance: push new comment's top edge above any
	// overlapping existing comment so title bars never overlap.
	// We keep the bottom edge fixed (must still cover target nodes)
	// and only grow upward.  Iterate up to 5 times for cascading.
	// ================================================================
	const float CommentGap = 60.f;  // min vertical gap between titles
	float OriginalCommentY = CommentY;  // track for collision_adjusted flag

	for (int32 Iter = 0; Iter < 5; ++Iter)
	{
		bool bAnyOverlap = false;
		for (UEdGraphNode* ExistingNode : Graph->Nodes)
		{
			UEdGraphNode_Comment* ExistingComment = Cast<UEdGraphNode_Comment>(ExistingNode);
			if (!ExistingComment) continue;

			float ExLeft   = static_cast<float>(ExistingComment->NodePosX);
			float ExTop    = static_cast<float>(ExistingComment->NodePosY);
			float ExRight  = ExLeft + static_cast<float>(ExistingComment->NodeWidth);
			float ExBottom = ExTop  + static_cast<float>(ExistingComment->NodeHeight);

			// Check AABB overlap (both axes must overlap)
			bool bOverlapX = (CommentX < ExRight) && (CommentRight > ExLeft);
			bool bOverlapY = (CommentY < ExBottom) && (CommentBottom > ExTop);

			if (bOverlapX && bOverlapY)
			{
				// Push our top edge above the existing comment's top - gap
				float NeededTop = ExTop - CommentGap;
				if (NeededTop < CommentY)
				{
					CommentY = NeededTop;
					bAnyOverlap = true;
				}
			}
		}
		if (!bAnyOverlap) break;
	}

	// ================================================================
	// Minimum width from comment title text
	// Ensure the comment box is wide enough to display its title.
	// Comment title uses ~10px/Latin char, CJK ~1.8x wider.
	// ================================================================
	{
		const float CommentCharW = 10.f;
		float TitleTextWidth = 0.f;
		for (TCHAR Ch : CommentText)
		{
			if ((Ch >= 0x4E00 && Ch <= 0x9FFF)
				|| (Ch >= 0x3400 && Ch <= 0x4DBF)
				|| (Ch >= 0xFF00 && Ch <= 0xFFEF)
				|| (Ch >= 0xAC00 && Ch <= 0xD7AF)
				|| (Ch >= 0x3040 && Ch <= 0x30FF))
			{
				TitleTextWidth += CommentCharW * 1.8f;
			}
			else
			{
				TitleTextWidth += CommentCharW;
			}
		}
		// Add left/right margin for the title bar (bubble icon + padding)
		const float TitleMargin = 40.f;
		float MinCommentWidth = TitleTextWidth + TitleMargin;
		float CurrentWidth = CommentRight - CommentX;
		if (MinCommentWidth > CurrentWidth)
		{
			// Expand symmetrically from center
			float Expand = (MinCommentWidth - CurrentWidth) * 0.5f;
			CommentX -= Expand;
			CommentRight += Expand;
		}
	}

	int32 CommentWidth  = FMath::RoundToInt(CommentRight - CommentX);
	int32 CommentHeight = FMath::RoundToInt(CommentBottom - CommentY);

	// Create the comment node
	UEdGraphNode_Comment* CommentNode = NewObject<UEdGraphNode_Comment>(Graph);
	CommentNode->CreateNewGuid();
	CommentNode->NodeComment = CommentText;
	CommentNode->NodePosX = FMath::RoundToInt(CommentX);
	CommentNode->NodePosY = FMath::RoundToInt(CommentY);
	CommentNode->NodeWidth = CommentWidth;
	CommentNode->NodeHeight = CommentHeight;

	// Color (optional RGBA)
	const TArray<TSharedPtr<FJsonValue>>* ColorArray = nullptr;
	if (Params->TryGetArrayField(TEXT("color"), ColorArray) && ColorArray->Num() >= 3)
	{
		float R = static_cast<float>((*ColorArray)[0]->AsNumber());
		float G = static_cast<float>((*ColorArray)[1]->AsNumber());
		float B = static_cast<float>((*ColorArray)[2]->AsNumber());
		float A = ColorArray->Num() >= 4 ? static_cast<float>((*ColorArray)[3]->AsNumber()) : 1.0f;
		CommentNode->CommentColor = FLinearColor(R, G, B, A);
	}

	Graph->AddNode(CommentNode, true, false);
	CommentNode->SetFlags(RF_Transactional);

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(CommentNode, Context);

	// Build response
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), CommentNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("comment_text"), CommentText);

	TArray<TSharedPtr<FJsonValue>> PosArray;
	PosArray.Add(MakeShared<FJsonValueNumber>(CommentX));
	PosArray.Add(MakeShared<FJsonValueNumber>(CommentY));
	ResultData->SetArrayField(TEXT("position"), PosArray);

	TArray<TSharedPtr<FJsonValue>> SizeArray;
	SizeArray.Add(MakeShared<FJsonValueNumber>(CommentWidth));
	SizeArray.Add(MakeShared<FJsonValueNumber>(CommentHeight));
	ResultData->SetArrayField(TEXT("size"), SizeArray);

	ResultData->SetNumberField(TEXT("nodes_wrapped"), NodesToWrap.Num());
	ResultData->SetBoolField(TEXT("wrap_all"), !bHasNodeIds);
	ResultData->SetBoolField(TEXT("collision_adjusted"), CommentY < OriginalCommentY);

	if (MissingNodes.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> MissingArray;
		for (const FString& Id : MissingNodes)
		{
			MissingArray.Add(MakeShared<FJsonValueString>(Id));
		}
		ResultData->SetArrayField(TEXT("missing_nodes"), MissingArray);
	}

	return CreateSuccessResponse(ResultData);
}


