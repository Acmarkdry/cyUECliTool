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
// P2 — Graph Operation Enhancements
// ============================================================================

bool FDisconnectBlueprintPinAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString NodeId, PinName;
	if (!GetRequiredString(Params, TEXT("node_id"), NodeId, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("pin_name"), PinName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FDisconnectBlueprintPinAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString NodeId = Params->GetStringField(TEXT("node_id"));
	FString PinName = Params->GetStringField(TEXT("pin_name"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the node
	UEdGraphNode* FoundNode = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node->NodeGuid.ToString() == NodeId)
		{
			FoundNode = Node;
			break;
		}
	}

	if (!FoundNode)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Node not found with ID: %s"), *NodeId));
	}

	// Find the pin (try both directions)
	UEdGraphPin* TargetPin = FMCPCommonUtils::FindPin(FoundNode, PinName, EGPD_Output);
	if (!TargetPin)
	{
		TargetPin = FMCPCommonUtils::FindPin(FoundNode, PinName, EGPD_Input);
	}

	if (!TargetPin)
	{
		TArray<FString> Suggestions;
		Suggestions.Add(TEXT("Pin names are case-sensitive. Use get_node_pins to see exact names."));
		return CreateErrorResponseWithSuggestions(
			FString::Printf(TEXT("Pin '%s' not found on node"), *PinName),
			TEXT("pin_not_found"),
			Suggestions,
			FMCPCommonUtils::CollectAvailablePins(FoundNode));
	}

	int32 DisconnectedCount = TargetPin->LinkedTo.Num();
	TargetPin->BreakAllPinLinks();
	FoundNode->PinConnectionListChanged(TargetPin);
	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), NodeId);
	ResultData->SetStringField(TEXT("pin_name"), PinName);
	ResultData->SetNumberField(TEXT("disconnected_count"), DisconnectedCount);
	return CreateSuccessResponse(ResultData);
}


bool FMoveNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString NodeId;
	if (!GetRequiredString(Params, TEXT("node_id"), NodeId, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FMoveNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString NodeId = Params->GetStringField(TEXT("node_id"));
	FVector2D NewPosition = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the node
	UEdGraphNode* FoundNode = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node->NodeGuid.ToString() == NodeId)
		{
			FoundNode = Node;
			break;
		}
	}

	if (!FoundNode)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Node not found with ID: %s"), *NodeId));
	}

	int32 OldX = FoundNode->NodePosX;
	int32 OldY = FoundNode->NodePosY;
	FoundNode->NodePosX = FMath::RoundToInt(NewPosition.X);
	FoundNode->NodePosY = FMath::RoundToInt(NewPosition.Y);

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), NodeId);
	TArray<TSharedPtr<FJsonValue>> OldPosArr;
	OldPosArr.Add(MakeShared<FJsonValueNumber>(OldX));
	OldPosArr.Add(MakeShared<FJsonValueNumber>(OldY));
	ResultData->SetArrayField(TEXT("old_position"), OldPosArr);
	TArray<TSharedPtr<FJsonValue>> NewPosArr;
	NewPosArr.Add(MakeShared<FJsonValueNumber>(FoundNode->NodePosX));
	NewPosArr.Add(MakeShared<FJsonValueNumber>(FoundNode->NodePosY));
	ResultData->SetArrayField(TEXT("new_position"), NewPosArr);
	return CreateSuccessResponse(ResultData);
}


bool FAddRerouteNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddRerouteNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Create a Knot (reroute) node
	UK2Node_Knot* RerouteNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_Knot>(
		TargetGraph, Position, EK2NewNodeFlags::None
	);

	if (!RerouteNode)
	{
		return CreateErrorResponse(TEXT("Failed to create reroute node"));
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(RerouteNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), RerouteNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Graph Topology — FDescribeGraphAction (P2.1)
// ============================================================================

bool FDescribeGraphAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FDescribeGraphAction::SerializePinToJson(const UEdGraphPin* Pin, bool bIncludeHidden)
{
	if (!Pin)
	{
		return nullptr;
	}
	if (!bIncludeHidden && Pin->bHidden)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
	PinObj->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
	PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
	PinObj->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());

	if (Pin->PinType.PinSubCategory != NAME_None)
	{
		PinObj->SetStringField(TEXT("sub_category"), Pin->PinType.PinSubCategory.ToString());
	}
	if (Pin->PinType.PinSubCategoryObject.Get())
	{
		PinObj->SetStringField(TEXT("sub_category_object"), Pin->PinType.PinSubCategoryObject->GetPathName());
	}

	PinObj->SetBoolField(TEXT("is_connected"), Pin->LinkedTo.Num() > 0);

	// Linked pins
	TArray<TSharedPtr<FJsonValue>> LinkedArray;
	for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
	{
		if (!LinkedPin) continue;
		const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
		if (!LinkedNode) continue;

		TSharedPtr<FJsonObject> LinkedObj = MakeShared<FJsonObject>();
		LinkedObj->SetStringField(TEXT("node_id"), LinkedNode->NodeGuid.ToString());
		LinkedObj->SetStringField(TEXT("pin_name"), LinkedPin->PinName.ToString());
		LinkedArray.Add(MakeShared<FJsonValueObject>(LinkedObj));
	}
	PinObj->SetArrayField(TEXT("linked_to"), LinkedArray);

	// Default value
	if (!Pin->DefaultValue.IsEmpty())
	{
		PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
	}
	else if (!Pin->AutogeneratedDefaultValue.IsEmpty())
	{
		PinObj->SetStringField(TEXT("default_value"), Pin->AutogeneratedDefaultValue);
	}

	return PinObj;
}

TSharedPtr<FJsonObject> FDescribeGraphAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);
	if (!TargetGraph)
	{
		return CreateErrorResponse(TEXT("Target graph not found"));
	}

	const bool bIncludeHidden = GetOptionalBool(Params, TEXT("include_hidden_pins"), false);

	// Build node array and collect edges (de-duplicated)
	TArray<TSharedPtr<FJsonValue>> NodesArray;
	TArray<TSharedPtr<FJsonValue>> EdgesArray;
	TSet<FString> SeenEdges; // "FromGuid:FromPin->ToGuid:ToPin"

	for (const UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (!Node) continue;

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
		NodeObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
		NodeObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
		NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);

		if (!Node->NodeComment.IsEmpty())
		{
			NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);
		}

		// Serialize pins
		TArray<TSharedPtr<FJsonValue>> PinsArray;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			TSharedPtr<FJsonObject> PinObj = SerializePinToJson(Pin, bIncludeHidden);
			if (PinObj.IsValid())
			{
				PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
			}

			// Collect edges (output pins only to avoid duplicates)
			if (Pin && Pin->Direction == EGPD_Output)
			{
				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin) continue;
					const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
					if (!LinkedNode) continue;

					// De-duplicate: create canonical edge key
					FString EdgeKey = FString::Printf(TEXT("%s:%s->%s:%s"),
						*Node->NodeGuid.ToString(), *Pin->PinName.ToString(),
						*LinkedNode->NodeGuid.ToString(), *LinkedPin->PinName.ToString());

					if (!SeenEdges.Contains(EdgeKey))
					{
						SeenEdges.Add(EdgeKey);

						TSharedPtr<FJsonObject> EdgeObj = MakeShared<FJsonObject>();
						EdgeObj->SetStringField(TEXT("from_node"), Node->NodeGuid.ToString());
						EdgeObj->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
						EdgeObj->SetStringField(TEXT("to_node"), LinkedNode->NodeGuid.ToString());
						EdgeObj->SetStringField(TEXT("to_pin"), LinkedPin->PinName.ToString());
						EdgesArray.Add(MakeShared<FJsonValueObject>(EdgeObj));
					}
				}
			}
		}
		NodeObj->SetArrayField(TEXT("pins"), PinsArray);

		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("graph_name"), TargetGraph->GetName());
	ResultData->SetNumberField(TEXT("node_count"), NodesArray.Num());
	ResultData->SetNumberField(TEXT("edge_count"), EdgesArray.Num());
	ResultData->SetArrayField(TEXT("nodes"), NodesArray);
	ResultData->SetArrayField(TEXT("edges"), EdgesArray);
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Graph Selection Read — FGetSelectedNodesAction
// ============================================================================

#define private public
#define protected public
#include "BlueprintEditor.h"
#undef protected
#undef private
#include "Subsystems/AssetEditorSubsystem.h"

// GetActiveBlueprintEditorForAction removed — use FMCPCommonUtils::GetActiveBlueprintEditor() instead.

bool FGetSelectedNodesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// No required params — works with editor selection or optional blueprint_name
	return true;
}

bool FCollapseSelectionToFunctionAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// No required params — action works on current selection in focused Blueprint editor.
	// Optional: blueprint_name to target a specific open Blueprint editor.
	return true;
}

TSharedPtr<FJsonObject> FCollapseSelectionToFunctionAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = GetOptionalString(Params, TEXT("blueprint_name"));

	FBlueprintEditor* BPEditor = FMCPCommonUtils::GetActiveBlueprintEditor(BlueprintName);
	if (!BPEditor)
	{
		return CreateErrorResponse(
			TEXT("No Blueprint editor is currently open. Open a Blueprint and select nodes first."),
			TEXT("no_editor"));
	}

	if (!BPEditor->GraphEditorCommands.IsValid())
	{
		return CreateErrorResponse(TEXT("Blueprint editor graph command list is not available."), TEXT("no_graph_commands"));
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(BPEditor->GetBlueprintObj());
	if (!Blueprint)
	{
		return CreateErrorResponse(TEXT("Failed to resolve Blueprint from active editor."), TEXT("blueprint_not_found"));
	}

	UEdGraph* FocusedGraph = BPEditor->GetFocusedGraph();
	if (!FocusedGraph)
	{
		return CreateErrorResponse(TEXT("Blueprint editor has no focused graph."), TEXT("no_graph"));
	}

	// ── Pre-flight: can the engine collapse the current selection? ──
	// NOTE: Delegate-binding nodes (AddDelegate, CreateDelegate, CallDelegate)
	// are fully supported by the engine's CollapseSelectionToFunction flow.
	// Only UK2Node_ComponentBoundEvent (an Event subclass) cannot be placed
	// in function graphs, but the engine's own CanPasteHere validation handles
	// that correctly — no need for MCP-side preemptive exclusion.
	const TSharedRef<const FUICommandInfo> CollapseToFunctionCommand = FGraphEditorCommands::Get().CollapseSelectionToFunction.ToSharedRef();
	if (!BPEditor->GraphEditorCommands->CanExecuteAction(CollapseToFunctionCommand))
	{
		return CreateErrorResponse(
			TEXT("Current selection cannot be collapsed to function. Ensure a valid node selection in a non-AnimGraph Blueprint graph."),
			TEXT("cannot_collapse_selection"));
	}

	// ── Snapshot existing function graphs ───────────────────────────
	TSet<FName> ExistingFunctionNames;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph)
		{
			ExistingFunctionNames.Add(Graph->GetFName());
		}
	}

	const int32 BeforeCount = ExistingFunctionNames.Num();
	if (!BPEditor->GraphEditorCommands->TryExecuteAction(CollapseToFunctionCommand))
	{
		return CreateErrorResponse(
			TEXT("Failed to execute Collapse Selection To Function command."),
			TEXT("collapse_command_failed"));
	}

	// ── Detect newly-created function graph(s) ──────────────────────
	TArray<TSharedPtr<FJsonValue>> NewFunctionNamesJson;
	FString NewestFunctionName;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		if (!ExistingFunctionNames.Contains(Graph->GetFName()))
		{
			const FString FunctionName = Graph->GetName();
			NewestFunctionName = FunctionName;
			NewFunctionNamesJson.Add(MakeShared<FJsonValueString>(FunctionName));
		}
	}

	if (NewFunctionNamesJson.Num() == 0)
	{
		return CreateErrorResponse(
			TEXT("Collapse to function did not create a new function graph. Check Blueprint Message Log for detailed validation errors."),
			TEXT("collapse_failed"));
	}

	Context.MarkPackageDirty(Blueprint->GetOutermost());

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
	ResultData->SetStringField(TEXT("source_graph"), FocusedGraph->GetName());
	ResultData->SetNumberField(TEXT("function_count_before"), BeforeCount);
	ResultData->SetNumberField(TEXT("function_count_after"), Blueprint->FunctionGraphs.Num());
	ResultData->SetArrayField(TEXT("created_functions"), NewFunctionNamesJson);
	ResultData->SetStringField(TEXT("created_function"), NewestFunctionName);

	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// FCollapseSelectionToMacroAction
// ============================================================================

bool FCollapseSelectionToMacroAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// No required params — action works on current selection in focused Blueprint editor.
	// Optional: blueprint_name to target a specific open Blueprint editor.
	return true;
}

TSharedPtr<FJsonObject> FCollapseSelectionToMacroAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = GetOptionalString(Params, TEXT("blueprint_name"));

	FBlueprintEditor* BPEditor = FMCPCommonUtils::GetActiveBlueprintEditor(BlueprintName);
	if (!BPEditor)
	{
		return CreateErrorResponse(
			TEXT("No Blueprint editor is currently open. Open a Blueprint and select nodes first."),
			TEXT("no_editor"));
	}

	if (!BPEditor->GraphEditorCommands.IsValid())
	{
		return CreateErrorResponse(TEXT("Blueprint editor graph command list is not available."), TEXT("no_graph_commands"));
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(BPEditor->GetBlueprintObj());
	if (!Blueprint)
	{
		return CreateErrorResponse(TEXT("Failed to resolve Blueprint from active editor."), TEXT("blueprint_not_found"));
	}

	UEdGraph* FocusedGraph = BPEditor->GetFocusedGraph();
	if (!FocusedGraph)
	{
		return CreateErrorResponse(TEXT("Blueprint editor has no focused graph."), TEXT("no_graph"));
	}

	// ── Pre-flight: can the engine collapse the current selection to a macro? ──
	const TSharedRef<const FUICommandInfo> CollapseToMacroCommand = FGraphEditorCommands::Get().CollapseSelectionToMacro.ToSharedRef();
	if (!BPEditor->GraphEditorCommands->CanExecuteAction(CollapseToMacroCommand))
	{
		return CreateErrorResponse(
			TEXT("Current selection cannot be collapsed to macro. Ensure a valid node selection in a Blueprint graph (not AnimGraph)."),
			TEXT("cannot_collapse_selection"));
	}

	// ── Snapshot existing macro graphs ──────────────────────────────
	TSet<FName> ExistingMacroNames;
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (Graph)
		{
			ExistingMacroNames.Add(Graph->GetFName());
		}
	}

	const int32 BeforeCount = ExistingMacroNames.Num();
	if (!BPEditor->GraphEditorCommands->TryExecuteAction(CollapseToMacroCommand))
	{
		return CreateErrorResponse(
			TEXT("Failed to execute Collapse Selection To Macro command."),
			TEXT("collapse_command_failed"));
	}

	// ── Detect newly-created macro graph(s) ─────────────────────────
	TArray<TSharedPtr<FJsonValue>> NewMacroNamesJson;
	FString NewestMacroName;
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		if (!ExistingMacroNames.Contains(Graph->GetFName()))
		{
			const FString MacroName = Graph->GetName();
			NewestMacroName = MacroName;
			NewMacroNamesJson.Add(MakeShared<FJsonValueString>(MacroName));
		}
	}

	if (NewMacroNamesJson.Num() == 0)
	{
		return CreateErrorResponse(
			TEXT("Collapse to macro did not create a new macro graph. Check Blueprint Message Log for detailed validation errors."),
			TEXT("collapse_failed"));
	}

	Context.MarkPackageDirty(Blueprint->GetOutermost());

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
	ResultData->SetStringField(TEXT("source_graph"), FocusedGraph->GetName());
	ResultData->SetNumberField(TEXT("macro_count_before"), BeforeCount);
	ResultData->SetNumberField(TEXT("macro_count_after"), Blueprint->MacroGraphs.Num());
	ResultData->SetArrayField(TEXT("created_macros"), NewMacroNamesJson);
	ResultData->SetStringField(TEXT("created_macro"), NewestMacroName);

	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Graph Selection Write — FSetSelectedNodesAction / FBatchSelectAndActAction
// ============================================================================

bool FSetSelectedNodesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* NodeIdsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("node_ids"), NodeIdsArray) || !NodeIdsArray || NodeIdsArray->Num() == 0)
	{
		OutError = TEXT("'node_ids' is required and must be a non-empty array of GUID strings.");
		return false;
	}
	return true;
}

/**
 * Helper: resolve nodes by GUID in a graph and set selection on the SGraphEditor.
 * Returns false + OutError on failure. On success, populates OutSelectedCount/OutMissingIds.
 */
static bool SetSelectionByNodeIds(
	FBlueprintEditor* BPEditor,
	UEdGraph* Graph,
	const TArray<TSharedPtr<FJsonValue>>& NodeIdsArray,
	bool bAppend,
	int32& OutSelectedCount,
	TArray<TSharedPtr<FJsonValue>>& OutSelectedIds,
	TArray<TSharedPtr<FJsonValue>>& OutMissingIds,
	FString& OutError)
{
	// Access the focused SGraphEditor via the private member (already exposed by the #define hack above)
	TSharedPtr<SGraphEditor> GraphEditor = BPEditor->FocusedGraphEdPtr.Pin();
	if (!GraphEditor.IsValid())
	{
		OutError = TEXT("No focused graph editor widget available. Ensure a graph tab is focused.");
		return false;
	}

	// Resolve node GUIDs
	TArray<UEdGraphNode*> NodesToSelect;

	for (const TSharedPtr<FJsonValue>& IdValue : NodeIdsArray)
	{
		FString NodeIdStr = IdValue->AsString();
		FGuid NodeGuid;
		if (!FGuid::Parse(NodeIdStr, NodeGuid))
		{
			OutMissingIds.Add(MakeShared<FJsonValueString>(NodeIdStr + TEXT(" (invalid GUID)")));
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

		if (FoundNode)
		{
			NodesToSelect.Add(FoundNode);
			OutSelectedIds.Add(MakeShared<FJsonValueString>(NodeIdStr));
		}
		else
		{
			OutMissingIds.Add(MakeShared<FJsonValueString>(NodeIdStr));
		}
	}

	// Set the selection
	if (!bAppend)
	{
		GraphEditor->ClearSelectionSet();
	}

	for (UEdGraphNode* Node : NodesToSelect)
	{
		GraphEditor->SetNodeSelection(Node, true);
	}

	OutSelectedCount = NodesToSelect.Num();
	return true;
}

TSharedPtr<FJsonObject> FSetSelectedNodesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = GetOptionalString(Params, TEXT("blueprint_name"));
	FBlueprintEditor* BPEditor = FMCPCommonUtils::GetActiveBlueprintEditor(BlueprintName);
	if (!BPEditor)
	{
		return CreateErrorResponse(
			TEXT("No Blueprint editor is currently open. Open a Blueprint and focus a graph first."),
			TEXT("no_editor"));
	}

	UEdGraph* FocusedGraph = BPEditor->GetFocusedGraph();
	if (!FocusedGraph)
	{
		return CreateErrorResponse(TEXT("Blueprint editor has no focused graph."), TEXT("no_graph"));
	}

	// Optional: find specific graph
	FString RequestedGraph = GetOptionalString(Params, TEXT("graph_name"));
	if (!RequestedGraph.IsEmpty() && FocusedGraph->GetName() != RequestedGraph)
	{
		UBlueprint* BP = Cast<UBlueprint>(BPEditor->GetBlueprintObj());
		if (BP)
		{
			FString FindError;
			UEdGraph* FoundGraph = FindGraph(BP, RequestedGraph, FindError);
			if (FoundGraph)
			{
				// Bring this graph to front so the SGraphEditor is valid for it
				BPEditor->OpenGraphAndBringToFront(FoundGraph);
				FocusedGraph = FoundGraph;
			}
			else
			{
				return CreateErrorResponse(
					FString::Printf(TEXT("Graph '%s' not found: %s"), *RequestedGraph, *FindError),
					TEXT("graph_not_found"));
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* NodeIdsArray = nullptr;
	Params->TryGetArrayField(TEXT("node_ids"), NodeIdsArray);
	bool bAppend = GetOptionalBool(Params, TEXT("append"), false);

	int32 SelectedCount = 0;
	TArray<TSharedPtr<FJsonValue>> SelectedIds;
	TArray<TSharedPtr<FJsonValue>> MissingIds;
	FString SelectError;

	if (!SetSelectionByNodeIds(BPEditor, FocusedGraph, *NodeIdsArray, bAppend, SelectedCount, SelectedIds, MissingIds, SelectError))
	{
		return CreateErrorResponse(SelectError, TEXT("selection_failed"));
	}

	UBlueprint* BP = Cast<UBlueprint>(BPEditor->GetBlueprintObj());
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("blueprint_name"), BP ? BP->GetName() : TEXT("Unknown"));
	ResultData->SetStringField(TEXT("graph_name"), FocusedGraph->GetName());
	ResultData->SetNumberField(TEXT("selected_count"), SelectedCount);
	ResultData->SetArrayField(TEXT("selected_ids"), SelectedIds);
	if (MissingIds.Num() > 0)
	{
		ResultData->SetArrayField(TEXT("missing_ids"), MissingIds);
	}
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// FBatchSelectAndActAction
// ============================================================================

bool FBatchSelectAndActAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* GroupsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("groups"), GroupsArray) || !GroupsArray || GroupsArray->Num() == 0)
	{
		OutError = TEXT("'groups' is required and must be a non-empty array of group objects.");
		return false;
	}

	for (int32 i = 0; i < GroupsArray->Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* GroupObj = nullptr;
		if (!(*GroupsArray)[i]->TryGetObject(GroupObj) || !GroupObj || !GroupObj->IsValid())
		{
			OutError = FString::Printf(TEXT("groups[%d] must be a JSON object."), i);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* InnerNodeIds = nullptr;
		if (!(*GroupObj)->TryGetArrayField(TEXT("node_ids"), InnerNodeIds) || !InnerNodeIds || InnerNodeIds->Num() == 0)
		{
			OutError = FString::Printf(TEXT("groups[%d].node_ids is required and must be a non-empty array."), i);
			return false;
		}

		FString ActionCmd;
		if (!(*GroupObj)->TryGetStringField(TEXT("action"), ActionCmd) || ActionCmd.IsEmpty())
		{
			OutError = FString::Printf(TEXT("groups[%d].action is required (e.g. 'collapse_selection_to_function', 'auto_comment')."), i);
			return false;
		}
	}

	return true;
}

TSharedPtr<FJsonObject> FBatchSelectAndActAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = GetOptionalString(Params, TEXT("blueprint_name"));
	FBlueprintEditor* BPEditor = FMCPCommonUtils::GetActiveBlueprintEditor(BlueprintName);
	if (!BPEditor)
	{
		return CreateErrorResponse(
			TEXT("No Blueprint editor is currently open."),
			TEXT("no_editor"));
	}

	UEdGraph* FocusedGraph = BPEditor->GetFocusedGraph();
	if (!FocusedGraph)
	{
		return CreateErrorResponse(TEXT("Blueprint editor has no focused graph."), TEXT("no_graph"));
	}

	// Optional: find specific graph
	FString RequestedGraph = GetOptionalString(Params, TEXT("graph_name"));
	if (!RequestedGraph.IsEmpty() && FocusedGraph->GetName() != RequestedGraph)
	{
		UBlueprint* BP = Cast<UBlueprint>(BPEditor->GetBlueprintObj());
		if (BP)
		{
			FString FindError;
			UEdGraph* FoundGraph = FindGraph(BP, RequestedGraph, FindError);
			if (FoundGraph)
			{
				BPEditor->OpenGraphAndBringToFront(FoundGraph);
				FocusedGraph = FoundGraph;
			}
			else
			{
				return CreateErrorResponse(
					FString::Printf(TEXT("Graph '%s' not found: %s"), *RequestedGraph, *FindError),
					TEXT("graph_not_found"));
			}
		}
	}

	// Access SGraphEditor
	TSharedPtr<SGraphEditor> GraphEditor = BPEditor->FocusedGraphEdPtr.Pin();
	if (!GraphEditor.IsValid())
	{
		return CreateErrorResponse(
			TEXT("No focused graph editor widget available."),
			TEXT("no_graph_editor_widget"));
	}

	// Get the MCPBridge to dispatch sub-actions
	UMCPBridge* Bridge = nullptr;
	for (TObjectIterator<UMCPBridge> It; It; ++It)
	{
		Bridge = *It;
		break;
	}
	if (!Bridge)
	{
		return CreateErrorResponse(TEXT("MCPBridge not found — cannot dispatch sub-actions."), TEXT("no_bridge"));
	}

	const TArray<TSharedPtr<FJsonValue>>* GroupsArray = nullptr;
	Params->TryGetArrayField(TEXT("groups"), GroupsArray);

	TArray<TSharedPtr<FJsonValue>> ResultsArray;

	for (int32 GroupIdx = 0; GroupIdx < GroupsArray->Num(); ++GroupIdx)
	{
		TSharedPtr<FJsonObject> GroupResult = MakeShared<FJsonObject>();
		GroupResult->SetNumberField(TEXT("group_index"), GroupIdx);

		const TSharedPtr<FJsonObject>& GroupObj = (*GroupsArray)[GroupIdx]->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* NodeIds = nullptr;
		GroupObj->TryGetArrayField(TEXT("node_ids"), NodeIds);
		FString ActionCmd = GroupObj->GetStringField(TEXT("action"));

		// 1. Clear selection and select the group's nodes
		GraphEditor->ClearSelectionSet();

		TArray<FString> SelectedIds;
		TArray<FString> MissingIds;

		for (const TSharedPtr<FJsonValue>& IdValue : *NodeIds)
		{
			FString NodeIdStr = IdValue->AsString();
			FGuid NodeGuid;
			if (!FGuid::Parse(NodeIdStr, NodeGuid))
			{
				MissingIds.Add(NodeIdStr + TEXT(" (invalid GUID)"));
				continue;
			}

			UEdGraphNode* FoundNode = nullptr;
			for (UEdGraphNode* Node : FocusedGraph->Nodes)
			{
				if (Node && Node->NodeGuid == NodeGuid)
				{
					FoundNode = Node;
					break;
				}
			}

			if (FoundNode)
			{
				GraphEditor->SetNodeSelection(FoundNode, true);
				SelectedIds.Add(NodeIdStr);
			}
			else
			{
				MissingIds.Add(NodeIdStr);
			}
		}

		GroupResult->SetNumberField(TEXT("selected_count"), SelectedIds.Num());
		if (MissingIds.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> MissingJson;
			for (const FString& Id : MissingIds)
			{
				MissingJson.Add(MakeShared<FJsonValueString>(Id));
			}
			GroupResult->SetArrayField(TEXT("missing_ids"), MissingJson);
		}

		// 2. Build params for the sub-action (merge blueprint_name, graph_name, and action_params)
		TSharedPtr<FJsonObject> ActionParams = MakeShared<FJsonObject>();
		if (!BlueprintName.IsEmpty())
		{
			ActionParams->SetStringField(TEXT("blueprint_name"), BlueprintName);
		}
		if (!RequestedGraph.IsEmpty())
		{
			ActionParams->SetStringField(TEXT("graph_name"), RequestedGraph);
		}

		// Merge user-provided action_params
		const TSharedPtr<FJsonObject>* ExtraParams = nullptr;
		if (GroupObj->TryGetObjectField(TEXT("action_params"), ExtraParams) && ExtraParams && (*ExtraParams).IsValid())
		{
			for (const auto& Pair : (*ExtraParams)->Values)
			{
				ActionParams->SetField(Pair.Key, Pair.Value);
			}
		}

		// For auto_comment: inject node_ids so it wraps the correct nodes
		if (ActionCmd == TEXT("auto_comment"))
		{
			ActionParams->SetArrayField(TEXT("node_ids"), *const_cast<TArray<TSharedPtr<FJsonValue>>*>(NodeIds));
		}

		// 3. Execute the sub-action via MCPBridge::ExecuteCommand
		TSharedPtr<FJsonObject> ActionResult = Bridge->ExecuteCommand(ActionCmd, ActionParams);
		GroupResult->SetStringField(TEXT("action"), ActionCmd);
		GroupResult->SetObjectField(TEXT("action_result"), ActionResult);
		
		// Check if the sub-action succeeded
		bool bSubSuccess = false;
		if (ActionResult.IsValid())
		{
			ActionResult->TryGetBoolField(TEXT("success"), bSubSuccess);
		}
		GroupResult->SetBoolField(TEXT("success"), bSubSuccess);

		ResultsArray.Add(MakeShared<FJsonValueObject>(GroupResult));
	}

	// Restore clean selection state
	GraphEditor->ClearSelectionSet();

	UBlueprint* BP = Cast<UBlueprint>(BPEditor->GetBlueprintObj());
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("blueprint_name"), BP ? BP->GetName() : TEXT("Unknown"));
	ResultData->SetStringField(TEXT("graph_name"), FocusedGraph->GetName());
	ResultData->SetNumberField(TEXT("group_count"), GroupsArray->Num());
	ResultData->SetArrayField(TEXT("results"), ResultsArray);
	return CreateSuccessResponse(ResultData);
}


TSharedPtr<FJsonObject> FGetSelectedNodesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	// Resolve which editor to inspect
	FString BlueprintName = GetOptionalString(Params, TEXT("blueprint_name"));
	FBlueprintEditor* BPEditor = FMCPCommonUtils::GetActiveBlueprintEditor(BlueprintName);

	if (!BPEditor)
	{
		return CreateErrorResponse(
			TEXT("No Blueprint editor is currently open. Open a Blueprint in the editor first."),
			TEXT("no_editor"));
	}

	UEdGraph* FocusedGraph = BPEditor->GetFocusedGraph();
	if (!FocusedGraph)
	{
		return CreateErrorResponse(
			TEXT("Blueprint editor has no focused graph."),
			TEXT("no_graph"));
	}

	// Optional: filter by graph_name
	FString RequestedGraph = GetOptionalString(Params, TEXT("graph_name"));
	if (!RequestedGraph.IsEmpty() && FocusedGraph->GetName() != RequestedGraph)
	{
		// Try to find the requested graph in the same Blueprint
		UBlueprint* BP = Cast<UBlueprint>(BPEditor->GetBlueprintObj());
		if (BP)
		{
			FString FindError;
			UEdGraph* FoundGraph = FindGraph(BP, RequestedGraph, FindError);
			if (FoundGraph)
			{
				FocusedGraph = FoundGraph;
			}
			else
			{
				return CreateErrorResponse(
					FString::Printf(TEXT("Graph '%s' not found: %s"), *RequestedGraph, *FindError),
					TEXT("graph_not_found"));
			}
		}
	}

	// Get selected nodes
	FGraphPanelSelectionSet SelectedNodes = BPEditor->GetSelectedNodes();

	const bool bIncludeHidden = GetOptionalBool(Params, TEXT("include_hidden_pins"), false);

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	TArray<TSharedPtr<FJsonValue>> EdgesArray;
	TSet<FString> SeenEdges;

	for (UObject* Obj : SelectedNodes)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(Obj);
		if (!Node) continue;

		// Only include nodes that belong to the focused graph
		if (!FocusedGraph->Nodes.Contains(Node)) continue;

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
		NodeObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
		NodeObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
		NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);

		if (!Node->NodeComment.IsEmpty())
		{
			NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);
		}

		// Serialize pins (reuse FDescribeGraphAction pattern)
		TArray<TSharedPtr<FJsonValue>> PinsArray;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;
			if (!bIncludeHidden && Pin->bHidden) continue;

			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
			PinObj->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());

			if (Pin->PinType.PinSubCategory != NAME_None)
			{
				PinObj->SetStringField(TEXT("sub_category"), Pin->PinType.PinSubCategory.ToString());
			}
			if (Pin->PinType.PinSubCategoryObject.Get())
			{
				PinObj->SetStringField(TEXT("sub_category_object"), Pin->PinType.PinSubCategoryObject->GetPathName());
			}

			PinObj->SetBoolField(TEXT("is_connected"), Pin->LinkedTo.Num() > 0);

			// Default value
			if (!Pin->DefaultValue.IsEmpty())
			{
				PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
			}
			else if (!Pin->AutogeneratedDefaultValue.IsEmpty())
			{
				PinObj->SetStringField(TEXT("default_value"), Pin->AutogeneratedDefaultValue);
			}

			// Linked pins
			TArray<TSharedPtr<FJsonValue>> LinkedArray;
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin) continue;
				const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
				if (!LinkedNode) continue;

				TSharedPtr<FJsonObject> LinkedObj = MakeShared<FJsonObject>();
				LinkedObj->SetStringField(TEXT("node_id"), LinkedNode->NodeGuid.ToString());
				LinkedObj->SetStringField(TEXT("pin_name"), LinkedPin->PinName.ToString());
				LinkedArray.Add(MakeShared<FJsonValueObject>(LinkedObj));
			}
			PinObj->SetArrayField(TEXT("linked_to"), LinkedArray);

			PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));

			// Collect edges (output only, de-duplicate)
			if (Pin->Direction == EGPD_Output)
			{
				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin) continue;
					const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
					if (!LinkedNode) continue;

					FString EdgeKey = FString::Printf(TEXT("%s:%s->%s:%s"),
						*Node->NodeGuid.ToString(), *Pin->PinName.ToString(),
						*LinkedNode->NodeGuid.ToString(), *LinkedPin->PinName.ToString());

					if (!SeenEdges.Contains(EdgeKey))
					{
						SeenEdges.Add(EdgeKey);
						TSharedPtr<FJsonObject> EdgeObj = MakeShared<FJsonObject>();
						EdgeObj->SetStringField(TEXT("from_node"), Node->NodeGuid.ToString());
						EdgeObj->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
						EdgeObj->SetStringField(TEXT("to_node"), LinkedNode->NodeGuid.ToString());
						EdgeObj->SetStringField(TEXT("to_pin"), LinkedPin->PinName.ToString());
						EdgesArray.Add(MakeShared<FJsonValueObject>(EdgeObj));
					}
				}
			}
		}
		NodeObj->SetArrayField(TEXT("pins"), PinsArray);
		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	// Build result
	UBlueprint* BP = Cast<UBlueprint>(BPEditor->GetBlueprintObj());
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("blueprint_name"), BP ? BP->GetName() : TEXT("Unknown"));
	ResultData->SetStringField(TEXT("graph_name"), FocusedGraph->GetName());
	ResultData->SetNumberField(TEXT("selected_count"), NodesArray.Num());
	ResultData->SetNumberField(TEXT("edge_count"), EdgesArray.Num());
	ResultData->SetArrayField(TEXT("nodes"), NodesArray);
	ResultData->SetArrayField(TEXT("edges"), EdgesArray);
	return CreateSuccessResponse(ResultData);
}
