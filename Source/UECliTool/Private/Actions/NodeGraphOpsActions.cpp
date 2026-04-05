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
// Graph Operations (connect, find, delete, inspect)
// ============================================================================

bool FConnectBlueprintNodesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString SourceNodeId, TargetNodeId, SourcePin, TargetPin;
	if (!GetRequiredString(Params, TEXT("source_node_id"), SourceNodeId, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("target_node_id"), TargetNodeId, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("source_pin"), SourcePin, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("target_pin"), TargetPin, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FConnectBlueprintNodesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SourceNodeId = Params->GetStringField(TEXT("source_node_id"));
	FString TargetNodeId = Params->GetStringField(TEXT("target_node_id"));
	FString SourcePinName = Params->GetStringField(TEXT("source_pin"));
	FString TargetPinName = Params->GetStringField(TEXT("target_pin"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the nodes
	UEdGraphNode* SourceNode = nullptr;
	UEdGraphNode* TargetNode = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node->NodeGuid.ToString() == SourceNodeId)
		{
			SourceNode = Node;
		}
		else if (Node->NodeGuid.ToString() == TargetNodeId)
		{
			TargetNode = Node;
		}
	}

	if (!SourceNode || !TargetNode)
	{
		return CreateErrorResponse(TEXT("Source or target node not found"));
	}

	// Find pins and provide detailed error messages
	UEdGraphPin* SourcePin = FMCPCommonUtils::FindPin(SourceNode, SourcePinName, EGPD_Output);
	UEdGraphPin* TargetPin = FMCPCommonUtils::FindPin(TargetNode, TargetPinName, EGPD_Input);

	auto GetAvailablePins = [](UEdGraphNode* Node, EEdGraphPinDirection Direction) -> FString
	{
		TArray<FString> PinNames;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == Direction && !Pin->bHidden)
			{
				PinNames.Add(FString::Printf(TEXT("'%s' (%s)"),
					*Pin->PinName.ToString(),
					*Pin->PinType.PinCategory.ToString()));
			}
		}
		return FString::Join(PinNames, TEXT(", "));
	};

	if (!SourcePin)
	{
		FString AvailablePins = GetAvailablePins(SourceNode, EGPD_Output);
		TArray<FString> Suggestions;
		Suggestions.Add(FString::Printf(TEXT("Available OUTPUT pins: [%s]"), *AvailablePins));
		Suggestions.Add(TEXT("Pin names are case-sensitive. Use get_node_pins to inspect exact names."));
		return CreateErrorResponseWithSuggestions(
			FString::Printf(TEXT("Source pin '%s' not found on node"), *SourcePinName),
			TEXT("pin_not_found"),
			Suggestions,
			FMCPCommonUtils::CollectAvailablePins(SourceNode));
	}

	if (!TargetPin)
	{
		FString AvailablePins = GetAvailablePins(TargetNode, EGPD_Input);
		TArray<FString> Suggestions;
		Suggestions.Add(FString::Printf(TEXT("Available INPUT pins: [%s]"), *AvailablePins));
		Suggestions.Add(TEXT("Pin names are case-sensitive. Use get_node_pins to inspect exact names."));
		return CreateErrorResponseWithSuggestions(
			FString::Printf(TEXT("Target pin '%s' not found on node"), *TargetPinName),
			TEXT("pin_not_found"),
			Suggestions,
			FMCPCommonUtils::CollectAvailablePins(TargetNode));
	}

	// Connect using the schema
	const UEdGraphSchema* Schema = TargetGraph->GetSchema();
	if (Schema)
	{
		bool bResult = Schema->TryCreateConnection(SourcePin, TargetPin);
		if (bResult)
		{
			SourceNode->PinConnectionListChanged(SourcePin);
			TargetNode->PinConnectionListChanged(TargetPin);
			MarkBlueprintModified(Blueprint, Context);

			TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
			ResultData->SetStringField(TEXT("source_node_id"), SourceNodeId);
			ResultData->SetStringField(TEXT("target_node_id"), TargetNodeId);
			return CreateSuccessResponse(ResultData);
		}
		else
		{
			return CreateErrorResponse(FString::Printf(
				TEXT("Schema refused connection: '%s' (%s) -> '%s' (%s). Types may be incompatible."),
				*SourcePin->PinName.ToString(), *SourcePin->PinType.PinCategory.ToString(),
				*TargetPin->PinName.ToString(), *TargetPin->PinType.PinCategory.ToString()));
		}
	}

	return CreateErrorResponse(TEXT("Failed to get graph schema"));
}


bool FFindBlueprintNodesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FFindBlueprintNodesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString NodeType = GetOptionalString(Params, TEXT("node_type"));
	FString EventName = GetOptionalString(Params, TEXT("event_name"));

	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	TArray<TSharedPtr<FJsonValue>> NodesArray;

	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (!Node) continue;

		bool bMatch = false;

		if (NodeType.IsEmpty())
		{
			// No filter - include all nodes
			bMatch = true;
		}
		else if (NodeType == TEXT("Event"))
		{
			UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
			if (EventNode)
			{
				if (EventName.IsEmpty())
				{
					bMatch = true;
				}
				else if (EventNode->EventReference.GetMemberName() == FName(*EventName))
				{
					bMatch = true;
				}
			}
			UK2Node_CustomEvent* CustomNode = Cast<UK2Node_CustomEvent>(Node);
			if (CustomNode)
			{
				if (EventName.IsEmpty())
				{
					bMatch = true;
				}
				else if (CustomNode->CustomFunctionName == EventName)
				{
					bMatch = true;
				}
			}
		}
		else if (NodeType == TEXT("Function"))
		{
			if (Cast<UK2Node_CallFunction>(Node))
			{
				bMatch = true;
			}
		}
		else if (NodeType == TEXT("Variable"))
		{
			if (Cast<UK2Node_VariableGet>(Node) || Cast<UK2Node_VariableSet>(Node))
			{
				bMatch = true;
			}
		}

		if (bMatch)
		{
			TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
			NodeObj->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());
			NodeObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
			NodeObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
		}
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetArrayField(TEXT("nodes"), NodesArray);
	ResultData->SetNumberField(TEXT("count"), NodesArray.Num());
	return CreateSuccessResponse(ResultData);
}


bool FDeleteBlueprintNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString NodeId;
	if (!GetRequiredString(Params, TEXT("node_id"), NodeId, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FDeleteBlueprintNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString NodeId = Params->GetStringField(TEXT("node_id"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the node
	UEdGraphNode* NodeToDelete = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node->NodeGuid.ToString() == NodeId)
		{
			NodeToDelete = Node;
			break;
		}
	}

	if (!NodeToDelete)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Node not found with ID: %s"), *NodeId));
	}

	FString NodeClass = NodeToDelete->GetClass()->GetName();
	FString NodeTitle = NodeToDelete->GetNodeTitle(ENodeTitleType::FullTitle).ToString();

	// Break all pin connections
	for (UEdGraphPin* Pin : NodeToDelete->Pins)
	{
		Pin->BreakAllPinLinks();
	}

	// Remove from graph
	TargetGraph->RemoveNode(NodeToDelete);
	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("deleted_node_id"), NodeId);
	ResultData->SetStringField(TEXT("deleted_node_class"), NodeClass);
	ResultData->SetStringField(TEXT("deleted_node_title"), NodeTitle);
	return CreateSuccessResponse(ResultData);
}


bool FGetNodePinsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString NodeId;
	if (!GetRequiredString(Params, TEXT("node_id"), NodeId, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FGetNodePinsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString NodeId = Params->GetStringField(TEXT("node_id"));
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

	// Build array of pin info (including links)
	TArray<TSharedPtr<FJsonValue>> PinsArray;
	for (UEdGraphPin* Pin : FoundNode->Pins)
	{
		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
		PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
		PinObj->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
		if (Pin->PinType.PinSubCategory != NAME_None)
		{
			PinObj->SetStringField(TEXT("sub_category"), Pin->PinType.PinSubCategory.ToString());
		}
		if (Pin->PinType.PinSubCategoryObject.Get())
		{
			PinObj->SetStringField(TEXT("sub_category_object"), Pin->PinType.PinSubCategoryObject->GetName());
		}
		PinObj->SetBoolField(TEXT("is_hidden"), Pin->bHidden);

		// Linked pins (node id + pin name)
		TArray<TSharedPtr<FJsonValue>> LinkedArray;
		for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (!LinkedPin) continue;
			UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
			if (!LinkedNode) continue;

			TSharedPtr<FJsonObject> LinkedObj = MakeShared<FJsonObject>();
			LinkedObj->SetStringField(TEXT("node_id"), LinkedNode->NodeGuid.ToString());
			LinkedObj->SetStringField(TEXT("pin_name"), LinkedPin->PinName.ToString());
			LinkedArray.Add(MakeShared<FJsonValueObject>(LinkedObj));
		}
		PinObj->SetArrayField(TEXT("linked_to"), LinkedArray);

		PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_class"), FoundNode->GetClass()->GetName());
	ResultData->SetArrayField(TEXT("pins"), PinsArray);
	return CreateSuccessResponse(ResultData);
}


