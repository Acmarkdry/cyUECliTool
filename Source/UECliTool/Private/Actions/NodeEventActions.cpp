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
// Event Nodes
// ============================================================================

bool FAddBlueprintEventNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// Support both "event_name" (canonical) and "event_type" (alias)
	FString EventName;
	if (!Params->TryGetStringField(TEXT("event_name"), EventName))
	{
		if (!Params->TryGetStringField(TEXT("event_type"), EventName))
		{
			OutError = TEXT("Missing required parameter: event_name (or event_type)");
			return false;
		}
	}
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintEventNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	// Support both "event_name" (canonical) and "event_type" (alias)
	FString EventName;
	if (!Params->TryGetStringField(TEXT("event_name"), EventName))
	{
		Params->TryGetStringField(TEXT("event_type"), EventName);
	}
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* EventGraph = GetTargetGraph(Params, Context);

	// Reuse existing event nodes to avoid duplicate Tick/BeginPlay, etc.
	UK2Node_Event* ExistingEventNode = FMCPCommonUtils::FindExistingEventNode(EventGraph, EventName);
	if (!ExistingEventNode)
	{
		const FString NormalizedEventName = EventName.StartsWith(TEXT("Receive"))
			? EventName.Mid(7)
			: FString::Printf(TEXT("Receive%s"), *EventName);
		ExistingEventNode = FMCPCommonUtils::FindExistingEventNode(EventGraph, NormalizedEventName);
	}
	if (ExistingEventNode)
	{
		TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
		ResultData->SetStringField(TEXT("node_id"), ExistingEventNode->NodeGuid.ToString());
		ResultData->SetBoolField(TEXT("reused_existing"), true);
		return CreateSuccessResponse(ResultData);
	}

	UK2Node_Event* EventNode = FMCPCommonUtils::CreateEventNode(EventGraph, EventName, Position);
	if (!EventNode)
	{
		return CreateErrorResponse(TEXT("Failed to create event node"));
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(EventNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), EventNode->NodeGuid.ToString());
	ResultData->SetBoolField(TEXT("reused_existing"), false);
	return CreateSuccessResponse(ResultData);
}


bool FAddBlueprintInputActionNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ActionName;
	if (!GetRequiredString(Params, TEXT("action_name"), ActionName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintInputActionNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ActionName = Params->GetStringField(TEXT("action_name"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* EventGraph = FMCPCommonUtils::FindOrCreateEventGraph(Blueprint);

	UK2Node_InputAction* InputActionNode = FMCPCommonUtils::CreateInputActionNode(EventGraph, ActionName, Position);
	if (!InputActionNode)
	{
		return CreateErrorResponse(TEXT("Failed to create input action node"));
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(InputActionNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), InputActionNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


bool FAddEnhancedInputActionNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ActionName;
	if (!GetRequiredString(Params, TEXT("action_name"), ActionName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddEnhancedInputActionNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ActionName = Params->GetStringField(TEXT("action_name"));
	FString ActionPath = GetOptionalString(Params, TEXT("action_path"), TEXT("/Game/Input"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* EventGraph = FMCPCommonUtils::FindOrCreateEventGraph(Blueprint);

	// Load the UInputAction asset
	FString AssetPath = FString::Printf(TEXT("%s/%s.%s"), *ActionPath, *ActionName, *ActionName);
	UInputAction* InputActionAsset = LoadObject<UInputAction>(nullptr, *AssetPath);
	if (!InputActionAsset)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Input Action asset not found: %s"), *AssetPath));
	}

	// Create the Enhanced Input Action node using editor's spawn API
	UK2Node_EnhancedInputAction* ActionNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_EnhancedInputAction>(
		EventGraph, Position, EK2NewNodeFlags::None,
		[InputActionAsset](UK2Node_EnhancedInputAction* Node) { Node->InputAction = InputActionAsset; }
	);

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(ActionNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), ActionNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


bool FAddBlueprintCustomEventAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString EventName;
	if (!GetRequiredString(Params, TEXT("event_name"), EventName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintCustomEventAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString EventName = Params->GetStringField(TEXT("event_name"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* EventGraph = GetTargetGraph(Params, Context);

	// Create Custom Event node using editor's spawn API
	UK2Node_CustomEvent* CustomEventNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CustomEvent>(
		EventGraph, Position, EK2NewNodeFlags::None,
		[&EventName](UK2Node_CustomEvent* Node) { Node->CustomFunctionName = FName(*EventName); }
	);

	// Add parameters if provided
	const TArray<TSharedPtr<FJsonValue>>* ParametersArray = GetOptionalArray(Params, TEXT("parameters"));
	if (ParametersArray)
	{
		for (const TSharedPtr<FJsonValue>& ParamValue : *ParametersArray)
		{
			const TSharedPtr<FJsonObject>* ParamObj;
			if (ParamValue->TryGetObject(ParamObj) && ParamObj)
			{
				FString ParamName, ParamType;
				if ((*ParamObj)->TryGetStringField(TEXT("name"), ParamName) &&
					(*ParamObj)->TryGetStringField(TEXT("type"), ParamType))
				{
					FEdGraphPinType PinType;
					FString TypeResolveError;
					if (!FMCPCommonUtils::ResolvePinTypeFromString(ParamType, PinType, TypeResolveError))
					{
						UE_LOG(LogTemp, Warning, TEXT("CustomEvent param '%s': %s, defaulting to Float"), *ParamName, *TypeResolveError);
						PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
						PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
					}

					TSharedPtr<FUserPinInfo> NewPinInfo = MakeShared<FUserPinInfo>();
					NewPinInfo->PinName = FName(*ParamName);
					NewPinInfo->PinType = PinType;
					NewPinInfo->DesiredPinDirection = EGPD_Output;
					CustomEventNode->UserDefinedPins.Add(NewPinInfo);
				}
			}
		}
		CustomEventNode->ReconstructNode();
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(CustomEventNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), CustomEventNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("event_name"), EventName);
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Custom Event For Delegate (signature-matched)
// ============================================================================

UClass* FAddCustomEventForDelegateAction::ResolveClass(const FString& ClassName) const
{
	// Try as full path first
	UClass* FoundClass = FindObject<UClass>(nullptr, *ClassName);
	if (FoundClass) return FoundClass;

	// Strip U/A prefix for fallback search
	FString BaseName = ClassName;
	if ((BaseName.StartsWith(TEXT("U")) || BaseName.StartsWith(TEXT("A"))) && BaseName.Len() > 1)
	{
		BaseName = BaseName.Mid(1);
	}

	// Common module paths to try
	auto TryModule = [&](const TCHAR* Module) -> UClass*
	{
		FString Path = FString::Printf(TEXT("/Script/%s.%s"), Module, *ClassName);
		UClass* Result = FindObject<UClass>(nullptr, *Path);
		if (Result) return Result;
		Path = FString::Printf(TEXT("/Script/%s.%s"), Module, *BaseName);
		return FindObject<UClass>(nullptr, *Path);
	};

	static const TCHAR* Modules[] = {
		TEXT("Engine"), TEXT("UMG"), TEXT("EnhancedInput"), TEXT("AIModule"),
		TEXT("NavigationSystem"), TEXT("GameplayAbilities"), TEXT("Niagara"), TEXT("MediaAssets")
	};

	for (const TCHAR* Module : Modules)
	{
		FoundClass = TryModule(Module);
		if (FoundClass) return FoundClass;
	}

	return nullptr;
}

bool FAddCustomEventForDelegateAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString EventName;
	if (!GetRequiredString(Params, TEXT("event_name"), EventName, OutError)) return false;

	// Must have either (delegate_class + delegate_name) or source_node_id
	FString DelegateClass = GetOptionalString(Params, TEXT("delegate_class"));
	FString DelegateName = GetOptionalString(Params, TEXT("delegate_name"));
	FString SourceNodeId = GetOptionalString(Params, TEXT("source_node_id"));

	if (DelegateClass.IsEmpty() && DelegateName.IsEmpty() && SourceNodeId.IsEmpty())
	{
		OutError = TEXT("Must provide either (delegate_class + delegate_name) or source_node_id to resolve delegate signature.");
		return false;
	}

	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddCustomEventForDelegateAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString EventName = Params->GetStringField(TEXT("event_name"));
	FString DelegateClassName = GetOptionalString(Params, TEXT("delegate_class"));
	FString DelegateName = GetOptionalString(Params, TEXT("delegate_name"));
	FString SourceNodeId = GetOptionalString(Params, TEXT("source_node_id"));
	FString SourcePinName = GetOptionalString(Params, TEXT("source_pin_name"));
	bool bAutoConnect = GetOptionalBool(Params, TEXT("auto_connect"), true);
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	const UFunction* SignatureFunc = nullptr;
	UEdGraphPin* TargetDelegatePin = nullptr;  // Pin to connect to (if from node)
	FString ResolvedDelegateInfo;

	// ---- Mode A: Resolve from class + delegate property name ----
	if (!DelegateClassName.IsEmpty() && !DelegateName.IsEmpty())
	{
		UClass* DelegateClass = ResolveClass(DelegateClassName);
		if (!DelegateClass)
		{
			// Try from blueprint's own GeneratedClass
			if (Blueprint && Blueprint->GeneratedClass)
			{
				// Check if the delegate_class refers to a component type on this blueprint
				for (TFieldIterator<FObjectProperty> It(Blueprint->GeneratedClass); It; ++It)
				{
					if (It->PropertyClass && It->PropertyClass->GetName().Contains(DelegateClassName))
					{
						DelegateClass = It->PropertyClass;
						break;
					}
				}
			}

			if (!DelegateClass)
			{
				return CreateErrorResponse(FString::Printf(
					TEXT("Cannot resolve class '%s'. Try full path like '/Script/Engine.PrimitiveComponent' or short name like 'PrimitiveComponent'."),
					*DelegateClassName));
			}
		}

		// Find the multicast delegate property on the class
		FMulticastDelegateProperty* DelegateProp = nullptr;
		for (TFieldIterator<FMulticastDelegateProperty> It(DelegateClass); It; ++It)
		{
			if (It->GetFName() == FName(*DelegateName))
			{
				DelegateProp = *It;
				break;
			}
		}

		if (!DelegateProp)
		{
			TArray<FString> AvailableDelegates;
			for (TFieldIterator<FMulticastDelegateProperty> It(DelegateClass); It; ++It)
			{
				AvailableDelegates.Add(It->GetName());
			}
			FString DelegateList = AvailableDelegates.Num() > 0
				? FString::Join(AvailableDelegates, TEXT(", "))
				: TEXT("(none)");
			return CreateErrorResponse(FString::Printf(
				TEXT("Delegate '%s' not found on class '%s'. Available delegates: %s"),
				*DelegateName, *DelegateClass->GetName(), *DelegateList));
		}

		SignatureFunc = DelegateProp->SignatureFunction;
		ResolvedDelegateInfo = FString::Printf(TEXT("%s::%s"), *DelegateClass->GetName(), *DelegateName);
	}
	// ---- Mode B: Resolve from existing node's delegate pin ----
	else if (!SourceNodeId.IsEmpty())
	{
		FGuid SourceGuid;
		if (!FGuid::Parse(SourceNodeId, SourceGuid))
		{
			return CreateErrorResponse(FString::Printf(TEXT("Invalid source_node_id GUID: %s"), *SourceNodeId));
		}

		UEdGraphNode* SourceNode = nullptr;
		for (UEdGraphNode* Node : TargetGraph->Nodes)
		{
			if (Node && Node->NodeGuid == SourceGuid)
			{
				SourceNode = Node;
				break;
			}
		}

		if (!SourceNode)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Source node not found: %s"), *SourceNodeId));
		}

		// Find the delegate pin
		if (!SourcePinName.IsEmpty())
		{
			TargetDelegatePin = FMCPCommonUtils::FindPin(SourceNode, SourcePinName, EGPD_Input);
		}

		// Fallback: find first unconnected delegate input pin
		if (!TargetDelegatePin)
		{
			for (UEdGraphPin* Pin : SourceNode->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Input
					&& (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate
						|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate)
					&& Pin->LinkedTo.Num() == 0)
				{
					TargetDelegatePin = Pin;
					break;
				}
			}
		}

		if (!TargetDelegatePin)
		{
			// List available pins for diagnostic
			TArray<FString> PinNames;
			for (UEdGraphPin* Pin : SourceNode->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Input)
				{
					PinNames.Add(FString::Printf(TEXT("%s (%s)"),
						*Pin->PinName.ToString(), *Pin->PinType.PinCategory.ToString()));
				}
			}
			return CreateErrorResponse(FString::Printf(
				TEXT("No delegate input pin found on node %s. Available input pins: %s"),
				*SourceNodeId, *FString::Join(PinNames, TEXT(", "))));
		}

		// Resolve the signature from the delegate pin
		// Check if the owning node is a UK2Node_BaseMCDelegate (AddDelegate, etc.)
		if (const UK2Node_BaseMCDelegate* MCDelegateNode = Cast<UK2Node_BaseMCDelegate>(SourceNode))
		{
			SignatureFunc = MCDelegateNode->GetDelegateSignature();
		}
		else if (TargetDelegatePin->PinType.PinSubCategoryMemberReference.MemberName != NAME_None)
		{
			// Resolve from pin type's member reference
			SignatureFunc = FMemberReference::ResolveSimpleMemberReference<UFunction>(
				TargetDelegatePin->PinType.PinSubCategoryMemberReference);
		}

		if (!SignatureFunc)
		{
			return CreateErrorResponse(FString::Printf(
				TEXT("Cannot resolve delegate signature from pin '%s' on node %s. "
					 "Try using delegate_class + delegate_name mode instead."),
				*TargetDelegatePin->PinName.ToString(), *SourceNodeId));
		}

		ResolvedDelegateInfo = FString::Printf(TEXT("pin '%s' on node %s"),
			*TargetDelegatePin->PinName.ToString(), *SourceNodeId);
	}
	else if (!DelegateName.IsEmpty())
	{
		// delegate_name without delegate_class: try to find it on the blueprint's own class
		if (Blueprint && Blueprint->GeneratedClass)
		{
			FMulticastDelegateProperty* DelegateProp = FindFProperty<FMulticastDelegateProperty>(
				Blueprint->GeneratedClass, FName(*DelegateName));
			if (DelegateProp)
			{
				SignatureFunc = DelegateProp->SignatureFunction;
				ResolvedDelegateInfo = FString::Printf(TEXT("self::%s"), *DelegateName);
			}
		}

		if (!SignatureFunc)
		{
			return CreateErrorResponse(FString::Printf(
				TEXT("Delegate '%s' not found on Blueprint's own class. Specify delegate_class to search another class."),
				*DelegateName));
		}
	}

	if (!SignatureFunc)
	{
		return CreateErrorResponse(TEXT("Failed to resolve delegate signature. Provide (delegate_class + delegate_name) or a valid source_node_id."));
	}

	// Create the custom event node using CreateFromFunction
	UK2Node_CustomEvent* CustomEventNode = UK2Node_CustomEvent::CreateFromFunction(
		Position, TargetGraph, EventName, SignatureFunc, false /*bSelectNewNode*/);

	if (!CustomEventNode)
	{
		return CreateErrorResponse(TEXT("Failed to create custom event node."));
	}

	// If Mode B with auto_connect, connect the delegate output pin to the target
	bool bConnected = false;
	if (TargetDelegatePin && bAutoConnect)
	{
		UEdGraphPin* DelegateOutPin = CustomEventNode->FindPin(UK2Node_Event::DelegateOutputName);
		if (DelegateOutPin)
		{
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			bConnected = Schema->TryCreateConnection(DelegateOutPin, TargetDelegatePin);

			// If connected, reconstruct to lock pins to delegate signature
			if (bConnected)
			{
				CustomEventNode->ReconstructNode();
			}
		}
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(CustomEventNode, Context);

	// Build result
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), CustomEventNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("event_name"), EventName);
	ResultData->SetStringField(TEXT("delegate_source"), ResolvedDelegateInfo);
	ResultData->SetBoolField(TEXT("connected"), bConnected);

	// List output pins (delegate signature parameters)
	TArray<TSharedPtr<FJsonValue>> OutputPins;
	for (const UEdGraphPin* Pin : CustomEventNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output && Pin->PinName != UEdGraphSchema_K2::PN_Then)
		{
			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
			if (!Pin->PinType.PinSubCategory.IsNone())
			{
				PinObj->SetStringField(TEXT("sub_type"), Pin->PinType.PinSubCategory.ToString());
			}
			OutputPins.Add(MakeShared<FJsonValueObject>(PinObj));
		}
	}
	ResultData->SetArrayField(TEXT("output_pins"), OutputPins);

	return CreateSuccessResponse(ResultData);
}


