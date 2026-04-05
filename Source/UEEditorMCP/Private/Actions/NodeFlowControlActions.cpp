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
// Sequence Node
// ============================================================================

bool FAddSequenceNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddSequenceNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	UK2Node_ExecutionSequence* SeqNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_ExecutionSequence>(
		TargetGraph, Position, EK2NewNodeFlags::None
	);

	if (!SeqNode)
	{
		return CreateErrorResponse(TEXT("Failed to create Sequence node"));
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(SeqNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), SeqNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Macro Instance Nodes
// ============================================================================

UEdGraph* FAddMacroInstanceNodeAction::FindMacroGraph(const FString& MacroName) const
{
	UBlueprint* MacroBP = LoadObject<UBlueprint>(nullptr,
		TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
	if (!MacroBP) return nullptr;

	for (UEdGraph* Graph : MacroBP->MacroGraphs)
	{
		if (Graph && Graph->GetFName().ToString().Equals(MacroName, ESearchCase::IgnoreCase))
			return Graph;
	}
	return nullptr;
}

bool FAddMacroInstanceNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MacroName;
	if (!GetRequiredString(Params, TEXT("macro_name"), MacroName, OutError)) return false;
	if (!FindMacroGraph(MacroName))
	{
		OutError = FString::Printf(TEXT("Macro '%s' not found in StandardMacros"), *MacroName);
		return false;
	}
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddMacroInstanceNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString MacroName = Params->GetStringField(TEXT("macro_name"));
	UEdGraph* MacroGraph = FindMacroGraph(MacroName);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	UK2Node_MacroInstance* MacroNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_MacroInstance>(
		TargetGraph, GetNodePosition(Params), EK2NewNodeFlags::None,
		[MacroGraph](UK2Node_MacroInstance* Node) { Node->SetMacroGraph(MacroGraph); }
	);

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(MacroNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), MacroNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Struct Nodes
// ============================================================================

bool FAddMakeStructNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString StructType;
	if (!GetRequiredString(Params, TEXT("struct_type"), StructType, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddMakeStructNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString StructType = Params->GetStringField(TEXT("struct_type"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Resolve struct type - try common structs first
	UScriptStruct* ScriptStruct = nullptr;
	FString StructTypeLower = StructType.ToLower();

	// Common struct mappings
	if (StructTypeLower == TEXT("intpoint") || StructTypeLower == TEXT("fintpoint"))
	{
		ScriptStruct = TBaseStructure<FIntPoint>::Get();
	}
	else if (StructTypeLower == TEXT("vector") || StructTypeLower == TEXT("fvector"))
	{
		ScriptStruct = TBaseStructure<FVector>::Get();
	}
	else if (StructTypeLower == TEXT("vector2d") || StructTypeLower == TEXT("fvector2d"))
	{
		ScriptStruct = TBaseStructure<FVector2D>::Get();
	}
	else if (StructTypeLower == TEXT("rotator") || StructTypeLower == TEXT("frotator"))
	{
		ScriptStruct = TBaseStructure<FRotator>::Get();
	}
	else if (StructTypeLower == TEXT("transform") || StructTypeLower == TEXT("ftransform"))
	{
		ScriptStruct = TBaseStructure<FTransform>::Get();
	}
	else if (StructTypeLower == TEXT("linearcolor") || StructTypeLower == TEXT("flinearcolor"))
	{
		ScriptStruct = TBaseStructure<FLinearColor>::Get();
	}
	else if (StructTypeLower == TEXT("color") || StructTypeLower == TEXT("fcolor"))
	{
		ScriptStruct = TBaseStructure<FColor>::Get();
	}
	else
	{
		// Try to find by name
		FString FullStructName = StructType;
		if (!FullStructName.StartsWith(TEXT("F")))
		{
			FullStructName = TEXT("F") + FullStructName;
		}
		ScriptStruct = FindObject<UScriptStruct>(nullptr, *FString::Printf(TEXT("/Script/CoreUObject.%s"), *FullStructName));
		if (!ScriptStruct)
		{
			ScriptStruct = FindObject<UScriptStruct>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *FullStructName));
		}
	}

	if (!ScriptStruct)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Struct type not found: %s"), *StructType));
	}

	UK2Node_MakeStruct* MakeStructNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_MakeStruct>(
		TargetGraph, Position, EK2NewNodeFlags::None,
		[ScriptStruct](UK2Node_MakeStruct* Node) { Node->StructType = ScriptStruct; }
	);

	if (!MakeStructNode)
	{
		return CreateErrorResponse(TEXT("Failed to create Make Struct node"));
	}

	// Set pin defaults if provided
	const TSharedPtr<FJsonObject>* PinDefaults = nullptr;
	if (Params->TryGetObjectField(TEXT("pin_defaults"), PinDefaults))
	{
		for (const auto& Pair : (*PinDefaults)->Values)
		{
			FString Value;
			if (Pair.Value->TryGetString(Value))
			{
				UEdGraphPin* Pin = FMCPCommonUtils::FindPin(MakeStructNode, Pair.Key, EGPD_Input);
				if (Pin)
				{
					Pin->DefaultValue = Value;
				}
			}
		}
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(MakeStructNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), MakeStructNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("struct_type"), ScriptStruct->GetName());
	return CreateSuccessResponse(ResultData);
}


bool FAddBreakStructNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString StructType;
	if (!GetRequiredString(Params, TEXT("struct_type"), StructType, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBreakStructNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString StructType = Params->GetStringField(TEXT("struct_type"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Resolve struct type - same logic as MakeStruct
	UScriptStruct* ScriptStruct = nullptr;
	FString StructTypeLower = StructType.ToLower();

	if (StructTypeLower == TEXT("intpoint") || StructTypeLower == TEXT("fintpoint"))
	{
		ScriptStruct = TBaseStructure<FIntPoint>::Get();
	}
	else if (StructTypeLower == TEXT("vector") || StructTypeLower == TEXT("fvector"))
	{
		ScriptStruct = TBaseStructure<FVector>::Get();
	}
	else if (StructTypeLower == TEXT("vector2d") || StructTypeLower == TEXT("fvector2d"))
	{
		ScriptStruct = TBaseStructure<FVector2D>::Get();
	}
	else if (StructTypeLower == TEXT("rotator") || StructTypeLower == TEXT("frotator"))
	{
		ScriptStruct = TBaseStructure<FRotator>::Get();
	}
	else if (StructTypeLower == TEXT("transform") || StructTypeLower == TEXT("ftransform"))
	{
		ScriptStruct = TBaseStructure<FTransform>::Get();
	}
	else if (StructTypeLower == TEXT("linearcolor") || StructTypeLower == TEXT("flinearcolor"))
	{
		ScriptStruct = TBaseStructure<FLinearColor>::Get();
	}
	else if (StructTypeLower == TEXT("color") || StructTypeLower == TEXT("fcolor"))
	{
		ScriptStruct = TBaseStructure<FColor>::Get();
	}
	else
	{
		FString FullStructName = StructType;
		if (!FullStructName.StartsWith(TEXT("F")))
		{
			FullStructName = TEXT("F") + FullStructName;
		}
		ScriptStruct = FindObject<UScriptStruct>(nullptr, *FString::Printf(TEXT("/Script/CoreUObject.%s"), *FullStructName));
		if (!ScriptStruct)
		{
			ScriptStruct = FindObject<UScriptStruct>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *FullStructName));
		}
	}

	if (!ScriptStruct)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Struct type not found: %s"), *StructType));
	}

	UK2Node_BreakStruct* BreakStructNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_BreakStruct>(
		TargetGraph, Position, EK2NewNodeFlags::None,
		[ScriptStruct](UK2Node_BreakStruct* Node) { Node->StructType = ScriptStruct; }
	);

	if (!BreakStructNode)
	{
		return CreateErrorResponse(TEXT("Failed to create Break Struct node"));
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(BreakStructNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), BreakStructNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("struct_type"), ScriptStruct->GetName());
	return CreateSuccessResponse(ResultData);
}


