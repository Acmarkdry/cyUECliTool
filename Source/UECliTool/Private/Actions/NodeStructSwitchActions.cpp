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
// Switch Nodes
// ============================================================================

bool FAddSwitchOnStringNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddSwitchOnStringNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	UK2Node_SwitchString* SwitchNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_SwitchString>(
		TargetGraph, Position, EK2NewNodeFlags::None,
		[](UK2Node_SwitchString* Node) {}
	);

	if (!SwitchNode)
	{
		return CreateErrorResponse(TEXT("Failed to create Switch on String node"));
	}

	// Add cases if provided
	const TArray<TSharedPtr<FJsonValue>>* CasesArray = nullptr;
	if (Params->TryGetArrayField(TEXT("cases"), CasesArray))
	{
		for (const TSharedPtr<FJsonValue>& CaseValue : *CasesArray)
		{
			FString CaseString;
			if (CaseValue->TryGetString(CaseString))
			{
				SwitchNode->PinNames.Add(FName(*CaseString));
			}
		}
		// Reconstruct node to add the pins
		SwitchNode->ReconstructNode();
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(SwitchNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), SwitchNode->NodeGuid.ToString());

	// Return available output pins
	TArray<TSharedPtr<FJsonValue>> PinsArray;
	for (UEdGraphPin* Pin : SwitchNode->Pins)
	{
		if (Pin->Direction == EGPD_Output && !Pin->bHidden)
		{
			PinsArray.Add(MakeShared<FJsonValueString>(Pin->PinName.ToString()));
		}
	}
	ResultData->SetArrayField(TEXT("output_pins"), PinsArray);

	return CreateSuccessResponse(ResultData);
}


bool FAddSwitchOnIntNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddSwitchOnIntNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	UK2Node_SwitchInteger* SwitchNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_SwitchInteger>(
		TargetGraph, Position, EK2NewNodeFlags::None,
		[](UK2Node_SwitchInteger* Node) {}
	);

	if (!SwitchNode)
	{
		return CreateErrorResponse(TEXT("Failed to create Switch on Int node"));
	}

	// Set start index if provided
	int32 StartIdx = 0;
	if (Params->TryGetNumberField(TEXT("start_index"), StartIdx))
	{
		SwitchNode->StartIndex = StartIdx;
	}

	// Add cases by calling AddPinToSwitchNode for each case
	const TArray<TSharedPtr<FJsonValue>>* CasesArray = nullptr;
	if (Params->TryGetArrayField(TEXT("cases"), CasesArray))
	{
		for (int32 i = 0; i < CasesArray->Num(); ++i)
		{
			SwitchNode->AddPinToSwitchNode();
		}
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(SwitchNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), SwitchNode->NodeGuid.ToString());

	TArray<TSharedPtr<FJsonValue>> PinsArray;
	for (UEdGraphPin* Pin : SwitchNode->Pins)
	{
		if (Pin->Direction == EGPD_Output && !Pin->bHidden)
		{
			PinsArray.Add(MakeShared<FJsonValueString>(Pin->PinName.ToString()));
		}
	}
	ResultData->SetArrayField(TEXT("output_pins"), PinsArray);

	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Local Variables
// ============================================================================

bool FAddFunctionLocalVariableAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString FunctionName, VariableName, VariableType;
	if (!GetRequiredString(Params, TEXT("function_name"), FunctionName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("variable_name"), VariableName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("variable_type"), VariableType, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddFunctionLocalVariableAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString FunctionName = Params->GetStringField(TEXT("function_name"));
	FString VariableName = Params->GetStringField(TEXT("variable_name"));
	FString VariableType = Params->GetStringField(TEXT("variable_type"));
	FString DefaultValue = GetOptionalString(Params, TEXT("default_value"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Find the function graph
	UEdGraph* FunctionGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph->GetFName() == FName(*FunctionName))
		{
			FunctionGraph = Graph;
			break;
		}
	}
	if (!FunctionGraph)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Function '%s' not found in Blueprint"), *FunctionName));
	}

	// Find the FunctionEntry node
	UK2Node_FunctionEntry* EntryNode = nullptr;
	for (UEdGraphNode* Node : FunctionGraph->Nodes)
	{
		EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		if (EntryNode) break;
	}
	if (!EntryNode)
	{
		return CreateErrorResponse(TEXT("Function entry node not found"));
	}

	// Resolve pin type
	FEdGraphPinType PinType;
	FString TypeResolveError;
	if (!FMCPCommonUtils::ResolvePinTypeFromString(VariableType, PinType, TypeResolveError))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Function local variable '%s': %s"), *VariableName, *TypeResolveError));
	}

	// Add local variable via entry node's LocalVariables array
	FBPVariableDescription NewVar;
	NewVar.VarName = FName(*VariableName);
	NewVar.VarGuid = FGuid::NewGuid();
	NewVar.VarType = PinType;
	NewVar.PropertyFlags |= CPF_BlueprintVisible;
	NewVar.FriendlyName = FName::NameToDisplayString(VariableName, PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean);
	NewVar.Category = UEdGraphSchema_K2::VR_DefaultCategory;
	if (!DefaultValue.IsEmpty())
	{
		NewVar.DefaultValue = DefaultValue;
	}

	EntryNode->LocalVariables.Add(NewVar);
	EntryNode->ReconstructNode();
	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("variable_name"), VariableName);
	ResultData->SetStringField(TEXT("variable_type"), VariableType);
	ResultData->SetStringField(TEXT("function_name"), FunctionName);
	if (!DefaultValue.IsEmpty())
	{
		ResultData->SetStringField(TEXT("default_value"), DefaultValue);
	}
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Variable Default Values
// ============================================================================

bool FSetBlueprintVariableDefaultAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString VariableName, DefaultValue;
	if (!GetRequiredString(Params, TEXT("variable_name"), VariableName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("default_value"), DefaultValue, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FSetBlueprintVariableDefaultAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString VariableName = Params->GetStringField(TEXT("variable_name"));
	FString DefaultValue = Params->GetStringField(TEXT("default_value"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	bool bFound = false;
	for (FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarName == FName(*VariableName))
		{
			Variable.DefaultValue = DefaultValue;
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Variable '%s' not found in Blueprint"), *VariableName));
	}

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("variable_name"), VariableName);
	ResultData->SetStringField(TEXT("default_value"), DefaultValue);
	return CreateSuccessResponse(ResultData);
}


