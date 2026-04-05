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

#include "UMGCommonHelpers.h"


// =============================================================================
// FCreateUMGWidgetBlueprintAction
// =============================================================================

bool FCreateUMGWidgetBlueprintAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FCreateUMGWidgetBlueprintAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));

	// Get optional path parameter
	FString PackagePath = TEXT("/Game/UI/");
	FString PathParam;
	if (Params->TryGetStringField(TEXT("path"), PathParam))
	{
		PackagePath = PathParam;
		if (!PackagePath.EndsWith(TEXT("/")))
		{
			PackagePath += TEXT("/");
		}
	}

	FString AssetName = BlueprintName;
	FString FullPath = PackagePath + AssetName;

	// Aggressive cleanup: remove any existing widget blueprint
	TArray<FString> PathsToCheck = {
		FullPath,
		TEXT("/Game/Widgets/") + AssetName,
		TEXT("/Game/UI/") + AssetName
	};

	for (const FString& CheckPath : PathsToCheck)
	{
		// Delete from disk first
		if (UEditorAssetLibrary::DoesAssetExist(CheckPath))
		{
			UE_LOG(LogMCP, Log, TEXT("Widget Blueprint exists at '%s', deleting from disk"), *CheckPath);
			UEditorAssetLibrary::DeleteAsset(CheckPath);
		}

		// Clean up from memory
		UPackage* ExistingPackage = FindPackage(nullptr, *CheckPath);
		if (ExistingPackage)
		{
			UBlueprint* ExistingBP = FindObject<UBlueprint>(ExistingPackage, *AssetName);
			if (!ExistingBP)
			{
				ExistingBP = FindObject<UBlueprint>(ExistingPackage, nullptr);
			}

			if (ExistingBP)
			{
				UE_LOG(LogMCP, Log, TEXT("Widget Blueprint '%s' found in memory, cleaning up"), *AssetName);
				FString TempName = FString::Printf(TEXT("%s_OLD_%d"), *AssetName, FMath::Rand());
				ExistingBP->Rename(*TempName, GetTransientPackage(), REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
				ExistingBP->ClearFlags(RF_Public | RF_Standalone);
				ExistingBP->MarkAsGarbage();
			}

			ExistingPackage->ClearFlags(RF_Public | RF_Standalone);
			ExistingPackage->MarkAsGarbage();
		}
	}

	// Force garbage collection
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	// Create package
	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create package"));
	}

	// Double-check cleanup worked
	if (FindObject<UBlueprint>(Package, *AssetName))
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Failed to clean up existing Widget Blueprint '%s'. Try restarting the editor."), *AssetName));
	}

	// Create Widget Blueprint
	UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(
		UUserWidget::StaticClass(),
		Package,
		FName(*AssetName),
		BPTYPE_Normal,
		UWidgetBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		FName("CreateUMGWidget")
	);

	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(NewBlueprint);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Widget Blueprint"));
	}

	// Add default Canvas Panel
	if (!WidgetBlueprint->WidgetTree->RootWidget)
	{
		UCanvasPanel* RootCanvas = WidgetBlueprint->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		WidgetBlueprint->WidgetTree->RootWidget = RootCanvas;
	}

	// Register asset with asset registry
	FAssetRegistryModule::AssetCreated(WidgetBlueprint);
	WidgetBlueprint->MarkPackageDirty();

	// Mark modified, compile, and refresh the Widget Designer
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	UE_LOG(LogMCP, Log, TEXT("Widget Blueprint '%s' created at '%s'"), *BlueprintName, *FullPath);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("name"), BlueprintName);
	ResultObj->SetStringField(TEXT("path"), FullPath);
	return ResultObj;
}

// =============================================================================
// FAddTextBlockToWidgetAction
// =============================================================================

bool FAddTextBlockToWidgetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
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
	return true;
}

TSharedPtr<FJsonObject> FAddTextBlockToWidgetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetName = Params->GetStringField(TEXT("text_block_name"));

	UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprintByName(BlueprintName);
	if (!WidgetBlueprint)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
			TEXT("Widget Blueprint '%s' not found in /Game/UI, /Game/Widgets, or /Game"), *BlueprintName));
	}

	// Optional parameters
	FString InitialText = TEXT("New Text Block");
	Params->TryGetStringField(TEXT("text"), InitialText);

	FVector2D Position(0.0f, 0.0f);
	if (Params->HasField(TEXT("position")))
	{
		const TArray<TSharedPtr<FJsonValue>>* PosArray;
		if (Params->TryGetArrayField(TEXT("position"), PosArray) && PosArray->Num() >= 2)
		{
			Position.X = (*PosArray)[0]->AsNumber();
			Position.Y = (*PosArray)[1]->AsNumber();
		}
	}

	// Create Text Block
	UTextBlock* TextBlock = WidgetBlueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *WidgetName);
	if (!TextBlock)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Text Block widget"));
	}

	TextBlock->SetText(FText::FromString(InitialText));

	// Add to canvas
	UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(WidgetBlueprint->WidgetTree->RootWidget);
	if (!RootCanvas)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Root Canvas Panel not found"));
	}

	UCanvasPanelSlot* PanelSlot = RootCanvas->AddChildToCanvas(TextBlock);
	PanelSlot->SetPosition(Position);

	// Mark modified, compile, and refresh the Widget Designer
	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	ResultObj->SetStringField(TEXT("text"), InitialText);
	return ResultObj;
}

// =============================================================================
// FAddButtonToWidgetAction
// =============================================================================

bool FAddButtonToWidgetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("button_name")))
	{
		OutError = TEXT("Missing 'button_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddButtonToWidgetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetName = Params->GetStringField(TEXT("button_name"));

	FString ButtonText = TEXT("Button");
	Params->TryGetStringField(TEXT("text"), ButtonText);

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

	// Create Button
	UButton* Button = WidgetBlueprint->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), *WidgetName);
	if (!Button)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Button widget"));
	}

	// Create text block for button label
	FString TextBlockName = WidgetName + TEXT("_Text");
	UTextBlock* ButtonTextBlock = WidgetBlueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *TextBlockName);
	if (ButtonTextBlock)
	{
		ButtonTextBlock->SetText(FText::FromString(ButtonText));
		Button->AddChild(ButtonTextBlock);
	}

	// Add to canvas
	UCanvasPanelSlot* ButtonSlot = RootCanvas->AddChildToCanvas(Button);
	if (ButtonSlot)
	{
		const TArray<TSharedPtr<FJsonValue>>* Position;
		if (Params->TryGetArrayField(TEXT("position"), Position) && Position->Num() >= 2)
		{
			FVector2D Pos((*Position)[0]->AsNumber(), (*Position)[1]->AsNumber());
			ButtonSlot->SetPosition(Pos);
		}

		const TArray<TSharedPtr<FJsonValue>>* Size;
		if (Params->TryGetArrayField(TEXT("size"), Size) && Size->Num() >= 2)
		{
			FVector2D SizeVec((*Size)[0]->AsNumber(), (*Size)[1]->AsNumber());
			ButtonSlot->SetSize(SizeVec);
			ButtonSlot->SetAutoSize(false);
		}
	}

	// Mark modified, compile, and refresh the Widget Designer
	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	return ResultObj;
}

// =============================================================================
// FAddImageToWidgetAction
// =============================================================================

bool FAddImageToWidgetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("image_name")))
	{
		OutError = TEXT("Missing 'image_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddImageToWidgetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetName = Params->GetStringField(TEXT("image_name"));

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

	// Optional parameters
	FVector2D Position(0.0f, 0.0f);
	if (Params->HasField(TEXT("position")))
	{
		const TArray<TSharedPtr<FJsonValue>>* PosArray;
		if (Params->TryGetArrayField(TEXT("position"), PosArray) && PosArray->Num() >= 2)
		{
			Position.X = (*PosArray)[0]->AsNumber();
			Position.Y = (*PosArray)[1]->AsNumber();
		}
	}

	bool bHasSize = false;
	FVector2D Size(0.0f, 0.0f);
	if (Params->HasField(TEXT("size")))
	{
		const TArray<TSharedPtr<FJsonValue>>* SizeArray;
		if (Params->TryGetArrayField(TEXT("size"), SizeArray) && SizeArray->Num() >= 2)
		{
			Size.X = (*SizeArray)[0]->AsNumber();
			Size.Y = (*SizeArray)[1]->AsNumber();
			bHasSize = true;
		}
	}

	int32 ZOrder = 0;
	Params->TryGetNumberField(TEXT("z_order"), ZOrder);

	bool bHasTint = false;
	FLinearColor Tint = FLinearColor::White;
	if (Params->HasField(TEXT("color")))
	{
		const TArray<TSharedPtr<FJsonValue>>* ColorArray;
		if (Params->TryGetArrayField(TEXT("color"), ColorArray) && ColorArray->Num() >= 4)
		{
			Tint = FLinearColor(
				(*ColorArray)[0]->AsNumber(),
				(*ColorArray)[1]->AsNumber(),
				(*ColorArray)[2]->AsNumber(),
				(*ColorArray)[3]->AsNumber());
			bHasTint = true;
		}
	}

	FString TexturePath;
	Params->TryGetStringField(TEXT("texture_path"), TexturePath);

	// Create Image
	UImage* Image = WidgetBlueprint->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), *WidgetName);
	if (!Image)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Image widget"));
	}

	if (!TexturePath.IsEmpty())
	{
		UObject* TextureObj = UEditorAssetLibrary::LoadAsset(TexturePath);
		UTexture2D* Texture = Cast<UTexture2D>(TextureObj);
		if (!Texture)
		{
			return FMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Texture not found or invalid: %s"), *TexturePath));
		}
		Image->SetBrushFromTexture(Texture);
	}

	if (bHasTint)
	{
		Image->SetColorAndOpacity(Tint);
	}

	// Add to canvas
	UCanvasPanelSlot* ImageSlot = RootCanvas->AddChildToCanvas(Image);
	if (ImageSlot)
	{
		ApplyCanvasSlot(ImageSlot, Params);
	}

	// Mark modified, compile, and refresh the Widget Designer
	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	if (!TexturePath.IsEmpty())
	{
		ResultObj->SetStringField(TEXT("texture_path"), TexturePath);
	}
	return ResultObj;
}

// =============================================================================
// FAddBorderToWidgetAction
// =============================================================================

bool FAddBorderToWidgetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("border_name")))
	{
		OutError = TEXT("Missing 'border_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddBorderToWidgetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetName = Params->GetStringField(TEXT("border_name"));

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

	UBorder* Border = WidgetBlueprint->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), *WidgetName);
	if (!Border)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Border widget"));
	}

	FLinearColor Tint;
	if (TryGetColorField(Params, TEXT("color"), Tint))
	{
		Border->SetBrushColor(Tint);
	}

	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(Border);
	ApplyCanvasSlot(Slot, Params);

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	return ResultObj;
}

// =============================================================================
// FAddOverlayToWidgetAction
// =============================================================================

bool FAddOverlayToWidgetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("overlay_name")))
	{
		OutError = TEXT("Missing 'overlay_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddOverlayToWidgetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetName = Params->GetStringField(TEXT("overlay_name"));

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

	UOverlay* Overlay = WidgetBlueprint->WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), *WidgetName);
	if (!Overlay)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Overlay widget"));
	}

	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(Overlay);
	ApplyCanvasSlot(Slot, Params);

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	return ResultObj;
}

// =============================================================================
// FAddHorizontalBoxToWidgetAction
// =============================================================================

bool FAddHorizontalBoxToWidgetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("horizontal_box_name")))
	{
		OutError = TEXT("Missing 'horizontal_box_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddHorizontalBoxToWidgetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetName = Params->GetStringField(TEXT("horizontal_box_name"));

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

	UHorizontalBox* HorizontalBox = WidgetBlueprint->WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), *WidgetName);
	if (!HorizontalBox)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create HorizontalBox widget"));
	}

	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(HorizontalBox);
	ApplyCanvasSlot(Slot, Params);

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	return ResultObj;
}

// =============================================================================
// FAddVerticalBoxToWidgetAction
// =============================================================================

bool FAddVerticalBoxToWidgetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("vertical_box_name")))
	{
		OutError = TEXT("Missing 'vertical_box_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddVerticalBoxToWidgetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetName = Params->GetStringField(TEXT("vertical_box_name"));

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

	UVerticalBox* VerticalBox = WidgetBlueprint->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), *WidgetName);
	if (!VerticalBox)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create VerticalBox widget"));
	}

	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(VerticalBox);
	ApplyCanvasSlot(Slot, Params);

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	return ResultObj;
}

// =============================================================================
// FAddSliderToWidgetAction
// =============================================================================

bool FAddSliderToWidgetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("slider_name")))
	{
		OutError = TEXT("Missing 'slider_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddSliderToWidgetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetName = Params->GetStringField(TEXT("slider_name"));

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

	USlider* Slider = WidgetBlueprint->WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), *WidgetName);
	if (!Slider)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Slider widget"));
	}

	double Value = 0.0;
	if (Params->TryGetNumberField(TEXT("value"), Value))
	{
		Slider->SetValue(static_cast<float>(Value));
	}

	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(Slider);
	ApplyCanvasSlot(Slot, Params);

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	return ResultObj;
}

// =============================================================================
// FAddProgressBarToWidgetAction
// =============================================================================

bool FAddProgressBarToWidgetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("progress_bar_name")))
	{
		OutError = TEXT("Missing 'progress_bar_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddProgressBarToWidgetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetName = Params->GetStringField(TEXT("progress_bar_name"));

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

	UProgressBar* ProgressBar = WidgetBlueprint->WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), *WidgetName);
	if (!ProgressBar)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create ProgressBar widget"));
	}

	double Percent = 0.0;
	if (Params->TryGetNumberField(TEXT("percent"), Percent))
	{
		ProgressBar->SetPercent(static_cast<float>(Percent));
	}

	FLinearColor Tint;
	if (TryGetColorField(Params, TEXT("color"), Tint))
	{
		ProgressBar->SetFillColorAndOpacity(Tint);
	}

	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(ProgressBar);
	ApplyCanvasSlot(Slot, Params);

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	return ResultObj;
}

// =============================================================================
// FAddSizeBoxToWidgetAction
// =============================================================================

bool FAddSizeBoxToWidgetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("size_box_name")))
	{
		OutError = TEXT("Missing 'size_box_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddSizeBoxToWidgetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetName = Params->GetStringField(TEXT("size_box_name"));

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

	USizeBox* SizeBox = WidgetBlueprint->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *WidgetName);
	if (!SizeBox)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create SizeBox widget"));
	}

	FVector2D OverrideSize;
	if (TryGetVector2Field(Params, TEXT("size"), OverrideSize))
	{
		SizeBox->SetWidthOverride(OverrideSize.X);
		SizeBox->SetHeightOverride(OverrideSize.Y);
	}

	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(SizeBox);
	ApplyCanvasSlot(Slot, Params);

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	return ResultObj;
}

// =============================================================================
// FAddScaleBoxToWidgetAction
// =============================================================================

bool FAddScaleBoxToWidgetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("scale_box_name")))
	{
		OutError = TEXT("Missing 'scale_box_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddScaleBoxToWidgetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetName = Params->GetStringField(TEXT("scale_box_name"));

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

	UScaleBox* ScaleBox = WidgetBlueprint->WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), *WidgetName);
	if (!ScaleBox)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create ScaleBox widget"));
	}

	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(ScaleBox);
	ApplyCanvasSlot(Slot, Params);

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	return ResultObj;
}

// =============================================================================
// FAddCanvasPanelToWidgetAction
// =============================================================================

bool FAddCanvasPanelToWidgetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("canvas_panel_name")))
	{
		OutError = TEXT("Missing 'canvas_panel_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddCanvasPanelToWidgetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetName = Params->GetStringField(TEXT("canvas_panel_name"));

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

	UCanvasPanel* CanvasPanel = WidgetBlueprint->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), *WidgetName);
	if (!CanvasPanel)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create CanvasPanel widget"));
	}

	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(CanvasPanel);
	ApplyCanvasSlot(Slot, Params);

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	return ResultObj;
}

// =============================================================================
// FAddComboBoxToWidgetAction
// =============================================================================

bool FAddComboBoxToWidgetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("combo_box_name")))
	{
		OutError = TEXT("Missing 'combo_box_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddComboBoxToWidgetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetName = Params->GetStringField(TEXT("combo_box_name"));

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

	UComboBoxString* ComboBox = WidgetBlueprint->WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), *WidgetName);
	if (!ComboBox)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create ComboBoxString widget"));
	}

	// Add options
	const TArray<TSharedPtr<FJsonValue>>* OptionsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("options"), OptionsArray))
	{
		for (const TSharedPtr<FJsonValue>& Val : *OptionsArray)
		{
			ComboBox->AddOption(Val->AsString());
		}
	}

	// Set default selected
	FString SelectedOption;
	if (Params->TryGetStringField(TEXT("selected_option"), SelectedOption))
	{
		ComboBox->SetSelectedOption(SelectedOption);
	}
	else if (OptionsArray && OptionsArray->Num() > 0)
	{
		ComboBox->SetSelectedOption((*OptionsArray)[0]->AsString());
	}

	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(ComboBox);
	ApplyCanvasSlot(Slot, Params);

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	return ResultObj;
}

// =============================================================================
// FAddCheckBoxToWidgetAction
// =============================================================================

bool FAddCheckBoxToWidgetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("check_box_name")))
	{
		OutError = TEXT("Missing 'check_box_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddCheckBoxToWidgetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetName = Params->GetStringField(TEXT("check_box_name"));

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

	UCheckBox* CheckBox = WidgetBlueprint->WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), *WidgetName);
	if (!CheckBox)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create CheckBox widget"));
	}

	bool bIsChecked = false;
	if (Params->TryGetBoolField(TEXT("is_checked"), bIsChecked) && bIsChecked)
	{
		CheckBox->SetIsChecked(true);
	}

	// Optional label text - add as a child TextBlock
	FString LabelText;
	if (Params->TryGetStringField(TEXT("label"), LabelText))
	{
		FString TextBlockName = WidgetName + TEXT("_Label");
		UTextBlock* Label = WidgetBlueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *TextBlockName);
		if (Label)
		{
			Label->SetText(FText::FromString(LabelText));
			CheckBox->AddChild(Label);
		}
	}

	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(CheckBox);
	ApplyCanvasSlot(Slot, Params);

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	return ResultObj;
}

// =============================================================================
// FAddSpinBoxToWidgetAction
// =============================================================================

bool FAddSpinBoxToWidgetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("spin_box_name")))
	{
		OutError = TEXT("Missing 'spin_box_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddSpinBoxToWidgetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetName = Params->GetStringField(TEXT("spin_box_name"));

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

	USpinBox* SpinBox = WidgetBlueprint->WidgetTree->ConstructWidget<USpinBox>(USpinBox::StaticClass(), *WidgetName);
	if (!SpinBox)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create SpinBox widget"));
	}

	double Value = 0.0;
	if (Params->TryGetNumberField(TEXT("value"), Value))
	{
		SpinBox->SetValue(static_cast<float>(Value));
	}

	double MinValue = 0.0;
	if (Params->TryGetNumberField(TEXT("min_value"), MinValue))
	{
		SpinBox->SetMinValue(static_cast<float>(MinValue));
	}

	double MaxValue = 100.0;
	if (Params->TryGetNumberField(TEXT("max_value"), MaxValue))
	{
		SpinBox->SetMaxValue(static_cast<float>(MaxValue));
	}

	double Delta = 1.0;
	if (Params->TryGetNumberField(TEXT("delta"), Delta))
	{
		SpinBox->SetDelta(static_cast<float>(Delta));
	}

	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(SpinBox);
	ApplyCanvasSlot(Slot, Params);

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	return ResultObj;
}

// =============================================================================
// FAddEditableTextBoxToWidgetAction
// =============================================================================

bool FAddEditableTextBoxToWidgetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing 'widget_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("editable_text_box_name")))
	{
		OutError = TEXT("Missing 'editable_text_box_name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddEditableTextBoxToWidgetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = Params->GetStringField(TEXT("widget_name"));
	FString WidgetName = Params->GetStringField(TEXT("editable_text_box_name"));

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

	UEditableTextBox* TextBox = WidgetBlueprint->WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), *WidgetName);
	if (!TextBox)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create EditableTextBox widget"));
	}

	FString InitialText;
	if (Params->TryGetStringField(TEXT("text"), InitialText))
	{
		TextBox->SetText(FText::FromString(InitialText));
	}

	FString HintText;
	if (Params->TryGetStringField(TEXT("hint_text"), HintText))
	{
		TextBox->SetHintText(FText::FromString(HintText));
	}

	bool bIsReadOnly = false;
	if (Params->TryGetBoolField(TEXT("is_read_only"), bIsReadOnly))
	{
		TextBox->SetIsReadOnly(bIsReadOnly);
	}

	UCanvasPanelSlot* Slot = RootCanvas->AddChildToCanvas(TextBox);
	ApplyCanvasSlot(Slot, Params);

	WidgetBlueprint->MarkPackageDirty();
	MarkWidgetBlueprintDirty(WidgetBlueprint, Context);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("widget_name"), WidgetName);
	return ResultObj;
}

// =============================================================================
