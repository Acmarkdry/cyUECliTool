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
// Event Dispatchers
// ============================================================================

bool FAddEventDispatcherAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString DispatcherName;
	if (!GetRequiredString(Params, TEXT("dispatcher_name"), DispatcherName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddEventDispatcherAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString DispatcherName = Params->GetStringField(TEXT("dispatcher_name"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Add the delegate variable
	FEdGraphPinType DelegateType;
	DelegateType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
	FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*DispatcherName), DelegateType);

	// Create the delegate signature graph
	FName GraphName = FName(*DispatcherName);
	UEdGraph* SignatureGraph = nullptr;

	for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
	{
		if (Graph->GetFName() == GraphName)
		{
			SignatureGraph = Graph;
			break;
		}
	}

	if (!SignatureGraph)
	{
		SignatureGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, GraphName,
			UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

		if (!SignatureGraph)
		{
			return CreateErrorResponse(TEXT("Failed to create delegate signature graph"));
		}

		SignatureGraph->bEditable = false;

		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		K2Schema->CreateDefaultNodesForGraph(*SignatureGraph);
		K2Schema->CreateFunctionGraphTerminators(*SignatureGraph, (UClass*)nullptr);
		K2Schema->AddExtraFunctionFlags(SignatureGraph, (FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public));
		K2Schema->MarkFunctionEntryAsEditable(SignatureGraph, true);

		Blueprint->DelegateSignatureGraphs.Add(SignatureGraph);
	}

	// Find entry node and add parameters
	UK2Node_FunctionEntry* EntryNode = nullptr;
	for (UEdGraphNode* Node : SignatureGraph->Nodes)
	{
		EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		if (EntryNode) break;
	}

	const TArray<TSharedPtr<FJsonValue>>* ParamsArray = GetOptionalArray(Params, TEXT("parameters"));
	if (EntryNode && ParamsArray)
	{
		for (const TSharedPtr<FJsonValue>& ParamValue : *ParamsArray)
		{
			const TSharedPtr<FJsonObject>& ParamObj = ParamValue->AsObject();
			if (!ParamObj) continue;

			FString ParamName, ParamType;
			if (!ParamObj->TryGetStringField(TEXT("name"), ParamName)) continue;
			if (!ParamObj->TryGetStringField(TEXT("type"), ParamType)) continue;

			FEdGraphPinType PinType;
			FString TypeResolveError;
			if (!FMCPCommonUtils::ResolvePinTypeFromString(ParamType, PinType, TypeResolveError))
			{
				UE_LOG(LogTemp, Warning, TEXT("EventDispatcher param '%s': %s, skipping"), *ParamName, *TypeResolveError);
				continue;
			}

			EntryNode->CreateUserDefinedPin(FName(*ParamName), PinType, EGPD_Output);
		}
		EntryNode->ReconstructNode();
	}

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("dispatcher_name"), DispatcherName);
	return CreateSuccessResponse(ResultData);
}


bool FCallEventDispatcherAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString DispatcherName;
	if (!GetRequiredString(Params, TEXT("dispatcher_name"), DispatcherName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FCallEventDispatcherAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString DispatcherName = Params->GetStringField(TEXT("dispatcher_name"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* EventGraph = GetTargetGraph(Params, Context);

	// Find the delegate property
	FMulticastDelegateProperty* DelegateProp = FindFProperty<FMulticastDelegateProperty>(
		Blueprint->GeneratedClass, FName(*DispatcherName));
	if (!DelegateProp)
	{
		return CreateErrorResponse(FString::Printf(
			TEXT("Delegate property '%s' not found. Compile the blueprint first."), *DispatcherName));
	}

	// Create CallDelegate node
	UClass* GenClass = Blueprint->GeneratedClass;
	UK2Node_CallDelegate* CallNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CallDelegate>(
		EventGraph, Position, EK2NewNodeFlags::None,
		[DelegateProp, GenClass](UK2Node_CallDelegate* Node) { Node->SetFromProperty(DelegateProp, false, GenClass); }
	);

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(CallNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), CallNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


bool FBindEventDispatcherAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString DispatcherName;
	if (!GetRequiredString(Params, TEXT("dispatcher_name"), DispatcherName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FBindEventDispatcherAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString DispatcherName = Params->GetStringField(TEXT("dispatcher_name"));
	FString TargetBlueprintName = GetOptionalString(Params, TEXT("target_blueprint"));
	FString BindingMode = GetOptionalString(Params, TEXT("binding_mode"));
	FString FunctionName = GetOptionalString(Params, TEXT("function_name"));
	const bool bBindToFunction = BindingMode.Equals(TEXT("function"), ESearchCase::IgnoreCase)
		|| GetOptionalBool(Params, TEXT("bind_to_function"), false);
	const bool bCreateFunctionIfMissing = GetOptionalBool(Params, TEXT("create_function_if_missing"), true);
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Get target blueprint (defaults to self)
	UBlueprint* TargetBlueprint = Blueprint;
	if (!TargetBlueprintName.IsEmpty())
	{
		TargetBlueprint = FMCPCommonUtils::FindBlueprint(TargetBlueprintName);
		if (!TargetBlueprint)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Target blueprint not found: %s"), *TargetBlueprintName));
		}
	}

	UEdGraph* EventGraph = GetTargetGraph(Params, Context);

	// Find the delegate property
	FMulticastDelegateProperty* DelegateProp = FindFProperty<FMulticastDelegateProperty>(
		TargetBlueprint->GeneratedClass, FName(*DispatcherName));
	if (!DelegateProp)
	{
		TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
		ResultData->SetStringField(TEXT("message"), TEXT("Dispatcher not found. Compile the target blueprint first."));
		return CreateErrorResponse(TEXT("Dispatcher not found in compiled class. Compile the target blueprint first."));
	}

	UFunction* SignatureFunc = DelegateProp->SignatureFunction;

	// Create UK2Node_AddDelegate
	UClass* TargetGenClass = TargetBlueprint->GeneratedClass;
	UK2Node_AddDelegate* BindNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_AddDelegate>(
		EventGraph, Position, EK2NewNodeFlags::None,
		[DelegateProp, TargetGenClass](UK2Node_AddDelegate* Node) { Node->SetFromProperty(DelegateProp, false, TargetGenClass); }
	);

	if (bBindToFunction)
	{
		if (FunctionName.IsEmpty())
		{
			FunctionName = FString::Printf(TEXT("On%s"), *DispatcherName);
		}

		UEdGraph* FunctionGraph = FMCPCommonUtils::FindFunctionGraph(Blueprint, FunctionName);
		bool bFunctionCreated = false;

		if (!FunctionGraph && bCreateFunctionIfMissing)
		{
			FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
				Blueprint, FName(*FunctionName),
				UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

			if (!FunctionGraph)
			{
				return CreateErrorResponse(FString::Printf(TEXT("Failed to create function graph '%s'"), *FunctionName));
			}

			FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, FunctionGraph, true, nullptr);
			const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
			K2Schema->CreateDefaultNodesForGraph(*FunctionGraph);

			UK2Node_FunctionEntry* EntryNode = nullptr;
			for (UEdGraphNode* Node : FunctionGraph->Nodes)
			{
				EntryNode = Cast<UK2Node_FunctionEntry>(Node);
				if (EntryNode)
				{
					break;
				}
			}

			if (EntryNode && SignatureFunc)
			{
				for (TFieldIterator<FProperty> PropIt(SignatureFunc); PropIt && (PropIt->PropertyFlags & CPF_Parm); ++PropIt)
				{
					FProperty* Param = *PropIt;
					if (!(Param->PropertyFlags & CPF_ReturnParm))
					{
						FEdGraphPinType PinType;
						if (K2Schema->ConvertPropertyToPinType(Param, PinType))
						{
							EntryNode->CreateUserDefinedPin(Param->GetFName(), PinType, EGPD_Output);
						}
					}
				}

				EntryNode->ReconstructNode();
			}

			bFunctionCreated = true;
		}

		if (!FunctionGraph)
		{
			return CreateErrorResponse(FString::Printf(
				TEXT("Function '%s' not found. Set create_function_if_missing=true to auto-create it."),
				*FunctionName));
		}

		FVector2D DelegatePosition(Position.X + 320.f, Position.Y);
		UK2Node_CreateDelegate* CreateDelegateNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CreateDelegate>(
			EventGraph, DelegatePosition, EK2NewNodeFlags::None,
			[](UK2Node_CreateDelegate* Node) {}
		);

		if (!CreateDelegateNode)
		{
			return CreateErrorResponse(TEXT("Failed to create CreateDelegate node for function binding"));
		}

		UEdGraphPin* BindDelegatePin = BindNode->GetDelegatePin();
		UEdGraphPin* DelegateOutPin = CreateDelegateNode->GetDelegateOutPin();
		bool bConnected = false;

		if (BindDelegatePin && DelegateOutPin)
		{
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			bConnected = Schema->TryCreateConnection(DelegateOutPin, BindDelegatePin);
		}

		CreateDelegateNode->SetFunction(FName(*FunctionName));
		CreateDelegateNode->HandleAnyChange(true);

		const bool bFunctionResolved = (CreateDelegateNode->GetFunctionName() != NAME_None);

		MarkBlueprintModified(Blueprint, Context);

		TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
		ResultData->SetStringField(TEXT("bind_node_id"), BindNode->NodeGuid.ToString());
		ResultData->SetStringField(TEXT("delegate_node_id"), CreateDelegateNode->NodeGuid.ToString());
		ResultData->SetStringField(TEXT("function_name"), FunctionName);
		ResultData->SetBoolField(TEXT("function_created"), bFunctionCreated);
		ResultData->SetBoolField(TEXT("function_resolved"), bFunctionResolved);
		ResultData->SetBoolField(TEXT("delegate_connected"), bConnected);
		ResultData->SetStringField(TEXT("binding_type"), TEXT("function"));

		if (!bConnected || !bFunctionResolved)
		{
			ResultData->SetStringField(TEXT("warning"),
				TEXT("Function binding created but may need manual fix. Ensure dispatcher signature matches function parameters."));
		}

		return CreateSuccessResponse(ResultData);
	}

	// Create matching Custom Event
	FString EventName = FString::Printf(TEXT("On%s"), *DispatcherName);
	FVector2D EventPosition(Position.X + 300, Position.Y);
	UK2Node_CustomEvent* CustomEventNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CustomEvent>(
		EventGraph, EventPosition, EK2NewNodeFlags::None,
		[&EventName](UK2Node_CustomEvent* Node) { Node->CustomFunctionName = FName(*EventName); }
	);

	// Set delegate signature — this ensures the custom event's pins and type
	// exactly match the dispatcher, making it connectable to the delegate pin.
	if (SignatureFunc)
	{
		CustomEventNode->SetDelegateSignature(SignatureFunc);
	}

	// Connect custom event delegate output to bind node BEFORE ReconstructNode
	// so that ReconstructNode can resolve the delegate signature from the connection.
	UEdGraphPin* EventDelegatePin = CustomEventNode->FindPin(UK2Node_Event::DelegateOutputName);
	UEdGraphPin* BindDelegatePin = BindNode->GetDelegatePin();
	bool bConnected = false;
	if (EventDelegatePin && BindDelegatePin)
	{
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		bConnected = Schema->TryCreateConnection(EventDelegatePin, BindDelegatePin);
	}

	// Reconstruct after connecting to fully resolve delegate signature
	CustomEventNode->ReconstructNode();

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("bind_node_id"), BindNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("event_node_id"), CustomEventNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("event_name"), EventName);
	ResultData->SetStringField(TEXT("binding_type"), TEXT("custom_event"));
	ResultData->SetBoolField(TEXT("delegate_connected"), bConnected);
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Create Event Delegate (K2Node_CreateDelegate)
// ============================================================================

bool FCreateEventDelegateAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString FunctionName;
	if (!GetRequiredString(Params, TEXT("function_name"), FunctionName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FCreateEventDelegateAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString FunctionName = Params->GetStringField(TEXT("function_name"));
	FString ConnectToNodeId = GetOptionalString(Params, TEXT("connect_to_node_id"));
	FString ConnectToPin = GetOptionalString(Params, TEXT("connect_to_pin"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Create the K2Node_CreateDelegate node
	UK2Node_CreateDelegate* CreateDelegateNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CreateDelegate>(
		TargetGraph, Position, EK2NewNodeFlags::None,
		[](UK2Node_CreateDelegate* Node) {}
	);

	if (!CreateDelegateNode)
	{
		return CreateErrorResponse(TEXT("Failed to create CreateDelegate node"));
	}

	// If connect_to_node_id is specified, connect the delegate output pin first
	// (required before SetFunction so the node can resolve the delegate signature)
	bool bConnected = false;
	if (!ConnectToNodeId.IsEmpty())
	{
		FGuid TargetNodeGuid;
		if (FGuid::Parse(ConnectToNodeId, TargetNodeGuid))
		{
			UEdGraphNode* TargetNode = nullptr;
			for (UEdGraphNode* Node : TargetGraph->Nodes)
			{
				if (Node && Node->NodeGuid == TargetNodeGuid)
				{
					TargetNode = Node;
					break;
				}
			}

			if (TargetNode)
			{
				// Find the target delegate pin
				FString PinName = ConnectToPin.IsEmpty() ? TEXT("Event") : ConnectToPin;
				UEdGraphPin* TargetDelegatePin = FMCPCommonUtils::FindPin(TargetNode, PinName, EGPD_Input);

				// Fallback: look for any unconnected delegate input pin
				if (!TargetDelegatePin)
				{
					for (UEdGraphPin* Pin : TargetNode->Pins)
					{
						if (Pin && Pin->Direction == EGPD_Input
							&& Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate
							&& Pin->LinkedTo.Num() == 0)
						{
							TargetDelegatePin = Pin;
							break;
						}
					}
				}

				if (TargetDelegatePin)
				{
					UEdGraphPin* DelegateOutPin = CreateDelegateNode->GetDelegateOutPin();
					if (DelegateOutPin)
					{
						const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
						if (Schema->TryCreateConnection(DelegateOutPin, TargetDelegatePin))
						{
							bConnected = true;
						}
					}
				}
			}
		}
	}

	// Set the function name
	CreateDelegateNode->SetFunction(FName(*FunctionName));

	// Trigger validation and GUID resolution
	CreateDelegateNode->HandleAnyChange(true);

	MarkBlueprintModified(Blueprint, Context);

	// Build result
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), CreateDelegateNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("function_name"), FunctionName);
	ResultData->SetBoolField(TEXT("connected"), bConnected);

	// Check validity by inspecting whether the function was resolved
	FName ResolvedName = CreateDelegateNode->GetFunctionName();
	bool bFunctionResolved = (ResolvedName != NAME_None);
	ResultData->SetBoolField(TEXT("function_resolved"), bFunctionResolved);

	if (!bFunctionResolved || !bConnected)
	{
		ResultData->SetStringField(TEXT("warning"),
			TEXT("Node created but may need manual setup. Ensure: (1) delegate output is connected to a delegate pin, (2) function_name exists in scope, (3) function signature matches the delegate."));
	}

	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Component Bound Event (bind component delegates like OnTTSEnvelope)
// ============================================================================

bool FBindComponentEventAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ComponentName, EventName;
	if (!GetRequiredString(Params, TEXT("component_name"), ComponentName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("event_name"), EventName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FBindComponentEventAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ComponentName = Params->GetStringField(TEXT("component_name"));
	FString EventName = Params->GetStringField(TEXT("event_name"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		return CreateErrorResponse(TEXT("Invalid Blueprint or GeneratedClass not available. Compile the Blueprint first."));
	}

	// 1. Find the component as FObjectProperty on the GeneratedClass
	FObjectProperty* ComponentProp = FindFProperty<FObjectProperty>(
		Blueprint->GeneratedClass, FName(*ComponentName));
	if (!ComponentProp)
	{
		// Collect available component properties for diagnostic
		TArray<FString> AvailableComponents;
		for (TFieldIterator<FObjectProperty> It(Blueprint->GeneratedClass); It; ++It)
		{
			if (It->PropertyClass && It->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
			{
				AvailableComponents.Add(It->GetName());
			}
		}
		FString CompList = AvailableComponents.Num() > 0
			? FString::Join(AvailableComponents, TEXT(", "))
			: TEXT("(none)");
		return CreateErrorResponse(FString::Printf(
			TEXT("Component '%s' not found as a property on GeneratedClass. "
				 "The component must be added via SCS or declared as UPROPERTY in C++ parent. "
				 "Available component properties: %s"),
			*ComponentName, *CompList));
	}

	// 2. Find the delegate property on the component class
	UClass* ComponentClass = ComponentProp->PropertyClass;
	if (!ComponentClass)
	{
		return CreateErrorResponse(FString::Printf(
			TEXT("Component property '%s' has no valid PropertyClass."), *ComponentName));
	}

	FMulticastDelegateProperty* DelegateProp = nullptr;
	for (TFieldIterator<FMulticastDelegateProperty> It(ComponentClass); It; ++It)
	{
		if (It->GetFName() == FName(*EventName))
		{
			DelegateProp = *It;
			break;
		}
	}

	if (!DelegateProp)
	{
		// Collect available delegates for diagnostic
		TArray<FString> AvailableDelegates;
		for (TFieldIterator<FMulticastDelegateProperty> It(ComponentClass); It; ++It)
		{
			AvailableDelegates.Add(It->GetName());
		}
		FString DelegateList = AvailableDelegates.Num() > 0
			? FString::Join(AvailableDelegates, TEXT(", "))
			: TEXT("(none)");
		return CreateErrorResponse(FString::Printf(
			TEXT("Delegate '%s' not found on component class '%s'. Available delegates: %s"),
			*EventName, *ComponentClass->GetName(), *DelegateList));
	}

	// 3. Get the event graph
	UEdGraph* EventGraph = GetTargetGraph(Params, Context);
	if (!EventGraph)
	{
		EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	}
	if (!EventGraph)
	{
		return CreateErrorResponse(TEXT("Failed to find event graph"));
	}

	// 4. Check if a ComponentBoundEvent node already exists for this combo
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		UK2Node_ComponentBoundEvent* ExistingEvent = Cast<UK2Node_ComponentBoundEvent>(Node);
		if (ExistingEvent &&
			ExistingEvent->ComponentPropertyName == FName(*ComponentName) &&
			ExistingEvent->DelegatePropertyName == DelegateProp->GetFName())
		{
			TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
			ResultData->SetBoolField(TEXT("already_exists"), true);
			ResultData->SetStringField(TEXT("component_name"), ComponentName);
			ResultData->SetStringField(TEXT("event_name"), EventName);
			ResultData->SetStringField(TEXT("node_id"), ExistingEvent->NodeGuid.ToString());
			return CreateSuccessResponse(ResultData);
		}
	}

	// 5. Create UK2Node_ComponentBoundEvent using the standard engine initializer
	UK2Node_ComponentBoundEvent* EventNode = NewObject<UK2Node_ComponentBoundEvent>(EventGraph);
	EventGraph->AddNode(EventNode, false, false);
	EventNode->CreateNewGuid();

	// Use the engine's standard initialization (sets ComponentPropertyName,
	// DelegatePropertyName, DelegateOwnerClass, EventReference,
	// CustomFunctionName, bOverrideFunction, bInternalEvent)
	EventNode->InitializeComponentBoundEventParams(ComponentProp, DelegateProp);

	EventNode->NodePosX = static_cast<int32>(Position.X);
	EventNode->NodePosY = static_cast<int32>(Position.Y);
	EventNode->AllocateDefaultPins();

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(EventNode, Context);

	// 6. Build result with output pin info
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), EventNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("component_name"), ComponentName);
	ResultData->SetStringField(TEXT("event_name"), EventName);
	ResultData->SetStringField(TEXT("component_class"), ComponentClass->GetName());

	// List output pins for the caller
	TArray<TSharedPtr<FJsonValue>> OutputPins;
	for (const UEdGraphPin* Pin : EventNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output)
		{
			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
			OutputPins.Add(MakeShared<FJsonValueObject>(PinObj));
		}
	}
	ResultData->SetArrayField(TEXT("output_pins"), OutputPins);

	return CreateSuccessResponse(ResultData);
}


