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


// =============================================================================
// FMVVMAddViewModelAction
// =============================================================================

bool FMVVMAddViewModelAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("viewmodel_class")))
	{
		OutError = TEXT("Missing 'viewmodel_class' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FMVVMAddViewModelAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString ViewModelClassName = Params->GetStringField(TEXT("viewmodel_class"));
	FString ViewModelName = Params->HasField(TEXT("viewmodel_name"))
		? Params->GetStringField(TEXT("viewmodel_name"))
		: ViewModelClassName;
	FString CreationTypeStr = Params->HasField(TEXT("creation_type"))
		? Params->GetStringField(TEXT("creation_type"))
		: TEXT("CreateInstance");

	// Find Widget Blueprint
	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	// Resolve ViewModel class
	UClass* VMClass = ResolveClassByName(ViewModelClassName);
	if (!VMClass)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("ViewModel class '%s' not found. Ensure it is compiled and loaded."), *ViewModelClassName));
	}

	// Verify it implements INotifyFieldValueChanged
	if (!VMClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass()))
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Class '%s' does not implement INotifyFieldValueChanged. "
			     "It must derive from UMVVMViewModelBase or implement the interface."),
			*VMClass->GetName()));
	}

	// Get or create the MVVM extension on the Widget Blueprint
	UMVVMWidgetBlueprintExtension_View* MVVMExt =
		UWidgetBlueprintExtension::RequestExtension<UMVVMWidgetBlueprintExtension_View>(WidgetBlueprint);
	if (!MVVMExt)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create MVVM extension on Widget Blueprint"));
	}

	// Ensure BlueprintView exists
	UMVVMBlueprintView* BPView = MVVMExt->GetBlueprintView();
	if (!BPView)
	{
		MVVMExt->CreateBlueprintViewInstance();
		BPView = MVVMExt->GetBlueprintView();
	}
	if (!BPView)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create MVVM BlueprintView"));
	}

	// Check if ViewModel already exists with this name
	FName VMFName(*ViewModelName);
	const FMVVMBlueprintViewModelContext* ExistingVM = BPView->FindViewModel(VMFName);
	if (ExistingVM)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("ViewModel '%s' already exists on this Widget Blueprint"), *ViewModelName));
	}

	// Create ViewModel context
	FMVVMBlueprintViewModelContext VMContext(VMClass, VMFName);

	// Set creation type
	if (CreationTypeStr.Equals(TEXT("Manual"), ESearchCase::IgnoreCase))
	{
		VMContext.CreationType = EMVVMBlueprintViewModelContextCreationType::Manual;
	}
	else if (CreationTypeStr.Equals(TEXT("CreateInstance"), ESearchCase::IgnoreCase))
	{
		VMContext.CreationType = EMVVMBlueprintViewModelContextCreationType::CreateInstance;
	}
	else if (CreationTypeStr.Equals(TEXT("GlobalViewModelCollection"), ESearchCase::IgnoreCase))
	{
		VMContext.CreationType = EMVVMBlueprintViewModelContextCreationType::GlobalViewModelCollection;
	}
	else if (CreationTypeStr.Equals(TEXT("PropertyPath"), ESearchCase::IgnoreCase))
	{
		VMContext.CreationType = EMVVMBlueprintViewModelContextCreationType::PropertyPath;
	}
	else if (CreationTypeStr.Equals(TEXT("Resolver"), ESearchCase::IgnoreCase))
	{
		VMContext.CreationType = EMVVMBlueprintViewModelContextCreationType::Resolver;
	}

	// Optional: getter/setter generation
	if (Params->HasField(TEXT("create_setter")))
	{
		VMContext.bCreateSetterFunction = Params->GetBoolField(TEXT("create_setter"));
	}
	if (Params->HasField(TEXT("create_getter")))
	{
		VMContext.bCreateGetterFunction = Params->GetBoolField(TEXT("create_getter"));
	}

	// Add ViewModel
	BPView->AddViewModel(VMContext);

	// Notify MVVM system
	BPView->OnBindingsUpdated.Broadcast();

	// Mark modified and compile
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("viewmodel_name"), ViewModelName);
	ResultObj->SetStringField(TEXT("viewmodel_class"), VMClass->GetName());
	ResultObj->SetStringField(TEXT("viewmodel_id"), VMContext.GetViewModelId().ToString());
	ResultObj->SetStringField(TEXT("creation_type"), CreationTypeStr);
	return ResultObj;
}

// =============================================================================
// FMVVMAddBindingAction
// =============================================================================

bool FMVVMAddBindingAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("viewmodel_name")))
	{
		OutError = TEXT("Missing 'viewmodel_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("source_property")))
	{
		OutError = TEXT("Missing 'source_property' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("destination_widget")))
	{
		OutError = TEXT("Missing 'destination_widget' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("destination_property")))
	{
		OutError = TEXT("Missing 'destination_property' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FMVVMAddBindingAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString ViewModelName = Params->GetStringField(TEXT("viewmodel_name"));
	FString SourcePropName = Params->GetStringField(TEXT("source_property"));
	FString DestWidgetName = Params->GetStringField(TEXT("destination_widget"));
	FString DestPropName = Params->GetStringField(TEXT("destination_property"));
	FString BindingModeStr = Params->HasField(TEXT("binding_mode"))
		? Params->GetStringField(TEXT("binding_mode"))
		: TEXT("OneWayToDestination");
	FString ExecutionModeStr = Params->HasField(TEXT("execution_mode"))
		? Params->GetStringField(TEXT("execution_mode"))
		: TEXT("");

	// Find Widget Blueprint
	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	// Get MVVM extension
	UMVVMWidgetBlueprintExtension_View* MVVMExt =
		UWidgetBlueprintExtension::GetExtension<UMVVMWidgetBlueprintExtension_View>(WidgetBlueprint);
	if (!MVVMExt)
	{
		return FMCPCommonUtils::CreateErrorResponse(
			TEXT("No MVVM extension found on Widget Blueprint. Use widget.mvvm_add_viewmodel first."));
	}

	UMVVMBlueprintView* BPView = MVVMExt->GetBlueprintView();
	if (!BPView)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("No MVVM BlueprintView found."));
	}

	// Find the ViewModel context
	FName VMFName(*ViewModelName);
	const FMVVMBlueprintViewModelContext* VMContext = BPView->FindViewModel(VMFName);
	if (!VMContext)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("ViewModel '%s' not found on this Widget Blueprint"), *ViewModelName));
	}

	UClass* VMClass = VMContext->GetViewModelClass();
	if (!VMClass)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("ViewModel class is null"));
	}

	// Resolve source field on ViewModel (property or FieldNotify function)
	UE::MVVM::FMVVMConstFieldVariant SourceFieldVariant;
	FProperty* SourceProp = VMClass->FindPropertyByName(FName(*SourcePropName));
	if (SourceProp)
	{
		SourceFieldVariant = UE::MVVM::FMVVMConstFieldVariant(SourceProp);
	}
	else
	{
		// Fall back to UFunction (FieldNotify functions like GetHealthPercent)
		UFunction* SourceFunc = VMClass->FindFunctionByName(FName(*SourcePropName));
		if (SourceFunc)
		{
			SourceFieldVariant = UE::MVVM::FMVVMConstFieldVariant(SourceFunc);
		}
		else
		{
			return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Property or function '%s' not found on ViewModel class '%s'"), *SourcePropName, *VMClass->GetName()));
		}
	}

	// Resolve destination widget in the tree
	UWidget* DestWidget = WidgetBlueprint->WidgetTree
		? WidgetBlueprint->WidgetTree->FindWidget(FName(*DestWidgetName))
		: nullptr;
	if (!DestWidget)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget '%s' not found in Widget Tree"), *DestWidgetName));
	}

	// Resolve destination field on the widget class (property or function)
	UE::MVVM::FMVVMConstFieldVariant DestFieldVariant;
	FProperty* DestProp = DestWidget->GetClass()->FindPropertyByName(FName(*DestPropName));
	if (DestProp)
	{
		DestFieldVariant = UE::MVVM::FMVVMConstFieldVariant(DestProp);
	}
	else
	{
		UFunction* DestFunc = DestWidget->GetClass()->FindFunctionByName(FName(*DestPropName));
		if (DestFunc)
		{
			DestFieldVariant = UE::MVVM::FMVVMConstFieldVariant(DestFunc);
		}
		else
		{
			return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Property or function '%s' not found on widget '%s' (class: %s)"),
				*DestPropName, *DestWidgetName, *DestWidget->GetClass()->GetName()));
		}
	}

	// Create binding
	FMVVMBlueprintViewBinding& NewBinding = BPView->AddDefaultBinding();

	// Configure source path (ViewModel side)
	NewBinding.SourcePath.SetViewModelId(VMContext->GetViewModelId());
	NewBinding.SourcePath.SetPropertyPath(WidgetBlueprint, SourceFieldVariant);

	// Configure destination path (Widget side)
	NewBinding.DestinationPath.SetWidgetName(FName(*DestWidgetName));
	NewBinding.DestinationPath.SetPropertyPath(WidgetBlueprint, DestFieldVariant);

	// Parse binding mode
	if (BindingModeStr.Equals(TEXT("OneTimeToDestination"), ESearchCase::IgnoreCase))
	{
		NewBinding.BindingType = EMVVMBindingMode::OneTimeToDestination;
	}
	else if (BindingModeStr.Equals(TEXT("OneWayToDestination"), ESearchCase::IgnoreCase))
	{
		NewBinding.BindingType = EMVVMBindingMode::OneWayToDestination;
	}
	else if (BindingModeStr.Equals(TEXT("TwoWay"), ESearchCase::IgnoreCase))
	{
		NewBinding.BindingType = EMVVMBindingMode::TwoWay;
	}
	else if (BindingModeStr.Equals(TEXT("OneTimeToSource"), ESearchCase::IgnoreCase))
	{
		NewBinding.BindingType = EMVVMBindingMode::OneTimeToSource;
	}
	else if (BindingModeStr.Equals(TEXT("OneWayToSource"), ESearchCase::IgnoreCase))
	{
		NewBinding.BindingType = EMVVMBindingMode::OneWayToSource;
	}

	// Parse execution mode (optional)
	if (!ExecutionModeStr.IsEmpty())
	{
		NewBinding.bOverrideExecutionMode = true;
		if (ExecutionModeStr.Equals(TEXT("Immediate"), ESearchCase::IgnoreCase))
		{
			NewBinding.OverrideExecutionMode = EMVVMExecutionMode::Immediate;
		}
		else if (ExecutionModeStr.Equals(TEXT("Delayed"), ESearchCase::IgnoreCase))
		{
			NewBinding.OverrideExecutionMode = EMVVMExecutionMode::Delayed;
		}
		else if (ExecutionModeStr.Equals(TEXT("Tick"), ESearchCase::IgnoreCase))
		{
			NewBinding.OverrideExecutionMode = EMVVMExecutionMode::Tick;
		}
		else if (ExecutionModeStr.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
		{
			NewBinding.OverrideExecutionMode = EMVVMExecutionMode::DelayedWhenSharedElseImmediate;
		}
	}

	// Notify MVVM system that bindings changed (critical for compilation stability)
	BPView->OnBindingsUpdated.Broadcast();

	// Mark modified
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("binding_id"), NewBinding.BindingId.ToString());
	ResultObj->SetStringField(TEXT("source"), FString::Printf(TEXT("%s.%s"), *ViewModelName, *SourcePropName));
	ResultObj->SetStringField(TEXT("destination"), FString::Printf(TEXT("%s.%s"), *DestWidgetName, *DestPropName));
	ResultObj->SetStringField(TEXT("binding_mode"), BindingModeStr);
	if (!ExecutionModeStr.IsEmpty())
	{
		ResultObj->SetStringField(TEXT("execution_mode"), ExecutionModeStr);
	}
	return ResultObj;
}

// =============================================================================
// FMVVMGetBindingsAction
// =============================================================================

bool FMVVMGetBindingsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FMVVMGetBindingsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));

	// Find Widget Blueprint
	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	// Get MVVM extension
	UMVVMWidgetBlueprintExtension_View* MVVMExt =
		UWidgetBlueprintExtension::GetExtension<UMVVMWidgetBlueprintExtension_View>(WidgetBlueprint);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);

	if (!MVVMExt)
	{
		ResultObj->SetBoolField(TEXT("has_mvvm"), false);
		ResultObj->SetArrayField(TEXT("viewmodels"), TArray<TSharedPtr<FJsonValue>>());
		ResultObj->SetArrayField(TEXT("bindings"), TArray<TSharedPtr<FJsonValue>>());
		return ResultObj;
	}

	UMVVMBlueprintView* BPView = MVVMExt->GetBlueprintView();
	if (!BPView)
	{
		ResultObj->SetBoolField(TEXT("has_mvvm"), false);
		ResultObj->SetArrayField(TEXT("viewmodels"), TArray<TSharedPtr<FJsonValue>>());
		ResultObj->SetArrayField(TEXT("bindings"), TArray<TSharedPtr<FJsonValue>>());
		return ResultObj;
	}

	ResultObj->SetBoolField(TEXT("has_mvvm"), true);

	// Serialize ViewModels
	TArray<TSharedPtr<FJsonValue>> VMArray;
	TArrayView<const FMVVMBlueprintViewModelContext> ViewModels = BPView->GetViewModels();
	for (const FMVVMBlueprintViewModelContext& VM : ViewModels)
	{
		TSharedPtr<FJsonObject> VMObj = MakeShared<FJsonObject>();
		VMObj->SetStringField(TEXT("id"), VM.GetViewModelId().ToString());
		VMObj->SetStringField(TEXT("name"), VM.GetViewModelName().ToString());
		VMObj->SetStringField(TEXT("class"), VM.GetViewModelClass() ? VM.GetViewModelClass()->GetName() : TEXT("null"));

		FString CreationStr;
		switch (VM.CreationType)
		{
		case EMVVMBlueprintViewModelContextCreationType::Manual:
			CreationStr = TEXT("Manual"); break;
		case EMVVMBlueprintViewModelContextCreationType::CreateInstance:
			CreationStr = TEXT("CreateInstance"); break;
		case EMVVMBlueprintViewModelContextCreationType::GlobalViewModelCollection:
			CreationStr = TEXT("GlobalViewModelCollection"); break;
		case EMVVMBlueprintViewModelContextCreationType::PropertyPath:
			CreationStr = TEXT("PropertyPath"); break;
		case EMVVMBlueprintViewModelContextCreationType::Resolver:
			CreationStr = TEXT("Resolver"); break;
		default:
			CreationStr = TEXT("Unknown"); break;
		}
		VMObj->SetStringField(TEXT("creation_type"), CreationStr);
		VMObj->SetBoolField(TEXT("create_setter"), VM.bCreateSetterFunction);
		VMObj->SetBoolField(TEXT("create_getter"), VM.bCreateGetterFunction);
		VMObj->SetBoolField(TEXT("optional"), VM.bOptional);

		VMArray.Add(MakeShared<FJsonValueObject>(VMObj));
	}
	ResultObj->SetArrayField(TEXT("viewmodels"), VMArray);

	// Serialize Bindings
	TArray<TSharedPtr<FJsonValue>> BindingArray;
	TArrayView<FMVVMBlueprintViewBinding> Bindings = BPView->GetBindings();
	for (const FMVVMBlueprintViewBinding& Binding : Bindings)
	{
		TSharedPtr<FJsonObject> BindObj = MakeShared<FJsonObject>();
		BindObj->SetStringField(TEXT("binding_id"), Binding.BindingId.ToString());
		BindObj->SetStringField(TEXT("display_name"), Binding.GetDisplayNameString(WidgetBlueprint));
		BindObj->SetBoolField(TEXT("enabled"), Binding.bEnabled);
		BindObj->SetBoolField(TEXT("compile"), Binding.bCompile);

		// Binding mode
		FString ModeStr;
		switch (Binding.BindingType)
		{
		case EMVVMBindingMode::OneTimeToDestination:
			ModeStr = TEXT("OneTimeToDestination"); break;
		case EMVVMBindingMode::OneWayToDestination:
			ModeStr = TEXT("OneWayToDestination"); break;
		case EMVVMBindingMode::TwoWay:
			ModeStr = TEXT("TwoWay"); break;
		case EMVVMBindingMode::OneTimeToSource:
			ModeStr = TEXT("OneTimeToSource"); break;
		case EMVVMBindingMode::OneWayToSource:
			ModeStr = TEXT("OneWayToSource"); break;
		default:
			ModeStr = TEXT("Unknown"); break;
		}
		BindObj->SetStringField(TEXT("binding_mode"), ModeStr);

		// Execution mode
		if (Binding.bOverrideExecutionMode)
		{
			FString ExecStr;
			switch (Binding.OverrideExecutionMode)
			{
			case EMVVMExecutionMode::Immediate:
				ExecStr = TEXT("Immediate"); break;
			case EMVVMExecutionMode::Delayed:
				ExecStr = TEXT("Delayed"); break;
			case EMVVMExecutionMode::Tick:
				ExecStr = TEXT("Tick"); break;
			case EMVVMExecutionMode::DelayedWhenSharedElseImmediate:
				ExecStr = TEXT("Auto"); break;
			default:
				ExecStr = TEXT("Unknown"); break;
			}
			BindObj->SetStringField(TEXT("execution_mode"), ExecStr);
		}

		BindingArray.Add(MakeShared<FJsonValueObject>(BindObj));
	}
	ResultObj->SetArrayField(TEXT("bindings"), BindingArray);

	return ResultObj;
}

// =============================================================================
// FMVVMRemoveBindingAction
// =============================================================================

bool FMVVMRemoveBindingAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("binding_id")))
	{
		OutError = TEXT("Missing 'binding_id' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FMVVMRemoveBindingAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString BindingIdStr = Params->GetStringField(TEXT("binding_id"));

	// Find Widget Blueprint
	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	// Get MVVM extension
	UMVVMWidgetBlueprintExtension_View* MVVMExt =
		UWidgetBlueprintExtension::GetExtension<UMVVMWidgetBlueprintExtension_View>(WidgetBlueprint);
	if (!MVVMExt)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("No MVVM extension found on Widget Blueprint."));
	}

	UMVVMBlueprintView* BPView = MVVMExt->GetBlueprintView();
	if (!BPView)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("No MVVM BlueprintView found."));
	}

	// Find and remove binding by ID
	FGuid TargetGuid;
	FGuid::Parse(BindingIdStr, TargetGuid);

	TArrayView<FMVVMBlueprintViewBinding> Bindings = BPView->GetBindings();
	int32 FoundIndex = INDEX_NONE;
	for (int32 i = 0; i < Bindings.Num(); ++i)
	{
		if (Bindings[i].BindingId == TargetGuid)
		{
			FoundIndex = i;
			break;
		}
	}

	if (FoundIndex == INDEX_NONE)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Binding with ID '%s' not found"), *BindingIdStr));
	}

	FString DisplayName = Bindings[FoundIndex].GetDisplayNameString(WidgetBlueprint);
	// RemoveBindingAt internally handles cleanup, broadcasts OnBindingsUpdated,
	// and marks the blueprint as structurally modified
	BPView->RemoveBindingAt(FoundIndex);

	// Only mark package dirty and auto-save (RemoveBindingAt already did MarkAsStructurallyModified)
	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("removed_binding_id"), BindingIdStr);
	ResultObj->SetStringField(TEXT("removed_display_name"), DisplayName);
	return ResultObj;
}

// =============================================================================
// FMVVMRemoveViewModelAction
// =============================================================================

bool FMVVMRemoveViewModelAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("viewmodel_name")))
	{
		OutError = TEXT("Missing 'viewmodel_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FMVVMRemoveViewModelAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString ViewModelName = Params->GetStringField(TEXT("viewmodel_name"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found"), *BlueprintName));
	}

	UMVVMWidgetBlueprintExtension_View* MVVMExt =
		UWidgetBlueprintExtension::GetExtension<UMVVMWidgetBlueprintExtension_View>(WidgetBlueprint);
	if (!MVVMExt)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("No MVVM extension found on Widget Blueprint."));
	}

	UMVVMBlueprintView* BPView = MVVMExt->GetBlueprintView();
	if (!BPView)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("No MVVM BlueprintView found."));
	}

	const FMVVMBlueprintViewModelContext* VMContext = BPView->FindViewModel(FName(*ViewModelName));
	if (!VMContext)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("ViewModel '%s' not found on this Widget Blueprint"), *ViewModelName));
	}

	const FGuid TargetId = VMContext->GetViewModelId();
	BPView->RemoveViewModel(TargetId);

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("removed_viewmodel_name"), ViewModelName);
	ResultObj->SetStringField(TEXT("removed_viewmodel_id"), TargetId.ToString());
	return ResultObj;
}
