// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/UMGActions.h"
#include "MCPCommonUtils.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBox.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/ComboBoxString.h"
#include "Components/CheckBox.h"

#include "UMGCommonHelpers.h"
// MVVM includes
#include "MVVMBlueprintView.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "MVVMBlueprintViewModelContext.h"
#include "MVVMBlueprintViewBinding.h"
#include "MVVMPropertyPath.h"
#include "Types/MVVMBindingMode.h"
#include "Types/MVVMExecutionMode.h"
#include "INotifyFieldValueChanged.h"
#include "Components/SpinBox.h"
#include "Components/EditableTextBox.h"
#include "Dom/JsonValue.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_VariableGet.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_ComponentBoundEvent.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Texture2D.h"
#include "Components/ScrollBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/BackgroundBlur.h"
#include "Components/UniformGridPanel.h"
#include "Components/Spacer.h"
#include "Components/RichTextBlock.h"
#include "Components/WrapBox.h"
#include "Components/CircularThrobber.h"


// FBindWidgetEventAction
// =============================================================================

bool FBindWidgetEventAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("widget_component_name")))
	{
		OutError = TEXT("Missing 'widget_component_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("event_name")))
	{
		OutError = TEXT("Missing 'event_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FBindWidgetEventAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetComponentName = Params->GetStringField(TEXT("widget_component_name"));
	FString EventName = Params->GetStringField(TEXT("event_name"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	// Find the widget in the WidgetTree
	UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(*WidgetComponentName);
	if (!Widget)
	{
		TArray<FString> AvailableWidgets;
		WidgetBlueprint->WidgetTree->ForEachWidget([&AvailableWidgets](UWidget* W) {
			if (W) AvailableWidgets.Add(W->GetName());
		});
		FString WidgetList = FString::Join(AvailableWidgets, TEXT(", "));
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget '%s' not found. Available: %s"), *WidgetComponentName, *WidgetList));
	}

	// Verify delegate exists
	FMulticastDelegateProperty* DelegateProp = nullptr;
	for (TFieldIterator<FMulticastDelegateProperty> It(Widget->GetClass()); It; ++It)
	{
		if (It->GetFName() == FName(*EventName))
		{
			DelegateProp = *It;
			break;
		}
	}

	if (!DelegateProp)
	{
		TArray<FString> AvailableDelegates;
		for (TFieldIterator<FMulticastDelegateProperty> It(Widget->GetClass()); It; ++It)
		{
			AvailableDelegates.Add(It->GetName());
		}
		FString DelegateList = FString::Join(AvailableDelegates, TEXT(", "));
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Delegate '%s' not found. Available: %s"), *EventName, *DelegateList));
	}

	// Get event graph
	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(WidgetBlueprint);
	if (!EventGraph)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to find event graph"));
	}

	// Check if Component Bound Event node already exists for this widget/delegate combo
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		UK2Node_ComponentBoundEvent* ExistingEvent = Cast<UK2Node_ComponentBoundEvent>(Node);
		if (ExistingEvent &&
			ExistingEvent->ComponentPropertyName == FName(*WidgetComponentName) &&
			ExistingEvent->DelegatePropertyName == DelegateProp->GetFName())
		{
			// Already exists - return the existing node
			TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
			ResultObj->SetBoolField(TEXT("success"), true);
			ResultObj->SetBoolField(TEXT("already_exists"), true);
			ResultObj->SetStringField(TEXT("widget_name"), WidgetComponentName);
			ResultObj->SetStringField(TEXT("event_name"), EventName);
			ResultObj->SetStringField(TEXT("node_id"), ExistingEvent->NodeGuid.ToString());
			return ResultObj;
		}
	}

	// Calculate position for new node (below existing nodes)
	float MaxY = 0.0f;
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		MaxY = FMath::Max(MaxY, (float)Node->NodePosY);
	}

	// Create Component Bound Event node - this is the proper way to handle widget events
	UK2Node_ComponentBoundEvent* EventNode = NewObject<UK2Node_ComponentBoundEvent>(EventGraph);
	EventGraph->AddNode(EventNode, false, false);
	EventNode->CreateNewGuid();

	// Find the widget as FObjectProperty on the WidgetBlueprint's GeneratedClass
	// to use the proper engine initializer (sets EventReference, CustomFunctionName,
	// bOverrideFunction, bInternalEvent in addition to the 3 basic fields)
	FObjectProperty* WidgetProp = FindFProperty<FObjectProperty>(
		WidgetBlueprint->GeneratedClass, FName(*WidgetComponentName));
	if (WidgetProp)
	{
		EventNode->InitializeComponentBoundEventParams(WidgetProp, DelegateProp);
	}
	else
	{
		// Fallback: manual field assignment (legacy path for edge cases)
		EventNode->ComponentPropertyName = FName(*WidgetComponentName);
		EventNode->DelegatePropertyName = DelegateProp->GetFName();
		EventNode->DelegateOwnerClass = Widget->GetClass();
	}

	EventNode->NodePosX = 200;
	EventNode->NodePosY = (int32)(MaxY + 200);
	EventNode->AllocateDefaultPins();

	UE_LOG(LogMCP, Log, TEXT("Created Component Bound Event: %s.%s"), *WidgetComponentName, *EventName);

	// Mark modified, compile, and refresh the Widget Designer
	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), WidgetComponentName);
	ResultObj->SetStringField(TEXT("event_name"), EventName);
	ResultObj->SetStringField(TEXT("node_id"), EventNode->NodeGuid.ToString());
	return ResultObj;
}

// =============================================================================
// FAddWidgetToViewportAction
// =============================================================================

bool FAddWidgetToViewportAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddWidgetToViewportAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	int32 ZOrder = 0;
	Params->TryGetNumberField(TEXT("z_order"), ZOrder);

	UClass* WidgetClass = WidgetBlueprint->GeneratedClass;
	if (!WidgetClass)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get widget class"));
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("blueprint_name"), BlueprintName);
	ResultObj->SetStringField(TEXT("class_path"), WidgetClass->GetPathName());
	ResultObj->SetNumberField(TEXT("z_order"), ZOrder);
	ResultObj->SetStringField(TEXT("note"), TEXT("Widget class ready. Use CreateWidget and AddToViewport nodes in Blueprint."));
	return ResultObj;
}

// =============================================================================
// FSetTextBlockBindingAction
// =============================================================================

bool FSetTextBlockBindingAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("text_block_name")))
	{
		OutError = TEXT("Missing 'text_block_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("binding_property")))
	{
		OutError = TEXT("Missing 'binding_property' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FSetTextBlockBindingAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetName = Params->GetStringField(TEXT("text_block_name"));
	FString BindingName = Params->GetStringField(TEXT("binding_property"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	// Create variable for binding
	FBlueprintEditorUtils::AddMemberVariable(
		WidgetBlueprint,
		FName(*BindingName),
		FEdGraphPinType(UEdGraphSchema_K2::PC_Text, NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType())
	);

	// Find the TextBlock widget
	UTextBlock* TextBlock = Cast<UTextBlock>(WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName)));
	if (!TextBlock)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("TextBlock '%s' not found"), *WidgetName));
	}

	// Create binding function
	const FString FunctionName = FString::Printf(TEXT("Get%s"), *BindingName);
	UEdGraph* FuncGraph = FBlueprintEditorUtils::CreateNewGraph(
		WidgetBlueprint,
		FName(*FunctionName),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass()
	);

	if (FuncGraph)
	{
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(WidgetBlueprint, FuncGraph, false, nullptr);

		// Create entry node
		UK2Node_FunctionEntry* EntryNode = NewObject<UK2Node_FunctionEntry>(FuncGraph);
		FuncGraph->AddNode(EntryNode, false, false);
		EntryNode->NodePosX = 0;
		EntryNode->NodePosY = 0;
		EntryNode->FunctionReference.SetExternalMember(FName(*FunctionName), WidgetBlueprint->GeneratedClass);
		EntryNode->AllocateDefaultPins();

		// Create get variable node
		UK2Node_VariableGet* GetVarNode = NewObject<UK2Node_VariableGet>(FuncGraph);
		GetVarNode->VariableReference.SetSelfMember(FName(*BindingName));
		FuncGraph->AddNode(GetVarNode, false, false);
		GetVarNode->NodePosX = 200;
		GetVarNode->NodePosY = 0;
		GetVarNode->AllocateDefaultPins();

		// Connect nodes
		UEdGraphPin* EntryThenPin = EntryNode->FindPin(UEdGraphSchema_K2::PN_Then);
		UEdGraphPin* GetVarOutPin = GetVarNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
		if (EntryThenPin && GetVarOutPin)
		{
			EntryThenPin->MakeLinkTo(GetVarOutPin);
		}
	}

	// Mark modified, compile, and refresh the Widget Designer
	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("binding_name"), BindingName);
	return ResultObj;
}

// =============================================================================
// FListWidgetComponentsAction
// =============================================================================

bool FListWidgetComponentsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FListWidgetComponentsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	if (!WidgetBlueprint->WidgetTree)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Widget Blueprint has no WidgetTree"));
	}

	TArray<TSharedPtr<FJsonValue>> Components;
	WidgetBlueprint->WidgetTree->ForEachWidget([&Components](UWidget* Widget)
	{
		if (!Widget)
		{
			return;
		}

		TSharedPtr<FJsonObject> ComponentObj = MakeShared<FJsonObject>();
		ComponentObj->SetStringField(TEXT("name"), Widget->GetName());
		ComponentObj->SetStringField(TEXT("class"), Widget->GetClass()->GetName());
		Components.Add(MakeShared<FJsonValueObject>(ComponentObj));
	});

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), BlueprintName);
	ResultObj->SetNumberField(TEXT("count"), Components.Num());
	ResultObj->SetArrayField(TEXT("components"), Components);
	return ResultObj;
}

// =============================================================================
// FReparentWidgetsAction
// =============================================================================

// Map of supported container types �� UClass*
static UClass* ResolveContainerClass(const FString& ContainerType)
{
	if (ContainerType.Equals(TEXT("VerticalBox"), ESearchCase::IgnoreCase)) return UVerticalBox::StaticClass();
	if (ContainerType.Equals(TEXT("HorizontalBox"), ESearchCase::IgnoreCase)) return UHorizontalBox::StaticClass();
	if (ContainerType.Equals(TEXT("Overlay"), ESearchCase::IgnoreCase)) return UOverlay::StaticClass();
	if (ContainerType.Equals(TEXT("CanvasPanel"), ESearchCase::IgnoreCase)) return UCanvasPanel::StaticClass();
	if (ContainerType.Equals(TEXT("SizeBox"), ESearchCase::IgnoreCase)) return USizeBox::StaticClass();
	if (ContainerType.Equals(TEXT("ScaleBox"), ESearchCase::IgnoreCase)) return UScaleBox::StaticClass();
	if (ContainerType.Equals(TEXT("Border"), ESearchCase::IgnoreCase)) return UBorder::StaticClass();
	return nullptr;
}

bool FReparentWidgetsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("target_container_name")))
	{
		OutError = TEXT("Missing 'target_container_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FReparentWidgetsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString TargetContainerName = Params->GetStringField(TEXT("target_container_name"));

	// container_type: VerticalBox, HorizontalBox, Overlay, CanvasPanel, SizeBox, ScaleBox, Border
	FString ContainerType = TEXT("VerticalBox");
	Params->TryGetStringField(TEXT("container_type"), ContainerType);

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(WidgetBlueprint->WidgetTree->RootWidget);
	if (!RootCanvas)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Root widget is not a Canvas Panel"));
	}

	// --- Resolve or create the target container ---
	UPanelWidget* TargetContainer = Cast<UPanelWidget>(WidgetBlueprint->WidgetTree->FindWidget(FName(*TargetContainerName)));
	if (!TargetContainer)
	{
		UClass* ContainerClass = ResolveContainerClass(ContainerType);
		if (!ContainerClass)
		{
			return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Unknown container_type '%s'. Supported: VerticalBox, HorizontalBox, Overlay, CanvasPanel, SizeBox, ScaleBox, Border"),
				*ContainerType));
		}

		UWidget* NewWidget = WidgetBlueprint->WidgetTree->ConstructWidget<UWidget>(ContainerClass, *TargetContainerName);
		TargetContainer = Cast<UPanelWidget>(NewWidget);
		if (!TargetContainer)
		{
			return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Failed to create container '%s' of type '%s'"), *TargetContainerName, *ContainerType));
		}

		UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(TargetContainer);
		ApplyCanvasSlot(Slot, Params);
	}
	else if (UCanvasPanelSlot* ExistingSlot = Cast<UCanvasPanelSlot>(TargetContainer->Slot))
	{
		ApplyCanvasSlot(ExistingSlot, Params);
	}

	// --- Determine which children to move ---
	TSet<FString> ChildFilter;
	const TArray<TSharedPtr<FJsonValue>>* ChildNames = nullptr;
	if (Params->TryGetArrayField(TEXT("children"), ChildNames))
	{
		for (const TSharedPtr<FJsonValue>& Val : *ChildNames)
		{
			ChildFilter.Add(Val->AsString());
		}
	}

	FString FilterClass;
	Params->TryGetStringField(TEXT("filter_class"), FilterClass);

	// Collect widgets to move (skip the target container itself)
	TArray<UWidget*> WidgetsToMove;
	WidgetBlueprint->WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		if (!Widget || Widget == TargetContainer)
		{
			return;
		}
		// Already a child of target
		if (Widget->GetParent() == TargetContainer)
		{
			return;
		}
		// If explicit children list provided, match by name
		if (ChildFilter.Num() > 0)
		{
			if (ChildFilter.Contains(Widget->GetName()))
			{
				WidgetsToMove.Add(Widget);
			}
			return;
		}
		// If filter_class provided, match by class name
		if (!FilterClass.IsEmpty())
		{
			if (Widget->GetClass()->GetName().Equals(FilterClass, ESearchCase::IgnoreCase))
			{
				WidgetsToMove.Add(Widget);
			}
			return;
		}
		// No filter �� move all direct children of root canvas (except target)
		if (Widget->GetParent() == RootCanvas)
		{
			WidgetsToMove.Add(Widget);
		}
	});

	// Move widgets into target container
	TArray<TSharedPtr<FJsonValue>> MovedNames;
	for (UWidget* Widget : WidgetsToMove)
	{
		Widget->RemoveFromParent();
		TargetContainer->AddChild(Widget);
		MovedNames.Add(MakeShared<FJsonValueString>(Widget->GetName()));
	}

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), BlueprintName);
	ResultObj->SetStringField(TEXT("target_container"), TargetContainer->GetName());
	ResultObj->SetStringField(TEXT("container_class"), TargetContainer->GetClass()->GetName());
	ResultObj->SetNumberField(TEXT("moved_count"), WidgetsToMove.Num());
	ResultObj->SetArrayField(TEXT("moved_widgets"), MovedNames);
	return ResultObj;
}

// =============================================================================
// FSetWidgetPropertiesAction
// =============================================================================

bool FSetWidgetPropertiesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("target")))
	{
		OutError = TEXT("Missing 'target' parameter (name of widget to modify)");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FSetWidgetPropertiesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString TargetName = Params->GetStringField(TEXT("target"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	UWidget* TargetWidget = WidgetBlueprint->WidgetTree->FindWidget(FName(*TargetName));
	if (!TargetWidget)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget '%s' not found in '%s'"), *TargetName, *BlueprintName));
	}

	TArray<FString> AppliedProps;

	// --- Render Transform: scale ---
	FVector2D Scale;
	if (TryGetVector2Field(Params, TEXT("render_scale"), Scale))
	{
		TargetWidget->SetRenderScale(Scale);
		AppliedProps.Add(TEXT("render_scale"));
	}

	// --- Render Transform: rotation angle ---
	double Angle = 0.0;
	if (Params->TryGetNumberField(TEXT("render_angle"), Angle))
	{
		TargetWidget->SetRenderTransformAngle(static_cast<float>(Angle));
		AppliedProps.Add(TEXT("render_angle"));
	}

	// --- Render Transform: shear ---
	FVector2D Shear;
	if (TryGetVector2Field(Params, TEXT("render_shear"), Shear))
	{
		TargetWidget->SetRenderShear(Shear);
		AppliedProps.Add(TEXT("render_shear"));
	}

	// --- Render Transform: translation ---
	FVector2D Translation;
	if (TryGetVector2Field(Params, TEXT("render_translation"), Translation))
	{
		FWidgetTransform CurrentTransform = TargetWidget->GetRenderTransform();
		CurrentTransform.Translation = Translation;
		TargetWidget->SetRenderTransform(CurrentTransform);
		AppliedProps.Add(TEXT("render_translation"));
	}

	// --- Render Transform: pivot ---
	FVector2D Pivot;
	if (TryGetVector2Field(Params, TEXT("render_pivot"), Pivot))
	{
		TargetWidget->SetRenderTransformPivot(Pivot);
		AppliedProps.Add(TEXT("render_pivot"));
	}

	// --- Visibility ---
	FString VisibilityStr;
	if (Params->TryGetStringField(TEXT("visibility"), VisibilityStr))
	{
		if (VisibilityStr.Equals(TEXT("Visible"), ESearchCase::IgnoreCase))
		{
			TargetWidget->SetVisibility(ESlateVisibility::Visible);
		}
		else if (VisibilityStr.Equals(TEXT("Collapsed"), ESearchCase::IgnoreCase))
		{
			TargetWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
		else if (VisibilityStr.Equals(TEXT("Hidden"), ESearchCase::IgnoreCase))
		{
			TargetWidget->SetVisibility(ESlateVisibility::Hidden);
		}
		else if (VisibilityStr.Equals(TEXT("HitTestInvisible"), ESearchCase::IgnoreCase))
		{
			TargetWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else if (VisibilityStr.Equals(TEXT("SelfHitTestInvisible"), ESearchCase::IgnoreCase))
		{
			TargetWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		AppliedProps.Add(TEXT("visibility"));
	}

	// --- IsEnabled ---
	bool bIsEnabled = true;
	if (Params->TryGetBoolField(TEXT("is_enabled"), bIsEnabled))
	{
		TargetWidget->SetIsEnabled(bIsEnabled);
		AppliedProps.Add(TEXT("is_enabled"));
	}

	// --- Slot-specific properties ---
	UPanelSlot* Slot = TargetWidget->Slot;

	// CanvasPanelSlot: position, size, anchors, alignment
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		FVector2D Position;
		if (TryGetVector2Field(Params, TEXT("position"), Position))
		{
			CanvasSlot->SetPosition(Position);
			AppliedProps.Add(TEXT("position"));
		}

		FVector2D Size;
		if (TryGetVector2Field(Params, TEXT("size"), Size))
		{
			CanvasSlot->SetSize(Size);
			CanvasSlot->SetAutoSize(false);
			AppliedProps.Add(TEXT("size"));
		}

		bool bAutoSize = false;
		if (Params->TryGetBoolField(TEXT("auto_size"), bAutoSize))
		{
			CanvasSlot->SetAutoSize(bAutoSize);
			AppliedProps.Add(TEXT("auto_size"));
		}

		int32 ZOrder = 0;
		if (Params->TryGetNumberField(TEXT("z_order"), ZOrder))
		{
			CanvasSlot->SetZOrder(ZOrder);
			AppliedProps.Add(TEXT("z_order"));
		}

		FVector2D Alignment;
		if (TryGetVector2Field(Params, TEXT("alignment"), Alignment))
		{
			CanvasSlot->SetAlignment(Alignment);
			AppliedProps.Add(TEXT("alignment"));
		}

		// Anchors: [MinX, MinY, MaxX, MaxY]
		FLinearColor AnchorValues;
		if (TryGetColorField(Params, TEXT("anchors"), AnchorValues))
		{
			FAnchors Anchors(AnchorValues.R, AnchorValues.G, AnchorValues.B, AnchorValues.A);
			CanvasSlot->SetAnchors(Anchors);
			AppliedProps.Add(TEXT("anchors"));
		}
	}

	// VerticalBoxSlot: padding, h-align, v-align, size rule
	if (UVerticalBoxSlot* VBoxSlot = Cast<UVerticalBoxSlot>(Slot))
	{
		FLinearColor PaddingValues;
		if (TryGetColorField(Params, TEXT("padding"), PaddingValues))
		{
			// [Left, Top, Right, Bottom]
			FMargin Margin(PaddingValues.R, PaddingValues.G, PaddingValues.B, PaddingValues.A);
			VBoxSlot->SetPadding(Margin);
			AppliedProps.Add(TEXT("padding"));
		}

		FString HAlign;
		if (Params->TryGetStringField(TEXT("h_align"), HAlign))
		{
			if (HAlign.Equals(TEXT("Fill"), ESearchCase::IgnoreCase)) VBoxSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			else if (HAlign.Equals(TEXT("Left"), ESearchCase::IgnoreCase)) VBoxSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Left);
			else if (HAlign.Equals(TEXT("Center"), ESearchCase::IgnoreCase)) VBoxSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
			else if (HAlign.Equals(TEXT("Right"), ESearchCase::IgnoreCase)) VBoxSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Right);
			AppliedProps.Add(TEXT("h_align"));
		}

		FString VAlign;
		if (Params->TryGetStringField(TEXT("v_align"), VAlign))
		{
			if (VAlign.Equals(TEXT("Fill"), ESearchCase::IgnoreCase)) VBoxSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
			else if (VAlign.Equals(TEXT("Top"), ESearchCase::IgnoreCase)) VBoxSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Top);
			else if (VAlign.Equals(TEXT("Center"), ESearchCase::IgnoreCase)) VBoxSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
			else if (VAlign.Equals(TEXT("Bottom"), ESearchCase::IgnoreCase)) VBoxSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Bottom);
			AppliedProps.Add(TEXT("v_align"));
		}

		FString SizeRule;
		if (Params->TryGetStringField(TEXT("size_rule"), SizeRule))
		{
			FSlateChildSize ChildSize;
			if (SizeRule.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
			{
				ChildSize.SizeRule = ESlateSizeRule::Automatic;
			}
			else if (SizeRule.Equals(TEXT("Fill"), ESearchCase::IgnoreCase))
			{
				ChildSize.SizeRule = ESlateSizeRule::Fill;
			}
			VBoxSlot->SetSize(ChildSize);
			AppliedProps.Add(TEXT("size_rule"));
		}
	}

	// HorizontalBoxSlot: padding, h-align, v-align, size rule
	if (UHorizontalBoxSlot* HBoxSlot = Cast<UHorizontalBoxSlot>(Slot))
	{
		FLinearColor PaddingValues;
		if (TryGetColorField(Params, TEXT("padding"), PaddingValues))
		{
			FMargin Margin(PaddingValues.R, PaddingValues.G, PaddingValues.B, PaddingValues.A);
			HBoxSlot->SetPadding(Margin);
			AppliedProps.Add(TEXT("padding"));
		}

		FString HAlign;
		if (Params->TryGetStringField(TEXT("h_align"), HAlign))
		{
			if (HAlign.Equals(TEXT("Fill"), ESearchCase::IgnoreCase)) HBoxSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			else if (HAlign.Equals(TEXT("Left"), ESearchCase::IgnoreCase)) HBoxSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Left);
			else if (HAlign.Equals(TEXT("Center"), ESearchCase::IgnoreCase)) HBoxSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
			else if (HAlign.Equals(TEXT("Right"), ESearchCase::IgnoreCase)) HBoxSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Right);
			AppliedProps.Add(TEXT("h_align"));
		}

		FString VAlign;
		if (Params->TryGetStringField(TEXT("v_align"), VAlign))
		{
			if (VAlign.Equals(TEXT("Fill"), ESearchCase::IgnoreCase)) HBoxSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
			else if (VAlign.Equals(TEXT("Top"), ESearchCase::IgnoreCase)) HBoxSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Top);
			else if (VAlign.Equals(TEXT("Center"), ESearchCase::IgnoreCase)) HBoxSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
			else if (VAlign.Equals(TEXT("Bottom"), ESearchCase::IgnoreCase)) HBoxSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Bottom);
			AppliedProps.Add(TEXT("v_align"));
		}

		FString SizeRule;
		if (Params->TryGetStringField(TEXT("size_rule"), SizeRule))
		{
			FSlateChildSize ChildSize;
			if (SizeRule.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
			{
				ChildSize.SizeRule = ESlateSizeRule::Automatic;
			}
			else if (SizeRule.Equals(TEXT("Fill"), ESearchCase::IgnoreCase))
			{
				ChildSize.SizeRule = ESlateSizeRule::Fill;
			}
			HBoxSlot->SetSize(ChildSize);
			AppliedProps.Add(TEXT("size_rule"));
		}
	}

	// OverlaySlot: padding, h-align, v-align
	if (UOverlaySlot* OvSlot = Cast<UOverlaySlot>(Slot))
	{
		FLinearColor PaddingValues;
		if (TryGetColorField(Params, TEXT("padding"), PaddingValues))
		{
			FMargin Margin(PaddingValues.R, PaddingValues.G, PaddingValues.B, PaddingValues.A);
			OvSlot->SetPadding(Margin);
			AppliedProps.Add(TEXT("padding"));
		}

		FString HAlign;
		if (Params->TryGetStringField(TEXT("h_align"), HAlign))
		{
			if (HAlign.Equals(TEXT("Fill"), ESearchCase::IgnoreCase)) OvSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			else if (HAlign.Equals(TEXT("Left"), ESearchCase::IgnoreCase)) OvSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Left);
			else if (HAlign.Equals(TEXT("Center"), ESearchCase::IgnoreCase)) OvSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
			else if (HAlign.Equals(TEXT("Right"), ESearchCase::IgnoreCase)) OvSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Right);
			AppliedProps.Add(TEXT("h_align"));
		}

		FString VAlign;
		if (Params->TryGetStringField(TEXT("v_align"), VAlign))
		{
			if (VAlign.Equals(TEXT("Fill"), ESearchCase::IgnoreCase)) OvSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
			else if (VAlign.Equals(TEXT("Top"), ESearchCase::IgnoreCase)) OvSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Top);
			else if (VAlign.Equals(TEXT("Center"), ESearchCase::IgnoreCase)) OvSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
			else if (VAlign.Equals(TEXT("Bottom"), ESearchCase::IgnoreCase)) OvSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Bottom);
			AppliedProps.Add(TEXT("v_align"));
		}
	}

	// =================================================================
	// Type-specific widget properties
	// =================================================================

	// Image: brush_texture (set texture asset on Image widget)
	FString BrushTexture;
	if (Params->TryGetStringField(TEXT("brush_texture"), BrushTexture))
	{
		if (UImage* ImageWidget = Cast<UImage>(TargetWidget))
		{
			UObject* TextureObj = UEditorAssetLibrary::LoadAsset(BrushTexture);
			if (UTexture2D* Texture = Cast<UTexture2D>(TextureObj))
			{
				ImageWidget->SetBrushFromTexture(Texture);
				AppliedProps.Add(TEXT("brush_texture"));
			}
		}
	}

	// Image: brush_size
	FVector2D BrushSize;
	if (TryGetVector2Field(Params, TEXT("brush_size"), BrushSize))
	{
		if (UImage* ImageWidget = Cast<UImage>(TargetWidget))
		{
			FSlateBrush CurrentBrush = ImageWidget->GetBrush();
			CurrentBrush.ImageSize = BrushSize;
			ImageWidget->SetBrush(CurrentBrush);
			AppliedProps.Add(TEXT("brush_size"));
		}
	}

	// Image: color_and_opacity
	{
		FLinearColor ImgColor;
		if (TryGetColorField(Params, TEXT("color_and_opacity"), ImgColor))
		{
			if (UImage* ImageWidget = Cast<UImage>(TargetWidget))
			{
				ImageWidget->SetColorAndOpacity(ImgColor);
				AppliedProps.Add(TEXT("color_and_opacity"));
			}
		}
	}

	// Button: normal/hovered/pressed tint colors
	{
		FLinearColor BtnNormalColor;
		if (TryGetColorField(Params, TEXT("button_normal_color"), BtnNormalColor))
		{
			if (UButton* ButtonWidget = Cast<UButton>(TargetWidget))
			{
				FButtonStyle Style = ButtonWidget->GetStyle();
				Style.Normal.TintColor = FSlateColor(BtnNormalColor);
				ButtonWidget->SetStyle(Style);
				AppliedProps.Add(TEXT("button_normal_color"));
			}
		}

		FLinearColor BtnHoveredColor;
		if (TryGetColorField(Params, TEXT("button_hovered_color"), BtnHoveredColor))
		{
			if (UButton* ButtonWidget = Cast<UButton>(TargetWidget))
			{
				FButtonStyle Style = ButtonWidget->GetStyle();
				Style.Hovered.TintColor = FSlateColor(BtnHoveredColor);
				ButtonWidget->SetStyle(Style);
				AppliedProps.Add(TEXT("button_hovered_color"));
			}
		}

		FLinearColor BtnPressedColor;
		if (TryGetColorField(Params, TEXT("button_pressed_color"), BtnPressedColor))
		{
			if (UButton* ButtonWidget = Cast<UButton>(TargetWidget))
			{
				FButtonStyle Style = ButtonWidget->GetStyle();
				Style.Pressed.TintColor = FSlateColor(BtnPressedColor);
				ButtonWidget->SetStyle(Style);
				AppliedProps.Add(TEXT("button_pressed_color"));
			}
		}
	}

	// WidgetSwitcher: active_widget_index
	{
		int32 ActiveIndex = 0;
		if (Params->TryGetNumberField(TEXT("active_widget_index"), ActiveIndex))
		{
			if (UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(TargetWidget))
			{
				Switcher->SetActiveWidgetIndex(ActiveIndex);
				AppliedProps.Add(TEXT("active_widget_index"));
			}
		}
	}

	// BackgroundBlur: blur_strength
	{
		double BlurStrength = 0;
		if (Params->TryGetNumberField(TEXT("blur_strength"), BlurStrength))
		{
			if (UBackgroundBlur* BlurWidget = Cast<UBackgroundBlur>(TargetWidget))
			{
				BlurWidget->SetBlurStrength(static_cast<float>(BlurStrength));
				AppliedProps.Add(TEXT("blur_strength"));
			}
		}
	}

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), BlueprintName);
	ResultObj->SetStringField(TEXT("target"), TargetName);
	ResultObj->SetStringField(TEXT("target_class"), TargetWidget->GetClass()->GetName());

	FString SlotType = TEXT("none");
	if (Slot)
	{
		SlotType = Slot->GetClass()->GetName();
	}
	ResultObj->SetStringField(TEXT("slot_type"), SlotType);

	TArray<TSharedPtr<FJsonValue>> AppliedArr;
	for (const FString& Prop : AppliedProps)
	{
		AppliedArr.Add(MakeShared<FJsonValueString>(Prop));
	}
	ResultObj->SetArrayField(TEXT("applied_properties"), AppliedArr);
	return ResultObj;
}

// =============================================================================
// FGetWidgetTreeAction
// =============================================================================

bool FGetWidgetTreeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	return true;
}

static TSharedPtr<FJsonObject> BuildWidgetNodeJson(UWidget* Widget, UWidgetTree* WidgetTree)
{
	if (!Widget)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
	NodeObj->SetStringField(TEXT("name"), Widget->GetName());
	NodeObj->SetStringField(TEXT("class"), Widget->GetClass()->GetName());
	NodeObj->SetBoolField(TEXT("is_visible"), Widget->IsVisible());

	// RenderTransform info
	FWidgetTransform RT = Widget->GetRenderTransform();
	{
		TSharedPtr<FJsonObject> TransformObj = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> TransArr;
		TransArr.Add(MakeShared<FJsonValueNumber>(RT.Translation.X));
		TransArr.Add(MakeShared<FJsonValueNumber>(RT.Translation.Y));
		TransformObj->SetArrayField(TEXT("translation"), TransArr);

		TArray<TSharedPtr<FJsonValue>> ScaleArr;
		ScaleArr.Add(MakeShared<FJsonValueNumber>(RT.Scale.X));
		ScaleArr.Add(MakeShared<FJsonValueNumber>(RT.Scale.Y));
		TransformObj->SetArrayField(TEXT("scale"), ScaleArr);

		TArray<TSharedPtr<FJsonValue>> ShearArr;
		ShearArr.Add(MakeShared<FJsonValueNumber>(RT.Shear.X));
		ShearArr.Add(MakeShared<FJsonValueNumber>(RT.Shear.Y));
		TransformObj->SetArrayField(TEXT("shear"), ShearArr);

		TransformObj->SetNumberField(TEXT("angle"), RT.Angle);
		NodeObj->SetObjectField(TEXT("render_transform"), TransformObj);
	}

	// Slot info
	UPanelSlot* Slot = Widget->Slot;
	if (Slot)
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
		SlotObj->SetStringField(TEXT("type"), Slot->GetClass()->GetName());

		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
		{
			FVector2D Pos = CanvasSlot->GetPosition();
			FVector2D Sz = CanvasSlot->GetSize();
			TArray<TSharedPtr<FJsonValue>> PosArr;
			PosArr.Add(MakeShared<FJsonValueNumber>(Pos.X));
			PosArr.Add(MakeShared<FJsonValueNumber>(Pos.Y));
			SlotObj->SetArrayField(TEXT("position"), PosArr);

			TArray<TSharedPtr<FJsonValue>> SzArr;
			SzArr.Add(MakeShared<FJsonValueNumber>(Sz.X));
			SzArr.Add(MakeShared<FJsonValueNumber>(Sz.Y));
			SlotObj->SetArrayField(TEXT("size"), SzArr);

			SlotObj->SetBoolField(TEXT("auto_size"), CanvasSlot->GetAutoSize());
			SlotObj->SetNumberField(TEXT("z_order"), CanvasSlot->GetZOrder());

			FAnchors Anchors = CanvasSlot->GetAnchors();
			TArray<TSharedPtr<FJsonValue>> AnchorArr;
			AnchorArr.Add(MakeShared<FJsonValueNumber>(Anchors.Minimum.X));
			AnchorArr.Add(MakeShared<FJsonValueNumber>(Anchors.Minimum.Y));
			AnchorArr.Add(MakeShared<FJsonValueNumber>(Anchors.Maximum.X));
			AnchorArr.Add(MakeShared<FJsonValueNumber>(Anchors.Maximum.Y));
			SlotObj->SetArrayField(TEXT("anchors"), AnchorArr);

			FVector2D Alignment = CanvasSlot->GetAlignment();
			TArray<TSharedPtr<FJsonValue>> AlignArr;
			AlignArr.Add(MakeShared<FJsonValueNumber>(Alignment.X));
			AlignArr.Add(MakeShared<FJsonValueNumber>(Alignment.Y));
			SlotObj->SetArrayField(TEXT("alignment"), AlignArr);
		}

		if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(Slot))
		{
			FMargin Pad = VSlot->GetPadding();
			TArray<TSharedPtr<FJsonValue>> PadArr;
			PadArr.Add(MakeShared<FJsonValueNumber>(Pad.Left));
			PadArr.Add(MakeShared<FJsonValueNumber>(Pad.Top));
			PadArr.Add(MakeShared<FJsonValueNumber>(Pad.Right));
			PadArr.Add(MakeShared<FJsonValueNumber>(Pad.Bottom));
			SlotObj->SetArrayField(TEXT("padding"), PadArr);
		}

		if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(Slot))
		{
			FMargin Pad = HSlot->GetPadding();
			TArray<TSharedPtr<FJsonValue>> PadArr;
			PadArr.Add(MakeShared<FJsonValueNumber>(Pad.Left));
			PadArr.Add(MakeShared<FJsonValueNumber>(Pad.Top));
			PadArr.Add(MakeShared<FJsonValueNumber>(Pad.Right));
			PadArr.Add(MakeShared<FJsonValueNumber>(Pad.Bottom));
			SlotObj->SetArrayField(TEXT("padding"), PadArr);
		}

		if (UOverlaySlot* OSlot = Cast<UOverlaySlot>(Slot))
		{
			FMargin Pad = OSlot->GetPadding();
			TArray<TSharedPtr<FJsonValue>> PadArr;
			PadArr.Add(MakeShared<FJsonValueNumber>(Pad.Left));
			PadArr.Add(MakeShared<FJsonValueNumber>(Pad.Top));
			PadArr.Add(MakeShared<FJsonValueNumber>(Pad.Right));
			PadArr.Add(MakeShared<FJsonValueNumber>(Pad.Bottom));
			SlotObj->SetArrayField(TEXT("padding"), PadArr);
		}

		NodeObj->SetObjectField(TEXT("slot"), SlotObj);
	}

	// Children
	UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
	if (Panel)
	{
		TArray<TSharedPtr<FJsonValue>> ChildArr;
		for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
		{
			UWidget* Child = Panel->GetChildAt(i);
			TSharedPtr<FJsonObject> ChildNode = BuildWidgetNodeJson(Child, WidgetTree);
			if (ChildNode)
			{
				ChildArr.Add(MakeShared<FJsonValueObject>(ChildNode));
			}
		}
		if (ChildArr.Num() > 0)
		{
			NodeObj->SetArrayField(TEXT("children"), ChildArr);
		}
	}

	return NodeObj;
}

TSharedPtr<FJsonObject> FGetWidgetTreeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	if (!WidgetBlueprint->WidgetTree || !WidgetBlueprint->WidgetTree->RootWidget)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Widget Blueprint has no root widget"));
	}

	TSharedPtr<FJsonObject> TreeNode = BuildWidgetNodeJson(WidgetBlueprint->WidgetTree->RootWidget, WidgetBlueprint->WidgetTree);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), BlueprintName);
	ResultObj->SetObjectField(TEXT("tree"), TreeNode);
	return ResultObj;
}

// =============================================================================
// FDeleteWidgetFromBlueprintAction
// =============================================================================

bool FDeleteWidgetFromBlueprintAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("target")))
	{
		OutError = TEXT("Missing 'target' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FDeleteWidgetFromBlueprintAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString TargetName = Params->GetStringField(TEXT("target"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	UWidget* TargetWidget = WidgetBlueprint->WidgetTree->FindWidget(FName(*TargetName));
	if (!TargetWidget)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget '%s' not found in '%s'"), *TargetName, *BlueprintName));
	}

	// Cannot delete root widget
	if (TargetWidget == WidgetBlueprint->WidgetTree->RootWidget)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Cannot delete the root widget"));
	}

	// Remove from parent
	TargetWidget->RemoveFromParent();

	// Remove widget and its children from the widget tree
	WidgetBlueprint->WidgetTree->RemoveWidget(TargetWidget);

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), BlueprintName);
	ResultObj->SetStringField(TEXT("deleted_widget"), TargetName);
	return ResultObj;
}

// =============================================================================
// FRenameWidgetInBlueprintAction
// =============================================================================

bool FRenameWidgetInBlueprintAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("target")))
	{
		OutError = TEXT("Missing 'target' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("new_name")))
	{
		OutError = TEXT("Missing 'new_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FRenameWidgetInBlueprintAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString TargetName = Params->GetStringField(TEXT("target"));
	FString NewName = Params->GetStringField(TEXT("new_name"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	UWidget* TargetWidget = WidgetBlueprint->WidgetTree->FindWidget(FName(*TargetName));
	if (!TargetWidget)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget '%s' not found in '%s'"), *TargetName, *BlueprintName));
	}

	// Check if new name already exists
	UWidget* ExistingWidget = WidgetBlueprint->WidgetTree->FindWidget(FName(*NewName));
	if (ExistingWidget && ExistingWidget != TargetWidget)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("A widget named '%s' already exists in '%s'"), *NewName, *BlueprintName));
	}

	FString OldName = TargetWidget->GetName();
	TargetWidget->Rename(*NewName, WidgetBlueprint->WidgetTree);

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), BlueprintName);
	ResultObj->SetStringField(TEXT("old_name"), OldName);
	ResultObj->SetStringField(TEXT("new_name"), TargetWidget->GetName());
	return ResultObj;
}

// =============================================================================
// FAddWidgetChildAction
// =============================================================================

bool FAddWidgetChildAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("child")))
	{
		OutError = TEXT("Missing 'child' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("parent")))
	{
		OutError = TEXT("Missing 'parent' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddWidgetChildAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString ChildName = Params->GetStringField(TEXT("child"));
	FString ParentName = Params->GetStringField(TEXT("parent"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	UWidget* ChildWidget = WidgetBlueprint->WidgetTree->FindWidget(FName(*ChildName));
	if (!ChildWidget)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Child widget '%s' not found in '%s'"), *ChildName, *BlueprintName));
	}

	UWidget* ParentWidget = WidgetBlueprint->WidgetTree->FindWidget(FName(*ParentName));
	if (!ParentWidget)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Parent widget '%s' not found in '%s'"), *ParentName, *BlueprintName));
	}

	UPanelWidget* ParentPanel = Cast<UPanelWidget>(ParentWidget);
	if (!ParentPanel)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget '%s' is not a container (PanelWidget) and cannot have children"), *ParentName));
	}

	// Avoid circular parenting
	if (ChildWidget == ParentWidget)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Cannot parent a widget to itself"));
	}

	// Remove child from its current parent
	ChildWidget->RemoveFromParent();

	// Add to new parent
	UPanelSlot* Slot = ParentPanel->AddChild(ChildWidget);
	if (!Slot)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Failed to add '%s' as child of '%s'"), *ChildName, *ParentName));
	}

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), BlueprintName);
	ResultObj->SetStringField(TEXT("child"), ChildWidget->GetName());
	ResultObj->SetStringField(TEXT("parent"), ParentPanel->GetName());
	ResultObj->SetStringField(TEXT("parent_class"), ParentPanel->GetClass()->GetName());
	return ResultObj;
}

// =============================================================================
// FDeleteUMGWidgetBlueprintAction
// =============================================================================

bool FDeleteUMGWidgetBlueprintAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FDeleteUMGWidgetBlueprintAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	FString AssetPath = WidgetBlueprint->GetPathName();
	// Get the package path (without object name)
	FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);

	bool bDeleted = UEditorAssetLibrary::DeleteAsset(PackagePath);
	if (!bDeleted)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Failed to delete Widget Blueprint '%s' at path '%s'"), *BlueprintName, *PackagePath));
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("deleted_asset"), PackagePath);
	return ResultObj;
}


// =============================================================================
// FSetComboBoxOptionsAction
// =============================================================================

bool FSetComboBoxOptionsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("target")))
	{
		OutError = TEXT("Missing 'target' parameter (name of ComboBoxString widget)");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FSetComboBoxOptionsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString TargetName = Params->GetStringField(TEXT("target"));
	FString Mode = GetOptionalString(Params, TEXT("mode"), TEXT("replace"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	UWidget* TargetWidget = WidgetBlueprint->WidgetTree->FindWidget(FName(*TargetName));
	if (!TargetWidget)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget '%s' not found in '%s'"), *TargetName, *BlueprintName));
	}

	UComboBoxString* ComboBox = Cast<UComboBoxString>(TargetWidget);
	if (!ComboBox)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget '%s' is not a ComboBoxString (actual: %s)"), *TargetName, *TargetWidget->GetClass()->GetName()));
	}

	const TArray<TSharedPtr<FJsonValue>>* OptionsArray = nullptr;
	Params->TryGetArrayField(TEXT("options"), OptionsArray);

	if (Mode.Equals(TEXT("replace"), ESearchCase::IgnoreCase))
	{
		// Clear existing and add new
		ComboBox->ClearOptions();
		if (OptionsArray)
		{
			for (const TSharedPtr<FJsonValue>& Val : *OptionsArray)
			{
				ComboBox->AddOption(Val->AsString());
			}
		}
	}
	else if (Mode.Equals(TEXT("add"), ESearchCase::IgnoreCase))
	{
		// Add to existing
		if (OptionsArray)
		{
			for (const TSharedPtr<FJsonValue>& Val : *OptionsArray)
			{
				ComboBox->AddOption(Val->AsString());
			}
		}
	}
	else if (Mode.Equals(TEXT("remove"), ESearchCase::IgnoreCase))
	{
		// Remove specified options
		if (OptionsArray)
		{
			for (const TSharedPtr<FJsonValue>& Val : *OptionsArray)
			{
				ComboBox->RemoveOption(Val->AsString());
			}
		}
	}
	else if (Mode.Equals(TEXT("clear"), ESearchCase::IgnoreCase))
	{
		ComboBox->ClearOptions();
	}

	// Set selected option if specified
	FString SelectedOption;
	if (Params->TryGetStringField(TEXT("selected_option"), SelectedOption))
	{
		ComboBox->SetSelectedOption(SelectedOption);
	}

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), TargetName);
	ResultObj->SetStringField(TEXT("mode"), Mode);
	ResultObj->SetNumberField(TEXT("option_count"), ComboBox->GetOptionCount());
	return ResultObj;
}


// =============================================================================
// FSetWidgetTextAction
// =============================================================================

bool FSetWidgetTextAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("target")))
	{
		OutError = TEXT("Missing 'target' parameter (name of TextBlock or Button widget)");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FSetWidgetTextAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString TargetName = Params->GetStringField(TEXT("target"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	UWidget* TargetWidget = WidgetBlueprint->WidgetTree->FindWidget(FName(*TargetName));
	if (!TargetWidget)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget '%s' not found in '%s'"), *TargetName, *BlueprintName));
	}

	TArray<FString> AppliedProps;

	// Handle TextBlock
	if (UTextBlock* TextBlock = Cast<UTextBlock>(TargetWidget))
	{
		FString Text;
		if (Params->TryGetStringField(TEXT("text"), Text))
		{
			TextBlock->SetText(FText::FromString(Text));
			AppliedProps.Add(TEXT("text"));
		}

		double FontSize = 0;
		if (Params->TryGetNumberField(TEXT("font_size"), FontSize))
		{
			FSlateFontInfo FontInfo = TextBlock->GetFont();
			FontInfo.Size = static_cast<int32>(FontSize);
			TextBlock->SetFont(FontInfo);
			AppliedProps.Add(TEXT("font_size"));
		}

		FLinearColor Color;
		if (TryGetColorField(Params, TEXT("color"), Color))
		{
			TextBlock->SetColorAndOpacity(FSlateColor(Color));
			AppliedProps.Add(TEXT("color"));
		}

		FString Justification;
		if (Params->TryGetStringField(TEXT("justification"), Justification))
		{
			if (Justification.Equals(TEXT("Left"), ESearchCase::IgnoreCase))
			{
				TextBlock->SetJustification(ETextJustify::Left);
			}
			else if (Justification.Equals(TEXT("Center"), ESearchCase::IgnoreCase))
			{
				TextBlock->SetJustification(ETextJustify::Center);
			}
			else if (Justification.Equals(TEXT("Right"), ESearchCase::IgnoreCase))
			{
				TextBlock->SetJustification(ETextJustify::Right);
			}
			AppliedProps.Add(TEXT("justification"));
		}
	}
	// Handle Button (look for child TextBlock)
	else if (UButton* Button = Cast<UButton>(TargetWidget))
	{
		FString Text;
		if (Params->TryGetStringField(TEXT("text"), Text))
		{
			// Find child TextBlock in button
			UPanelWidget* Panel = Cast<UPanelWidget>(Button);
			if (Panel)
			{
				for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
				{
					if (UTextBlock* ChildText = Cast<UTextBlock>(Panel->GetChildAt(i)))
					{
						ChildText->SetText(FText::FromString(Text));
						AppliedProps.Add(TEXT("text"));
						break;
					}
				}
			}
		}

		FLinearColor BGColor;
		if (TryGetColorField(Params, TEXT("background_color"), BGColor))
		{
			Button->SetBackgroundColor(BGColor);
			AppliedProps.Add(TEXT("background_color"));
		}
	}
	else
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget '%s' is not a TextBlock or Button (actual: %s)"), *TargetName, *TargetWidget->GetClass()->GetName()));
	}

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), TargetName);
	TArray<TSharedPtr<FJsonValue>> AppliedArray;
	for (const FString& Prop : AppliedProps)
	{
		AppliedArray.Add(MakeShared<FJsonValueString>(Prop));
	}
	ResultObj->SetArrayField(TEXT("applied_properties"), AppliedArray);
	return ResultObj;
}


// =============================================================================
// FSetSliderPropertiesAction
// =============================================================================

bool FSetSliderPropertiesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("target")))
	{
		OutError = TEXT("Missing 'target' parameter (name of Slider widget)");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FSetSliderPropertiesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString TargetName = Params->GetStringField(TEXT("target"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	UWidget* TargetWidget = WidgetBlueprint->WidgetTree->FindWidget(FName(*TargetName));
	if (!TargetWidget)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget '%s' not found in '%s'"), *TargetName, *BlueprintName));
	}

	USlider* Slider = Cast<USlider>(TargetWidget);
	if (!Slider)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget '%s' is not a Slider (actual: %s)"), *TargetName, *TargetWidget->GetClass()->GetName()));
	}

	TArray<FString> AppliedProps;

	double Value = 0;
	if (Params->TryGetNumberField(TEXT("value"), Value))
	{
		Slider->SetValue(static_cast<float>(Value));
		AppliedProps.Add(TEXT("value"));
	}

	double MinValue = 0;
	if (Params->TryGetNumberField(TEXT("min_value"), MinValue))
	{
		Slider->SetMinValue(static_cast<float>(MinValue));
		AppliedProps.Add(TEXT("min_value"));
	}

	double MaxValue = 0;
	if (Params->TryGetNumberField(TEXT("max_value"), MaxValue))
	{
		Slider->SetMaxValue(static_cast<float>(MaxValue));
		AppliedProps.Add(TEXT("max_value"));
	}

	double StepSize = 0;
	if (Params->TryGetNumberField(TEXT("step_size"), StepSize))
	{
		Slider->SetStepSize(static_cast<float>(StepSize));
		AppliedProps.Add(TEXT("step_size"));
	}

	bool bLocked = false;
	if (Params->TryGetBoolField(TEXT("locked"), bLocked))
	{
		Slider->SetLocked(bLocked);
		AppliedProps.Add(TEXT("locked"));
	}

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), TargetName);
	TArray<TSharedPtr<FJsonValue>> AppliedArray;
	for (const FString& Prop : AppliedProps)
	{
		AppliedArray.Add(MakeShared<FJsonValueString>(Prop));
	}
	ResultObj->SetArrayField(TEXT("applied_properties"), AppliedArray);
	return ResultObj;
}

// =============================================================================
// FAddGenericWidgetAction
// =============================================================================

UClass* FAddGenericWidgetAction::ResolveWidgetClass(const FString& ClassName) const
{
	if (ClassName == TEXT("ScrollBox")) return UScrollBox::StaticClass();
	if (ClassName == TEXT("WidgetSwitcher")) return UWidgetSwitcher::StaticClass();
	if (ClassName == TEXT("BackgroundBlur")) return UBackgroundBlur::StaticClass();
	if (ClassName == TEXT("UniformGridPanel")) return UUniformGridPanel::StaticClass();
	if (ClassName == TEXT("Spacer")) return USpacer::StaticClass();
	if (ClassName == TEXT("RichTextBlock")) return URichTextBlock::StaticClass();
	if (ClassName == TEXT("WrapBox")) return UWrapBox::StaticClass();
	if (ClassName == TEXT("CircularThrobber")) return UCircularThrobber::StaticClass();
	return nullptr;
}

bool FAddGenericWidgetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("component_name")))
	{
		OutError = TEXT("Missing 'component_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("component_class")))
	{
		OutError = TEXT("Missing 'component_class' parameter");
		return false;
	}
	FString ClassName = Params->GetStringField(TEXT("component_class"));
	if (!ResolveWidgetClass(ClassName))
	{
		OutError = FString::Printf(TEXT("Unknown component_class: %s. Supported: ScrollBox, WidgetSwitcher, BackgroundBlur, UniformGridPanel, Spacer, RichTextBlock, WrapBox, CircularThrobber"), *ClassName);
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddGenericWidgetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetName = Params->GetStringField(TEXT("component_name"));
	FString ClassName = Params->GetStringField(TEXT("component_class"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found in /Game/UI, /Game/Widgets, or /Game"), *BlueprintName));
	}

	UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(WidgetBlueprint->WidgetTree->RootWidget);
	if (!RootCanvas)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Root widget is not a Canvas Panel"));
	}

	UClass* WidgetClass = ResolveWidgetClass(ClassName);
	if (!WidgetClass)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Unknown component class: %s"), *ClassName));
	}

	UWidget* NewWidget = WidgetBlueprint->WidgetTree->ConstructWidget<UWidget>(WidgetClass, *WidgetName);
	if (!NewWidget)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Failed to create %s widget"), *ClassName));
	}

	// Apply type-specific initialization properties
	if (UBackgroundBlur* Blur = Cast<UBackgroundBlur>(NewWidget))
	{
		double BlurStrength = 10.0;
		Params->TryGetNumberField(TEXT("blur_strength"), BlurStrength);
		Blur->SetBlurStrength(static_cast<float>(BlurStrength));
	}

	if (URichTextBlock* RichText = Cast<URichTextBlock>(NewWidget))
	{
		FString Text;
		if (Params->TryGetStringField(TEXT("text"), Text))
		{
			RichText->SetText(FText::FromString(Text));
		}
	}

	if (UScrollBox* ScrollBox = Cast<UScrollBox>(NewWidget))
	{
		FString Orientation;
		if (Params->TryGetStringField(TEXT("orientation"), Orientation))
		{
			if (Orientation.Equals(TEXT("Horizontal"), ESearchCase::IgnoreCase))
			{
				ScrollBox->SetOrientation(Orient_Horizontal);
			}
		}
	}

	if (UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(NewWidget))
	{
		int32 ActiveIndex = 0;
		if (Params->TryGetNumberField(TEXT("active_index"), ActiveIndex))
		{
			Switcher->SetActiveWidgetIndex(ActiveIndex);
		}
	}

	// Add to canvas and apply slot properties
	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(NewWidget);
	ApplyCanvasSlot(Slot, Params);

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	ResultObj->SetStringField(TEXT("widget_class"), ClassName);
	return ResultObj;
}

