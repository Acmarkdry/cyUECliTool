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
// Helper: Resolve a UClass by short name or full path
// =============================================================================
static UClass* ResolveClassByName(const FString& ClassName)
{
	// Try full path first (e.g. "/Script/MyModule.UMyViewModel")
	UClass* FoundClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
	if (FoundClass)
	{
		return FoundClass;
	}

	// Try with common prefixes
	TArray<FString> Candidates = {
		FString::Printf(TEXT("U%s"), *ClassName),   // UMyViewModel
		FString::Printf(TEXT("%s_C"), *ClassName),   // Blueprint generated class
	};

	for (const FString& Candidate : Candidates)
	{
		FoundClass = FindFirstObject<UClass>(*Candidate, EFindFirstObjectOptions::EnsureIfAmbiguous);
		if (FoundClass)
		{
			return FoundClass;
		}
	}

	// Search via Asset Registry for Blueprint-based ViewModels
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), AssetList, true);

	for (const FAssetData& AssetData : AssetList)
	{
		if (AssetData.AssetName.ToString() == ClassName)
		{
			UBlueprint* BP = Cast<UBlueprint>(AssetData.GetAsset());
			if (BP && BP->GeneratedClass)
			{
				return BP->GeneratedClass;
			}
		}
	}

	return nullptr;
}

