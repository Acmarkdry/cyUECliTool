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
// Variable Nodes
// ============================================================================

bool FAddBlueprintVariableAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString VariableName, VariableType;
	if (!GetRequiredString(Params, TEXT("variable_name"), VariableName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("variable_type"), VariableType, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintVariableAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString VariableName = Params->GetStringField(TEXT("variable_name"));
	FString VariableType = Params->GetStringField(TEXT("variable_type"));
	bool IsExposed = GetOptionalBool(Params, TEXT("is_exposed"), false);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Create variable based on type
	FEdGraphPinType PinType;
	FString TypeResolveError;
	if (!FMCPCommonUtils::ResolvePinTypeFromString(VariableType, PinType, TypeResolveError))
	{
		return CreateErrorResponse(TypeResolveError);
	}

	FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*VariableName), PinType);

	// Set variable properties
	for (FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarName == FName(*VariableName))
		{
			if (IsExposed)
			{
				Variable.PropertyFlags |= CPF_Edit;
				Variable.PropertyFlags &= ~CPF_DisableEditOnInstance;
				// Use UE API to ensure structural modification notification
				FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, FName(*VariableName), false);
			}
			break;
		}
	}

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("variable_name"), VariableName);
	ResultData->SetStringField(TEXT("variable_type"), VariableType);
	return CreateSuccessResponse(ResultData);
}


bool FAddBlueprintVariableGetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString VariableName;
	if (!GetRequiredString(Params, TEXT("variable_name"), VariableName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintVariableGetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString VariableName = Params->GetStringField(TEXT("variable_name"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Check if this is a local variable in a function graph
	bool bIsLocalVar = false;
	FGuid LocalVarGuid;
	FString GraphNameStr = TargetGraph->GetName();
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
		{
			for (const FBPVariableDescription& LocalVar : EntryNode->LocalVariables)
			{
				if (LocalVar.VarName == FName(*VariableName))
				{
					bIsLocalVar = true;
					LocalVarGuid = LocalVar.VarGuid;
					break;
				}
			}
			break;
		}
	}

	UK2Node_VariableGet* VarGetNode = nullptr;
	if (bIsLocalVar)
	{
		VarGetNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableGet>(
			TargetGraph, Position, EK2NewNodeFlags::None,
			[&VariableName, &GraphNameStr, &LocalVarGuid](UK2Node_VariableGet* Node)
			{
				Node->VariableReference.SetLocalMember(FName(*VariableName), GraphNameStr, LocalVarGuid);
			}
		);
	}
	else
	{
		VarGetNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableGet>(
			TargetGraph, Position, EK2NewNodeFlags::None,
			[&VariableName](UK2Node_VariableGet* Node) { Node->VariableReference.SetSelfMember(FName(*VariableName)); }
		);
	}
	VarGetNode->ReconstructNode();

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(VarGetNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), VarGetNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


bool FAddBlueprintVariableSetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString VariableName;
	if (!GetRequiredString(Params, TEXT("variable_name"), VariableName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintVariableSetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString VariableName = Params->GetStringField(TEXT("variable_name"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Check if this is a local variable in a function graph
	bool bIsLocalVar = false;
	FGuid LocalVarGuid;
	FString GraphNameStr = TargetGraph->GetName();
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
		{
			for (const FBPVariableDescription& LocalVar : EntryNode->LocalVariables)
			{
				if (LocalVar.VarName == FName(*VariableName))
				{
					bIsLocalVar = true;
					LocalVarGuid = LocalVar.VarGuid;
					break;
				}
			}
			break;
		}
	}

	UK2Node_VariableSet* VarSetNode = nullptr;
	if (bIsLocalVar)
	{
		VarSetNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableSet>(
			TargetGraph, Position, EK2NewNodeFlags::None,
			[&VariableName, &GraphNameStr, &LocalVarGuid](UK2Node_VariableSet* Node)
			{
				Node->VariableReference.SetLocalMember(FName(*VariableName), GraphNameStr, LocalVarGuid);
			}
		);
	}
	else
	{
		VarSetNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableSet>(
			TargetGraph, Position, EK2NewNodeFlags::None,
			[&VariableName](UK2Node_VariableSet* Node) { Node->VariableReference.SetSelfMember(FName(*VariableName)); }
		);
	}
	VarSetNode->ReconstructNode();

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(VarSetNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), VarSetNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


bool FSetNodePinDefaultAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString NodeId, PinName, DefaultValue;
	if (!GetRequiredString(Params, TEXT("node_id"), NodeId, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("pin_name"), PinName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("default_value"), DefaultValue, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FSetNodePinDefaultAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString NodeId = Params->GetStringField(TEXT("node_id"));
	FString PinName = Params->GetStringField(TEXT("pin_name"));
	FString DefaultValue = Params->GetStringField(TEXT("default_value"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the node
	UEdGraphNode* TargetNode = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node->NodeGuid.ToString() == NodeId)
		{
			TargetNode = Node;
			break;
		}
	}

	if (!TargetNode)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Node not found: %s"), *NodeId));
	}

	// Find the pin — try input first, then output (some default values are on output pins)
	UEdGraphPin* TargetPin = FMCPCommonUtils::FindPin(TargetNode, PinName, EGPD_Input);
	if (!TargetPin)
	{
		TargetPin = FMCPCommonUtils::FindPin(TargetNode, PinName, EGPD_Output);
	}
	if (!TargetPin)
	{
		TArray<FString> Suggestions;
		Suggestions.Add(TEXT("Pin names are case-sensitive."));
		Suggestions.Add(TEXT("Use get_node_pins with the node_id to see all available pins."));
		return CreateErrorResponseWithSuggestions(
			FString::Printf(TEXT("Pin '%s' not found on node"), *PinName),
			TEXT("pin_not_found"),
			Suggestions,
			FMCPCommonUtils::CollectAvailablePins(TargetNode));
	}

	// Set the default value - handle object pins differently
	if (TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
		TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
		TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
		TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
	{
		UObject* LoadedObject = StaticLoadObject(UObject::StaticClass(), nullptr, *DefaultValue);
		if (LoadedObject)
		{
			TargetPin->DefaultObject = LoadedObject;
			TargetPin->DefaultValue.Empty();
		}
		else
		{
			return CreateErrorResponse(FString::Printf(TEXT("Failed to load object: %s"), *DefaultValue));
		}
	}
	else
	{
		TargetPin->DefaultValue = DefaultValue;
	}

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("pin_name"), PinName);
	ResultData->SetStringField(TEXT("default_value"), DefaultValue);
	return CreateSuccessResponse(ResultData);
}


