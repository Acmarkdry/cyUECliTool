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
// P1 — Variable & Function Management
// ============================================================================

bool FDeleteBlueprintVariableAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString VariableName;
	if (!GetRequiredString(Params, TEXT("variable_name"), VariableName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FDeleteBlueprintVariableAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString VariableName = Params->GetStringField(TEXT("variable_name"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Check variable exists
	bool bFound = false;
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarName == FName(*VariableName))
		{
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Variable '%s' not found in Blueprint"), *VariableName));
	}

	// Remove the variable using the editor utility
	FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, FName(*VariableName));
	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("deleted_variable"), VariableName);
	return CreateSuccessResponse(ResultData);
}


bool FRenameBlueprintVariableAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString OldName, NewName;
	if (!GetRequiredString(Params, TEXT("old_name"), OldName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("new_name"), NewName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FRenameBlueprintVariableAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString OldName = Params->GetStringField(TEXT("old_name"));
	FString NewName = Params->GetStringField(TEXT("new_name"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Check old variable exists
	bool bFound = false;
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarName == FName(*OldName))
		{
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Variable '%s' not found in Blueprint"), *OldName));
	}

	// Check new name not already taken
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarName == FName(*NewName))
		{
			return CreateErrorResponse(FString::Printf(TEXT("Variable '%s' already exists in Blueprint"), *NewName));
		}
	}

	FBlueprintEditorUtils::RenameMemberVariable(Blueprint, FName(*OldName), FName(*NewName));
	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("old_name"), OldName);
	ResultData->SetStringField(TEXT("new_name"), NewName);
	return CreateSuccessResponse(ResultData);
}


bool FSetVariableMetadataAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString VariableName;
	if (!GetRequiredString(Params, TEXT("variable_name"), VariableName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FSetVariableMetadataAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString VariableName = Params->GetStringField(TEXT("variable_name"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Find the variable
	FBPVariableDescription* TargetVar = nullptr;
	for (FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarName == FName(*VariableName))
		{
			TargetVar = &Variable;
			break;
		}
	}

	if (!TargetVar)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Variable '%s' not found in Blueprint"), *VariableName));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("variable_name"), VariableName);

	// Category
	FString Category = GetOptionalString(Params, TEXT("category"));
	if (!Category.IsEmpty())
	{
		FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, FName(*VariableName), nullptr, FText::FromString(Category));
		ResultData->SetStringField(TEXT("category"), Category);
	}

	// Tooltip
	FString Tooltip = GetOptionalString(Params, TEXT("tooltip"));
	if (!Tooltip.IsEmpty())
	{
		TargetVar->SetMetaData(FBlueprintMetadata::MD_Tooltip, *Tooltip);
		ResultData->SetStringField(TEXT("tooltip"), Tooltip);
	}

	// Instance Editable (expose to editor details)
	// Use UE official API: SetBlueprintOnlyEditableFlag handles CPF_DisableEditOnInstance
	// AND calls MarkBlueprintAsStructurallyModified internally, which is required
	// for the Blueprint editor UI (eye icon) to refresh.
	bool bDidChangeInstanceEditable = false;
	if (Params->HasField(TEXT("instance_editable")))
	{
		bool bEditable = Params->GetBoolField(TEXT("instance_editable"));
		if (bEditable)
		{
			TargetVar->PropertyFlags |= CPF_Edit;
		}
		else
		{
			TargetVar->PropertyFlags &= ~CPF_Edit;
		}
		// bNewBlueprintOnly = !bEditable: true means "blueprint only" (DisableEditOnInstance)
		FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, FName(*VariableName), !bEditable);
		bDidChangeInstanceEditable = true;
		ResultData->SetBoolField(TEXT("instance_editable"), bEditable);
	}

	// Blueprint Read Only
	if (Params->HasField(TEXT("blueprint_read_only")))
	{
		bool bReadOnly = Params->GetBoolField(TEXT("blueprint_read_only"));
		if (bReadOnly)
		{
			TargetVar->PropertyFlags |= CPF_BlueprintReadOnly;
			TargetVar->PropertyFlags &= ~CPF_BlueprintVisible;
		}
		else
		{
			TargetVar->PropertyFlags &= ~CPF_BlueprintReadOnly;
		}
		ResultData->SetBoolField(TEXT("blueprint_read_only"), bReadOnly);
	}

	// Expose on Spawn
	if (Params->HasField(TEXT("expose_on_spawn")))
	{
		bool bExposeOnSpawn = Params->GetBoolField(TEXT("expose_on_spawn"));
		if (bExposeOnSpawn)
		{
			TargetVar->PropertyFlags |= CPF_ExposeOnSpawn;
		}
		else
		{
			TargetVar->PropertyFlags &= ~CPF_ExposeOnSpawn;
		}
		ResultData->SetBoolField(TEXT("expose_on_spawn"), bExposeOnSpawn);
	}

	// Replicated
	if (Params->HasField(TEXT("replicated")))
	{
		bool bReplicated = Params->GetBoolField(TEXT("replicated"));
		if (bReplicated)
		{
			TargetVar->PropertyFlags |= CPF_Net;
			TargetVar->RepNotifyFunc = NAME_None;
		}
		else
		{
			TargetVar->PropertyFlags &= ~CPF_Net;
		}
		ResultData->SetBoolField(TEXT("replicated"), bReplicated);
	}

	// Private (visible only within this Blueprint)
	if (Params->HasField(TEXT("private")))
	{
		bool bPrivate = Params->GetBoolField(TEXT("private"));
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, FName(*VariableName), nullptr,
			FBlueprintMetadata::MD_Private, bPrivate ? TEXT("true") : TEXT("false"));
		ResultData->SetBoolField(TEXT("private"), bPrivate);
	}

	// If instance_editable was changed, SetBlueprintOnlyEditableFlag already
	// called MarkBlueprintAsStructurallyModified (skeleton rebuild + UI refresh).
	// For other flags, use the standard MarkBlueprintModified path.
	if (!bDidChangeInstanceEditable)
	{
		MarkBlueprintModified(Blueprint, Context);
	}
	else
	{
		// Still need to mark the package dirty for auto-save
		Context.MarkPackageDirty(Blueprint->GetOutermost());
	}

	// Force a full recompile so that flag changes
	// (CPF_Edit, CPF_ExposeOnSpawn, etc.) are reflected in the Generated Class.
	FString CompileError;
	bool bCompileOk = CompileBlueprint(Blueprint, CompileError);
	ResultData->SetBoolField(TEXT("compiled"), bCompileOk);
	if (!bCompileOk)
	{
		ResultData->SetStringField(TEXT("compile_error"), CompileError);
	}

	return CreateSuccessResponse(ResultData);
}


bool FDeleteBlueprintFunctionAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString FunctionName;
	if (!GetRequiredString(Params, TEXT("function_name"), FunctionName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FDeleteBlueprintFunctionAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString FunctionName = Params->GetStringField(TEXT("function_name"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Find the function graph
	UEdGraph* FunctionGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FName(*FunctionName))
		{
			FunctionGraph = Graph;
			break;
		}
	}

	if (!FunctionGraph)
	{
		// List available functions for helpful error
		TArray<FString> AvailableFunctions;
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph)
			{
				AvailableFunctions.Add(Graph->GetName());
			}
		}
		FString AvailableStr = FString::Join(AvailableFunctions, TEXT(", "));
		return CreateErrorResponse(FString::Printf(TEXT("Function '%s' not found. Available: %s"), *FunctionName, *AvailableStr));
	}

	FBlueprintEditorUtils::RemoveGraph(Blueprint, FunctionGraph, EGraphRemoveFlags::Default);
	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("deleted_function"), FunctionName);
	return CreateSuccessResponse(ResultData);
}

bool FRenameBlueprintFunctionAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString FunctionName;
	FString NewName;
	if (!GetRequiredString(Params, TEXT("function_name"), FunctionName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("new_name"), NewName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FRenameBlueprintFunctionAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	const FString FunctionName = Params->GetStringField(TEXT("function_name"));
	const FString NewName = Params->GetStringField(TEXT("new_name"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	UEdGraph* FunctionGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FName(*FunctionName))
		{
			FunctionGraph = Graph;
			break;
		}
	}

	if (!FunctionGraph)
	{
		TArray<FString> AvailableFunctions;
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph)
			{
				AvailableFunctions.Add(Graph->GetName());
			}
		}
		const FString AvailableStr = FString::Join(AvailableFunctions, TEXT(", "));
		return CreateErrorResponse(FString::Printf(TEXT("Function '%s' not found. Available: %s"), *FunctionName, *AvailableStr));
	}

	if (FunctionName.Equals(NewName, ESearchCase::CaseSensitive))
	{
		TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
		ResultData->SetBoolField(TEXT("already_named"), true);
		ResultData->SetStringField(TEXT("function_name"), FunctionName);
		ResultData->SetStringField(TEXT("new_name"), NewName);
		return CreateSuccessResponse(ResultData);
	}

	const FName OldFName(*FunctionName);

	// Pre-validate: ensure new name won't collide with an existing graph
	UEdGraph* ExistingGraph = FindObject<UEdGraph>(FunctionGraph->GetOuter(), *NewName);
	if (ExistingGraph && ExistingGraph != FunctionGraph)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("A graph named '%s' already exists in this Blueprint."), *NewName),
			TEXT("name_collision"));
	}

	// Use the engine's own RenameGraph — it handles:
	// 1. UObject::Rename on the graph
	// 2. FunctionEntry/FunctionResult FunctionReference updates
	// 3. All K2Node_CallFunction call site updates (via TObjectIterator)
	// 4. Local variable scope updates
	// 5. ReplaceFunctionReferences for cross-BP refs
	// 6. NotifyGraphRenamed + MarkBlueprintAsStructurallyModified
	FBlueprintEditorUtils::RenameGraph(FunctionGraph, NewName);

	const FString RenamedTo = FunctionGraph->GetName();

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("old_name"), FunctionName);
	ResultData->SetStringField(TEXT("new_name"), RenamedTo);
	ResultData->SetBoolField(TEXT("exact_match"), RenamedTo.Equals(NewName, ESearchCase::CaseSensitive));
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// FRenameBlueprintMacroAction — macro.rename
// ============================================================================

bool FRenameBlueprintMacroAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MacroName;
	FString NewName;
	if (!GetRequiredString(Params, TEXT("macro_name"), MacroName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("new_name"), NewName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FRenameBlueprintMacroAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	const FString MacroName = Params->GetStringField(TEXT("macro_name"));
	const FString NewName = Params->GetStringField(TEXT("new_name"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	UEdGraph* MacroGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (Graph && Graph->GetFName() == FName(*MacroName))
		{
			MacroGraph = Graph;
			break;
		}
	}

	if (!MacroGraph)
	{
		TArray<FString> AvailableMacros;
		for (UEdGraph* Graph : Blueprint->MacroGraphs)
		{
			if (Graph)
			{
				AvailableMacros.Add(Graph->GetName());
			}
		}
		const FString AvailableStr = FString::Join(AvailableMacros, TEXT(", "));
		return CreateErrorResponse(FString::Printf(TEXT("Macro '%s' not found. Available: %s"), *MacroName, *AvailableStr));
	}

	if (MacroName.Equals(NewName, ESearchCase::CaseSensitive))
	{
		TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
		ResultData->SetBoolField(TEXT("already_named"), true);
		ResultData->SetStringField(TEXT("macro_name"), MacroName);
		ResultData->SetStringField(TEXT("new_name"), NewName);
		return CreateSuccessResponse(ResultData);
	}

	// Pre-validate: ensure new name won't collide with an existing graph
	UEdGraph* ExistingGraph = FindObject<UEdGraph>(MacroGraph->GetOuter(), *NewName);
	if (ExistingGraph && ExistingGraph != MacroGraph)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("A graph named '%s' already exists in this Blueprint."), *NewName),
			TEXT("name_collision"));
	}

	// Use the engine's own RenameGraph — it handles:
	// 1. UObject::Rename on the graph
	// 2. Macro instance node reference updates
	// 3. NotifyGraphRenamed + MarkBlueprintAsStructurallyModified
	FBlueprintEditorUtils::RenameGraph(MacroGraph, NewName);

	const FString RenamedTo = MacroGraph->GetName();

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("old_name"), MacroName);
	ResultData->SetStringField(TEXT("new_name"), RenamedTo);
	ResultData->SetBoolField(TEXT("exact_match"), RenamedTo.Equals(NewName, ESearchCase::CaseSensitive));
	return CreateSuccessResponse(ResultData);
}


