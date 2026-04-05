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
// Spawn Actor Nodes
// ============================================================================

bool FAddSpawnActorFromClassNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ClassToSpawn;
	if (!GetRequiredString(Params, TEXT("class_to_spawn"), ClassToSpawn, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddSpawnActorFromClassNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ClassToSpawn = Params->GetStringField(TEXT("class_to_spawn"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the class to spawn
	UClass* SpawnClass = nullptr;

	// First, try to find as a blueprint
	UBlueprint* SpawnBP = FMCPCommonUtils::FindBlueprint(ClassToSpawn);
	if (SpawnBP && SpawnBP->GeneratedClass)
	{
		SpawnClass = SpawnBP->GeneratedClass;
	}

	// Try content path
	if (!SpawnClass && ClassToSpawn.StartsWith(TEXT("/Game/")))
	{
		FString BPPath = ClassToSpawn;
		if (!BPPath.EndsWith(TEXT("_C")))
		{
			BPPath += TEXT("_C");
		}
		SpawnClass = LoadClass<AActor>(nullptr, *BPPath);
		if (!SpawnClass)
		{
			SpawnClass = LoadClass<AActor>(nullptr, *ClassToSpawn);
		}
	}

	// Try engine classes
	if (!SpawnClass)
	{
		static const FString ModulePaths[] = {
			TEXT("/Script/Engine"),
			TEXT("/Script/CoreUObject")
		};
		for (const FString& ModulePath : ModulePaths)
		{
			FString FullPath = FString::Printf(TEXT("%s.%s"), *ModulePath, *ClassToSpawn);
			SpawnClass = LoadClass<AActor>(nullptr, *FullPath);
			if (SpawnClass) break;
		}
	}

	if (!SpawnClass)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Class to spawn not found: %s"), *ClassToSpawn));
	}

	UK2Node_SpawnActorFromClass* SpawnNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_SpawnActorFromClass>(
		TargetGraph, Position, EK2NewNodeFlags::None
	);

	// Set the class to spawn (must be done after pins are allocated)
	UEdGraphPin* ClassPin = SpawnNode->GetClassPin();
	if (ClassPin)
	{
		const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(TargetGraph->GetSchema());
		if (K2Schema)
		{
			K2Schema->TrySetDefaultObject(*ClassPin, SpawnClass);
		}
	}
	SpawnNode->ReconstructNode();

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(SpawnNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), SpawnNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("class_to_spawn"), SpawnClass->GetName());
	return CreateSuccessResponse(ResultData);
}


bool FCallBlueprintFunctionAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString TargetBlueprint, FunctionName;
	if (!GetRequiredString(Params, TEXT("target_blueprint"), TargetBlueprint, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("function_name"), FunctionName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FCallBlueprintFunctionAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString TargetBlueprintName = Params->GetStringField(TEXT("target_blueprint"));
	FString FunctionName = Params->GetStringField(TEXT("function_name"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the target blueprint
	UBlueprint* TargetBlueprint = FMCPCommonUtils::FindBlueprint(TargetBlueprintName);
	if (!TargetBlueprint)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Target blueprint not found: %s"), *TargetBlueprintName));
	}

	// Ensure target is compiled
	if (!TargetBlueprint->GeneratedClass)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Target blueprint not compiled: %s"), *TargetBlueprintName));
	}

	// Find the function
	UFunction* Function = TargetBlueprint->GeneratedClass->FindFunctionByName(*FunctionName);
	if (!Function)
	{
		// Check if graph exists
		bool bFoundGraph = false;
		for (UEdGraph* Graph : TargetBlueprint->FunctionGraphs)
		{
			if (Graph && Graph->GetFName() == FName(*FunctionName))
			{
				bFoundGraph = true;
				break;
			}
		}

		if (bFoundGraph)
		{
			return CreateErrorResponse(FString::Printf(
				TEXT("Function '%s' exists but is not compiled. Compile '%s' first."),
				*FunctionName, *TargetBlueprintName));
		}
		else
		{
			// List available functions
			TArray<FString> AvailableFunctions;
			for (TFieldIterator<UFunction> FuncIt(TargetBlueprint->GeneratedClass); FuncIt; ++FuncIt)
			{
				if ((*FuncIt)->HasAnyFunctionFlags(FUNC_BlueprintCallable))
				{
					AvailableFunctions.Add((*FuncIt)->GetName());
				}
			}
			FString AvailableStr = FString::Join(AvailableFunctions, TEXT(", "));
			return CreateErrorResponse(FString::Printf(
				TEXT("Function '%s' not found in '%s'. Available: %s"),
				*FunctionName, *TargetBlueprintName, *AvailableStr));
		}
	}

	UClass* TargetGenClass = TargetBlueprint->GeneratedClass;
	bool bIsSelfCall = (Blueprint == TargetBlueprint);
	UK2Node_CallFunction* FunctionNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CallFunction>(
		TargetGraph, Position, EK2NewNodeFlags::None,
		[Function, TargetGenClass, bIsSelfCall](UK2Node_CallFunction* Node) {
			if (bIsSelfCall)
			{
				Node->FunctionReference.SetSelfMember(Function->GetFName());
			}
			else
			{
				Node->FunctionReference.SetExternalMember(Function->GetFName(), TargetGenClass);
			}
		}
	);

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(FunctionNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), FunctionNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("function_name"), FunctionName);
	ResultData->SetStringField(TEXT("target_blueprint"), TargetBlueprintName);
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// External Object Property Nodes
// ============================================================================

bool FSetObjectPropertyAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString PropertyName, OwnerClass;
	if (!GetRequiredString(Params, TEXT("property_name"), PropertyName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("owner_class"), OwnerClass, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FSetObjectPropertyAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString PropertyName = Params->GetStringField(TEXT("property_name"));
	FString OwnerClassName = Params->GetStringField(TEXT("owner_class"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the owner class - try /Script/Engine first (most common), then blueprint
	UClass* OwnerClass = LoadClass<UObject>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *OwnerClassName));
	if (!OwnerClass)
	{
		UBlueprint* OwnerBP = FMCPCommonUtils::FindBlueprint(OwnerClassName);
		if (OwnerBP) OwnerClass = OwnerBP->GeneratedClass;
	}

	if (!OwnerClass)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Class not found: %s"), *OwnerClassName));
	}

	// Verify property exists
	FProperty* Property = OwnerClass->FindPropertyByName(FName(*PropertyName));
	if (!Property)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Property '%s' not found on '%s'"), *PropertyName, *OwnerClassName));
	}

	// Create Set node with external member reference
	UK2Node_VariableSet* VarSetNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableSet>(
		TargetGraph, Position, EK2NewNodeFlags::None,
		[&PropertyName, OwnerClass](UK2Node_VariableSet* Node) {
			Node->VariableReference.SetExternalMember(FName(*PropertyName), OwnerClass);
		}
	);
	VarSetNode->ReconstructNode();

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(VarSetNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), VarSetNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("property_name"), PropertyName);
	ResultData->SetStringField(TEXT("owner_class"), OwnerClass->GetName());
	return CreateSuccessResponse(ResultData);
}


