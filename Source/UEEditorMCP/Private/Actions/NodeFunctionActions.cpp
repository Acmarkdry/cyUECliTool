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
// Function Nodes
// ============================================================================

bool FAddBlueprintFunctionNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Target, FunctionName;
	if (!GetRequiredString(Params, TEXT("target"), Target, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("function_name"), FunctionName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintFunctionNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Target = Params->GetStringField(TEXT("target"));
	FString FunctionName = Params->GetStringField(TEXT("function_name"));
	FVector2D Position = GetNodePosition(Params);

	// Optional extra params payload (JSON string)
	TSharedPtr<FJsonObject> ExtraParams;
	if (Params->HasField(TEXT("params")))
	{
		const FString ParamsStr = Params->GetStringField(TEXT("params"));
		if (!ParamsStr.IsEmpty())
		{
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ParamsStr);
			TSharedPtr<FJsonObject> Parsed;
			if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
			{
				ExtraParams = Parsed;
			}
		}
	}

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the function
	UFunction* Function = nullptr;
	UClass* TargetClass = nullptr;

	// Direct mapping for known library classes
	FString TargetLower = Target.ToLower();
	if (TargetLower.Contains(TEXT("kismetmathlibrary")) || TargetLower.Contains(TEXT("math")))
	{
		TargetClass = UKismetMathLibrary::StaticClass();
	}
	else if (TargetLower.Contains(TEXT("kismetsystemlibrary")) || TargetLower.Contains(TEXT("systemlibrary")))
	{
		TargetClass = UKismetSystemLibrary::StaticClass();
	}
	else if (TargetLower.Contains(TEXT("gameplaystatics")))
	{
		TargetClass = UGameplayStatics::StaticClass();
	}
	else if (TargetLower.Contains(TEXT("enhancedinputlocalplayersubsystem")) || TargetLower.Contains(TEXT("inputsubsystem")))
	{
		TargetClass = UEnhancedInputLocalPlayerSubsystem::StaticClass();
	}
	else if (TargetLower.Equals(TEXT("kismetstringlibrary"), ESearchCase::IgnoreCase)
		|| TargetLower.Equals(TEXT("ukismetstringlibrary"), ESearchCase::IgnoreCase)
		|| TargetLower.Equals(TEXT("stringlibrary"), ESearchCase::IgnoreCase))
	{
		TargetClass = UKismetStringLibrary::StaticClass();
	}
	else if (TargetLower.Contains(TEXT("widgetblueprintlibrary")) || TargetLower.Contains(TEXT("widgetlibrary")))
	{
		TargetClass = UWidgetBlueprintLibrary::StaticClass();
	}
	else if (TargetLower.Contains(TEXT("gameusersettings")) || TargetLower.Contains(TEXT("usersettings")))
	{
		TargetClass = UGameUserSettings::StaticClass();
	}

	// If not a known class, try module paths
	if (!TargetClass)
	{
		// Fix: If target is already a full /Script/ path, try loading directly first
		if (Target.StartsWith(TEXT("/Script/")))
		{
			TargetClass = LoadClass<UObject>(nullptr, *Target);
		}

		// Try candidates with module path prefixes
		if (!TargetClass)
		{
			TArray<FString> CandidateNames;
			CandidateNames.Add(Target);
			if (!Target.StartsWith(TEXT("U")) && !Target.StartsWith(TEXT("/")))
			{
				CandidateNames.Add(TEXT("U") + Target);
			}

			static const FString ModulePaths[] = {
				TEXT("/Script/Engine"),
				TEXT("/Script/CoreUObject"),
				TEXT("/Script/UMG"),
			};

			for (const FString& Candidate : CandidateNames)
			{
				if (TargetClass) break;
				for (const FString& ModulePath : ModulePaths)
				{
					FString FullPath = FString::Printf(TEXT("%s.%s"), *ModulePath, *Candidate);
					TargetClass = LoadClass<UObject>(nullptr, *FullPath);
					if (TargetClass) break;
				}
			}
		}

		// Dynamic fallback: scan all loaded UClass objects for a matching name.
		// This handles project-specific classes (e.g., UPlayerStatusBlueprintLibrary)
		// without requiring the caller to know the full /Script/ path.
		if (!TargetClass)
		{
			FString ClassNameOnly = Target;
			// Strip module path prefix if present
			if (ClassNameOnly.Contains(TEXT(".")))
			{
				ClassNameOnly = ClassNameOnly.RightChop(ClassNameOnly.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd) + 1);
			}

			TArray<FString> NameVariants;
			NameVariants.Add(ClassNameOnly);
			if (!ClassNameOnly.StartsWith(TEXT("U")))
			{
				NameVariants.Add(TEXT("U") + ClassNameOnly);
			}

			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (TargetClass) break;
				for (const FString& Variant : NameVariants)
				{
					if (It->GetName() == Variant)
					{
						TargetClass = *It;
						break;
					}
				}
			}
		}
	}

	// Look for function in target class
	if (TargetClass)
	{
		Function = TargetClass->FindFunctionByName(*FunctionName);
		// Try case-insensitive match
		if (!Function)
		{
			for (TFieldIterator<UFunction> FuncIt(TargetClass); FuncIt; ++FuncIt)
			{
				if ((*FuncIt)->GetName().Equals(FunctionName, ESearchCase::IgnoreCase))
				{
					Function = *FuncIt;
					break;
				}
			}
		}
		// Try with K2_ prefix (UE wraps many AActor/UObject methods with K2_ prefix)
		if (!Function)
		{
			FString K2FunctionName = TEXT("K2_") + FunctionName;
			Function = TargetClass->FindFunctionByName(*K2FunctionName);
			if (!Function)
			{
				for (TFieldIterator<UFunction> FuncIt(TargetClass); FuncIt; ++FuncIt)
				{
					if ((*FuncIt)->GetName().Equals(K2FunctionName, ESearchCase::IgnoreCase))
					{
						Function = *FuncIt;
						break;
					}
				}
			}
		}
	}

	// Fallback to blueprint class
	if (!Function)
	{
		Function = Blueprint->GeneratedClass->FindFunctionByName(*FunctionName);
		// Also try K2_ prefix on blueprint class
		if (!Function)
		{
			FString K2FunctionName = TEXT("K2_") + FunctionName;
			Function = Blueprint->GeneratedClass->FindFunctionByName(*K2FunctionName);
		}
	}

	// Fallback: search common static function libraries (covers target="self" cases
	// where the function actually lives in a library class like KismetSystemLibrary)
	if (!Function)
	{
		static const TCHAR* FallbackLibraryPaths[] = {
			TEXT("/Script/Engine.KismetSystemLibrary"),
			TEXT("/Script/Engine.GameplayStatics"),
			TEXT("/Script/Engine.KismetMathLibrary"),
			TEXT("/Script/Engine.KismetStringLibrary"),
			TEXT("/Script/Engine.KismetTextLibrary"),
			TEXT("/Script/Engine.KismetArrayLibrary"),
		};
		for (const TCHAR* LibPath : FallbackLibraryPaths)
		{
			UClass* LibClass = FindObject<UClass>(nullptr, LibPath);
			if (LibClass)
			{
				Function = LibClass->FindFunctionByName(*FunctionName);
				if (!Function)
				{
					FString K2FunctionName = TEXT("K2_") + FunctionName;
					Function = LibClass->FindFunctionByName(*K2FunctionName);
				}
				if (Function)
				{
					TargetClass = LibClass;
					break;
				}
			}
		}
	}

	if (!Function)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Function not found: %s in target %s"), *FunctionName, *Target));
	}

	// Special-case: Create Widget node with explicit widget class
	// Use reflection-based K2Node_CreateWidget creation so we don't depend on private headers.
	if (TargetClass == UWidgetBlueprintLibrary::StaticClass())
	{
		const bool bIsCreateWidgetCall = FunctionName.Equals(TEXT("Create"), ESearchCase::IgnoreCase)
			|| FunctionName.Equals(TEXT("CreateWidget"), ESearchCase::IgnoreCase)
			|| Function->GetName().Equals(TEXT("Create"), ESearchCase::IgnoreCase)
			|| Function->GetName().Equals(TEXT("CreateWidget"), ESearchCase::IgnoreCase);

		if (bIsCreateWidgetCall && ExtraParams.IsValid()
			&& (ExtraParams->HasField(TEXT("widget_blueprint")) || ExtraParams->HasField(TEXT("widget_class_path"))))
		{
			UClass* WidgetClass = nullptr;

			if (ExtraParams->HasField(TEXT("widget_blueprint")))
			{
				const FString WidgetBPName = ExtraParams->GetStringField(TEXT("widget_blueprint"));
				UBlueprint* WidgetBP = FMCPCommonUtils::FindBlueprint(WidgetBPName);
				if (WidgetBP && WidgetBP->GeneratedClass)
				{
					WidgetClass = WidgetBP->GeneratedClass;
				}
			}

			if (!WidgetClass && ExtraParams->HasField(TEXT("widget_class_path")))
			{
				const FString ClassPath = ExtraParams->GetStringField(TEXT("widget_class_path"));
				WidgetClass = LoadClass<UObject>(nullptr, *ClassPath);
				if (!WidgetClass && !ClassPath.EndsWith(TEXT("_C")))
				{
					WidgetClass = LoadClass<UObject>(nullptr, *(ClassPath + TEXT("_C")));
				}
			}

			if (!WidgetClass)
			{
				return CreateErrorResponse(TEXT("Failed to resolve widget class for CreateWidget. Provide params.widget_blueprint or params.widget_class_path."));
			}

			UClass* CreateWidgetNodeClass = LoadClass<UK2Node>(nullptr, TEXT("/Script/UMGEditor.K2Node_CreateWidget"));
			if (!CreateWidgetNodeClass)
			{
				return CreateErrorResponse(TEXT("Failed to load /Script/UMGEditor.K2Node_CreateWidget"));
			}

			UK2Node* CreateWidgetNode = NewObject<UK2Node>(TargetGraph, CreateWidgetNodeClass);
			if (!CreateWidgetNode)
			{
				return CreateErrorResponse(TEXT("Failed to create K2Node_CreateWidget node"));
			}

			TargetGraph->AddNode(CreateWidgetNode, false, false);
			CreateWidgetNode->CreateNewGuid();
			CreateWidgetNode->NodePosX = FMath::RoundToInt(Position.X);
			CreateWidgetNode->NodePosY = FMath::RoundToInt(Position.Y);
			CreateWidgetNode->AllocateDefaultPins();

			if (UEdGraphPin* ClassPin = CreateWidgetNode->FindPin(TEXT("Class")))
			{
				ClassPin->DefaultObject = WidgetClass;
				ClassPin->DefaultValue.Empty();
				CreateWidgetNode->PinDefaultValueChanged(ClassPin);
			}

			CreateWidgetNode->ReconstructNode();

			MarkBlueprintModified(Blueprint, Context);
			RegisterCreatedNode(CreateWidgetNode, Context);

			TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
			ResultData->SetStringField(TEXT("node_id"), CreateWidgetNode->NodeGuid.ToString());
			return CreateSuccessResponse(ResultData);
		}
	}

	UK2Node_CallFunction* FunctionNode = FMCPCommonUtils::CreateFunctionCallNode(TargetGraph, Function, Position);
	if (!FunctionNode)
	{
		return CreateErrorResponse(TEXT("Failed to create function call node"));
	}

	// Optional: apply pin defaults from params JSON by pin name.
	// Example:
	// params: {"WidgetType":"/Game/UI/WBP_Options.WBP_Options_C", "ZOrder":0}
	if (ExtraParams.IsValid())
	{
		for (const auto& Pair : ExtraParams->Values)
		{
			const FString PinName = Pair.Key;
			UEdGraphPin* Pin = FMCPCommonUtils::FindPin(FunctionNode, PinName, EGPD_Input);
			if (!Pin) continue;

			const TSharedPtr<FJsonValue>& JsonVal = Pair.Value;
			if (!JsonVal.IsValid()) continue;

			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object
				|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class
				|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject
				|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
			{
				if (JsonVal->Type == EJson::String)
				{
					const FString ObjPath = JsonVal->AsString();
					UObject* LoadedObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjPath);
					if (LoadedObject)
					{
						Pin->DefaultObject = LoadedObject;
						Pin->DefaultValue.Empty();
					}
					else
					{
						Pin->DefaultValue = ObjPath;
					}
				}
			}
			else if (JsonVal->Type == EJson::String)
			{
				Pin->DefaultValue = JsonVal->AsString();
			}
			else if (JsonVal->Type == EJson::Number)
			{
				// Integer pins require a whole-number string; JSON has no
				// int vs float distinction so we must check pin category.
				const double NumVal = JsonVal->AsNumber();
				if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int
					|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
				{
					Pin->DefaultValue = FString::FromInt(static_cast<int32>(NumVal));
				}
				else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int64)
				{
					Pin->DefaultValue = FString::Printf(TEXT("%lld"), static_cast<int64>(NumVal));
				}
				else
				{
					Pin->DefaultValue = LexToString(NumVal);
				}
			}
			else if (JsonVal->Type == EJson::Boolean)
			{
				Pin->DefaultValue = JsonVal->AsBool() ? TEXT("true") : TEXT("false");
			}

			FunctionNode->PinDefaultValueChanged(Pin);
		}
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(FunctionNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), FunctionNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


bool FAddBlueprintSelfReferenceAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintSelfReferenceAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	UK2Node_Self* SelfNode = FMCPCommonUtils::CreateSelfReferenceNode(TargetGraph, Position);
	if (!SelfNode)
	{
		return CreateErrorResponse(TEXT("Failed to create self node"));
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(SelfNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), SelfNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


bool FAddBlueprintGetSelfComponentReferenceAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ComponentName;
	if (!GetRequiredString(Params, TEXT("component_name"), ComponentName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintGetSelfComponentReferenceAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ComponentName = Params->GetStringField(TEXT("component_name"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	UK2Node_VariableGet* GetComponentNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableGet>(
		TargetGraph, Position, EK2NewNodeFlags::None,
		[&ComponentName](UK2Node_VariableGet* Node) { Node->VariableReference.SetSelfMember(FName(*ComponentName)); }
	);
	GetComponentNode->ReconstructNode();

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(GetComponentNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), GetComponentNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


bool FAddBlueprintBranchNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintBranchNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	UK2Node_IfThenElse* BranchNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_IfThenElse>(
		TargetGraph, Position, EK2NewNodeFlags::None
	);

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(BranchNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), BranchNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


bool FAddBlueprintCastNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString TargetClass;
	if (!GetRequiredString(Params, TEXT("target_class"), TargetClass, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintCastNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString TargetClassName = Params->GetStringField(TEXT("target_class"));
	bool bPureCast = GetOptionalBool(Params, TEXT("pure_cast"), false);
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Fix: Respect graph_name parameter to avoid phantom node issue
	UEdGraph* EventGraph = GetTargetGraph(Params, Context);
	if (!EventGraph)
	{
		EventGraph = FMCPCommonUtils::FindOrCreateEventGraph(Blueprint);
	}

	// Find the target class
	UClass* TargetClass = nullptr;

	// Check if it's a content path
	if (TargetClassName.StartsWith(TEXT("/Game/")))
	{
		FString BPPath = TargetClassName;
		if (!BPPath.EndsWith(TEXT("_C")))
		{
			BPPath += TEXT("_C");
		}
		TargetClass = LoadClass<UObject>(nullptr, *BPPath);
		if (!TargetClass)
		{
			TargetClass = LoadClass<UObject>(nullptr, *TargetClassName);
		}
	}

	// Try to find as a blueprint name
	if (!TargetClass)
	{
		UBlueprint* TargetBP = FMCPCommonUtils::FindBlueprint(TargetClassName);
		if (TargetBP && TargetBP->GeneratedClass)
		{
			TargetClass = TargetBP->GeneratedClass;
		}
	}

	// Try engine classes
	if (!TargetClass)
	{
		static const FString ModulePaths[] = {
			TEXT("/Script/Engine"),
			TEXT("/Script/CoreUObject"),
		};
		for (const FString& ModulePath : ModulePaths)
		{
			FString FullPath = FString::Printf(TEXT("%s.%s"), *ModulePath, *TargetClassName);
			TargetClass = LoadClass<UObject>(nullptr, *FullPath);
			if (TargetClass) break;
		}
	}

	if (!TargetClass)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Target class not found: %s"), *TargetClassName));
	}

	UK2Node_DynamicCast* CastNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_DynamicCast>(
		EventGraph, Position, EK2NewNodeFlags::None,
		[TargetClass, bPureCast](UK2Node_DynamicCast* Node) {
			Node->TargetType = TargetClass;
			Node->SetPurity(bPureCast);
		}
	);

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(CastNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), CastNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("target_class"), TargetClass->GetName());
	ResultData->SetBoolField(TEXT("pure_cast"), bPureCast);
	return CreateSuccessResponse(ResultData);
}


// =============================================================================
// Subsystem Nodes
// =============================================================================

bool FAddBlueprintGetSubsystemNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString SubsystemClass;
	if (!GetRequiredString(Params, TEXT("subsystem_class"), SubsystemClass, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintGetSubsystemNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SubsystemClassName = Params->GetStringField(TEXT("subsystem_class"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Fix: Respect graph_name parameter instead of hardcoding EventGraph.
	// This prevents the "phantom node" issue where nodes are silently created
	// in EventGraph when the caller expects them in a function graph.
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);
	if (!TargetGraph)
	{
		TargetGraph = FMCPCommonUtils::FindOrCreateEventGraph(Blueprint);
	}

	// Find the subsystem class
	UClass* FoundClass = nullptr;

	// Try to load the class directly (for full paths like /Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem)
	if (SubsystemClassName.StartsWith(TEXT("/Script/")))
	{
		FoundClass = LoadClass<USubsystem>(nullptr, *SubsystemClassName);
	}

	// Try known module paths
	if (!FoundClass)
	{
		// Build candidate names: original + with U prefix
		TArray<FString> CandidateNames;
		CandidateNames.Add(SubsystemClassName);
		if (!SubsystemClassName.StartsWith(TEXT("U")) && !SubsystemClassName.StartsWith(TEXT("/")))
		{
			CandidateNames.Add(TEXT("U") + SubsystemClassName);
		}

		const FString ModulePaths[] = {
			TEXT("/Script/EnhancedInput"),
			TEXT("/Script/Engine"),
			TEXT("/Script/GameplayAbilities"),
		};

		for (const FString& Candidate : CandidateNames)
		{
			if (FoundClass) break;
			for (const FString& ModulePath : ModulePaths)
			{
				FString FullPath = FString::Printf(TEXT("%s.%s"), *ModulePath, *Candidate);
				FoundClass = LoadClass<USubsystem>(nullptr, *FullPath);
				if (FoundClass) break;
			}
		}
	}

	// Dynamic fallback: scan all loaded packages for any USubsystem subclass matching the name
	if (!FoundClass)
	{
		FString ClassNameOnly = SubsystemClassName;
		if (ClassNameOnly.Contains(TEXT(".")))
		{
			ClassNameOnly = ClassNameOnly.RightChop(ClassNameOnly.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd) + 1);
		}
		if (!ClassNameOnly.StartsWith(TEXT("U")))
		{
			ClassNameOnly = TEXT("U") + ClassNameOnly;
		}

		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->IsChildOf(USubsystem::StaticClass()) && It->GetName() == ClassNameOnly)
			{
				FoundClass = *It;
				break;
			}
		}
	}

	if (!FoundClass)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Subsystem class not found: %s. Try full path like /Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem or /Script/p110_2.UMySubsystem"), *SubsystemClassName));
	}

	// Fix: Select the correct K2Node type based on subsystem inheritance.
	// - UGameInstanceSubsystem  → UK2Node_GetSubsystem (base, uses WorldContext)
	// - ULocalPlayerSubsystem   → UK2Node_GetSubsystemFromPC (needs PlayerController)
	// - UEngineSubsystem         → UK2Node_GetEngineSubsystem
	// - UEditorSubsystem         → UK2Node_GetEditorSubsystem
	// Using the wrong node type causes type-mismatch on the output pin.
	UEdGraphNode* CreatedNode = nullptr;
	FString NodeTypeUsed;

	if (FoundClass->IsChildOf(ULocalPlayerSubsystem::StaticClass()))
	{
		UK2Node_GetSubsystemFromPC* SubsystemNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_GetSubsystemFromPC>(
			TargetGraph, Position, EK2NewNodeFlags::None,
			[FoundClass](UK2Node_GetSubsystemFromPC* Node) { Node->Initialize(FoundClass); }
		);
		CreatedNode = SubsystemNode;
		NodeTypeUsed = TEXT("GetSubsystemFromPC");
	}
	else if (FoundClass->IsChildOf(UEngineSubsystem::StaticClass()))
	{
		UK2Node_GetEngineSubsystem* SubsystemNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_GetEngineSubsystem>(
			TargetGraph, Position, EK2NewNodeFlags::None,
			[FoundClass](UK2Node_GetEngineSubsystem* Node) { Node->Initialize(FoundClass); }
		);
		CreatedNode = SubsystemNode;
		NodeTypeUsed = TEXT("GetEngineSubsystem");
	}
	else
	{
		// Default: UK2Node_GetSubsystem — works for UGameInstanceSubsystem
		// and any other USubsystem subclass
		UK2Node_GetSubsystem* SubsystemNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_GetSubsystem>(
			TargetGraph, Position, EK2NewNodeFlags::None,
			[FoundClass](UK2Node_GetSubsystem* Node) { Node->Initialize(FoundClass); }
		);
		CreatedNode = SubsystemNode;
		NodeTypeUsed = TEXT("GetSubsystem");
	}

	if (!CreatedNode)
	{
		return CreateErrorResponse(TEXT("Failed to spawn subsystem getter node"));
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(CreatedNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), CreatedNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("subsystem_class"), FoundClass->GetName());
	ResultData->SetStringField(TEXT("node_type"), NodeTypeUsed);
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Blueprint Function Graph
// ============================================================================

bool FCreateBlueprintFunctionAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString FunctionName;
	if (!GetRequiredString(Params, TEXT("function_name"), FunctionName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FCreateBlueprintFunctionAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString FunctionName = Params->GetStringField(TEXT("function_name"));
	bool bIsPure = GetOptionalBool(Params, TEXT("is_pure"), false);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Check if function already exists
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph->GetFName() == FName(*FunctionName))
		{
			// Find entry node
			FString EntryNodeId;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node);
				if (EntryNode)
				{
					EntryNodeId = EntryNode->NodeGuid.ToString();
					break;
				}
			}

			TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
			ResultData->SetBoolField(TEXT("already_exists"), true);
			ResultData->SetStringField(TEXT("function_name"), FunctionName);
			ResultData->SetStringField(TEXT("graph_name"), Graph->GetName());
			ResultData->SetStringField(TEXT("entry_node_id"), EntryNodeId);
			return CreateSuccessResponse(ResultData);
		}
	}

	// Create the function graph
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint, FName(*FunctionName),
		UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

	if (!NewGraph)
	{
		return CreateErrorResponse(TEXT("Failed to create function graph"));
	}

	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, NewGraph, true, nullptr);

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	K2Schema->CreateDefaultNodesForGraph(*NewGraph);

	// Find entry and result nodes
	UK2Node_FunctionEntry* EntryNode = nullptr;
	UK2Node_FunctionResult* ResultNode = nullptr;
	for (UEdGraphNode* Node : NewGraph->Nodes)
	{
		if (!EntryNode) EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		if (!ResultNode) ResultNode = Cast<UK2Node_FunctionResult>(Node);
	}

	// Add input parameters
	const TArray<TSharedPtr<FJsonValue>>* InputsArray = GetOptionalArray(Params, TEXT("inputs"));
	if (InputsArray && EntryNode)
	{
		for (const TSharedPtr<FJsonValue>& InputValue : *InputsArray)
		{
			const TSharedPtr<FJsonObject>& InputObj = InputValue->AsObject();
			if (!InputObj) continue;

			FString ParamName, ParamType;
			if (!InputObj->TryGetStringField(TEXT("name"), ParamName)) continue;
			if (!InputObj->TryGetStringField(TEXT("type"), ParamType)) continue;

			FEdGraphPinType PinType;
			FString TypeResolveError;
			if (!FMCPCommonUtils::ResolvePinTypeFromString(ParamType, PinType, TypeResolveError))
			{
				return CreateErrorResponse(FString::Printf(TEXT("Function input '%s': %s"), *ParamName, *TypeResolveError));
			}

			EntryNode->CreateUserDefinedPin(FName(*ParamName), PinType, EGPD_Output);
		}
		EntryNode->ReconstructNode();
	}

	// Add output parameters
	const TArray<TSharedPtr<FJsonValue>>* OutputsArray = GetOptionalArray(Params, TEXT("outputs"));
	if (OutputsArray)
	{
		if (!ResultNode)
		{
			ResultNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_FunctionResult>(
				NewGraph, FVector2D(400, 0), EK2NewNodeFlags::None
			);
		}

		for (const TSharedPtr<FJsonValue>& OutputValue : *OutputsArray)
		{
			const TSharedPtr<FJsonObject>& OutputObj = OutputValue->AsObject();
			if (!OutputObj) continue;

			FString ParamName, ParamType;
			if (!OutputObj->TryGetStringField(TEXT("name"), ParamName)) continue;
			if (!OutputObj->TryGetStringField(TEXT("type"), ParamType)) continue;

			FEdGraphPinType PinType;
			FString TypeResolveError;
			if (!FMCPCommonUtils::ResolvePinTypeFromString(ParamType, PinType, TypeResolveError))
			{
				return CreateErrorResponse(FString::Printf(TEXT("Function output '%s': %s"), *ParamName, *TypeResolveError));
			}

			ResultNode->CreateUserDefinedPin(FName(*ParamName), PinType, EGPD_Input);
		}
		ResultNode->ReconstructNode();
	}

	if (bIsPure && EntryNode)
	{
		K2Schema->AddExtraFunctionFlags(NewGraph, FUNC_BlueprintPure);
	}

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("function_name"), FunctionName);
	ResultData->SetStringField(TEXT("graph_name"), NewGraph->GetName());
	if (EntryNode) ResultData->SetStringField(TEXT("entry_node_id"), EntryNode->NodeGuid.ToString());
	if (ResultNode) ResultData->SetStringField(TEXT("result_node_id"), ResultNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


