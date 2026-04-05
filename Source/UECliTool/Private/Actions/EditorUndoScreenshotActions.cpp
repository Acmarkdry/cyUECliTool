// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/EditorActions.h"
#include "MCPCommonUtils.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/CameraActor.h"
#include "Kismet/GameplayStatics.h"
#include "FileHelpers.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "AssetSelection.h"
#include "ObjectTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"
#include "ImageUtils.h"
#include "Misc/Base64.h"
#include "Misc/PackageName.h"
#include "MCPLogCapture.h"
#include "MCPBridge.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/UnrealType.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformMisc.h"
#include "Misc/DateTime.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "EngineUtils.h"
#include "Selection.h"



// =========================================================================
// P7: Undo / Redo / Undo History
// =========================================================================

TSharedPtr<FJsonObject> FUndoAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	if (!GEditor || !GEditor->Trans)
	{
		return CreateErrorResponse(TEXT("Editor transaction system is not available"), TEXT("editor_not_ready"));
	}

	UTransBuffer* TransBuffer = CastChecked<UTransBuffer>(GEditor->Trans);
	if (TransBuffer->GetUndoCount() == 0)
	{
		return CreateErrorResponse(TEXT("Nothing to undo — undo history is empty"), TEXT("nothing_to_undo"));
	}

	// Get description of what we're about to undo
	FString Description = TransBuffer->GetUndoContext(false).Title.ToString();

	bool bSuccess = GEditor->UndoTransaction();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("undone"), bSuccess);
	Result->SetStringField(TEXT("description"), Description);
	Result->SetNumberField(TEXT("remaining_undo_count"), TransBuffer->GetUndoCount());

	return CreateSuccessResponse(Result);
}


TSharedPtr<FJsonObject> FRedoAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	if (!GEditor || !GEditor->Trans)
	{
		return CreateErrorResponse(TEXT("Editor transaction system is not available"), TEXT("editor_not_ready"));
	}

	UTransBuffer* TransBuffer = CastChecked<UTransBuffer>(GEditor->Trans);

	// Check if there's anything to redo
	FTransactionContext RedoContext = TransBuffer->GetRedoContext();
	if (RedoContext.Title.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Nothing to redo — redo history is empty"), TEXT("nothing_to_redo"));
	}

	FString Description = RedoContext.Title.ToString();

	bool bSuccess = GEditor->RedoTransaction();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("redone"), bSuccess);
	Result->SetStringField(TEXT("description"), Description);

	return CreateSuccessResponse(Result);
}


TSharedPtr<FJsonObject> FGetUndoHistoryAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	if (!GEditor || !GEditor->Trans)
	{
		return CreateErrorResponse(TEXT("Editor transaction system is not available"), TEXT("editor_not_ready"));
	}

	int32 Limit = static_cast<int32>(GetOptionalNumber(Params, TEXT("limit"), 20.0));
	Limit = FMath::Clamp(Limit, 1, 100);

	UTransBuffer* TransBuffer = CastChecked<UTransBuffer>(GEditor->Trans);

	TArray<TSharedPtr<FJsonValue>> UndoEntries;
	int32 UndoCount = TransBuffer->GetUndoCount();
	int32 StartIdx = FMath::Max(0, UndoCount - Limit);
	for (int32 i = StartIdx; i < UndoCount; ++i)
	{
		const FTransaction* Transaction = TransBuffer->GetTransaction(i);
		if (Transaction)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("index"), i);
			Entry->SetStringField(TEXT("title"), Transaction->GetContext().Title.ToString());
			Entry->SetStringField(TEXT("description"), Transaction->GetContext().Context);
			UndoEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("undo_entries"), UndoEntries);
	Result->SetNumberField(TEXT("total_undo_count"), UndoCount);
	Result->SetNumberField(TEXT("returned_count"), UndoEntries.Num());

	return CreateSuccessResponse(Result);
}


// =========================================================================
// P7: Viewport Screenshot
// =========================================================================

#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "ImageUtils.h"
#include "Misc/Base64.h"
#include "UnrealClient.h"

static TSharedPtr<FJsonObject> CaptureViewportToBase64(
	FViewport* Viewport, int32 TargetWidth, int32 TargetHeight, FEditorAction* Action)
{
	if (!Viewport)
	{
		return nullptr;
	}

	// Read raw pixels from viewport
	TArray<FColor> Bitmap;
	if (!Viewport->ReadPixels(Bitmap))
	{
		return nullptr;
	}

	int32 SrcWidth = Viewport->GetSizeXY().X;
	int32 SrcHeight = Viewport->GetSizeXY().Y;
	if (SrcWidth == 0 || SrcHeight == 0 || Bitmap.Num() == 0)
	{
		return nullptr;
	}

	// Resize if needed
	TArray<FColor> ResizedBitmap;
	if (SrcWidth != TargetWidth || SrcHeight != TargetHeight)
	{
		ResizedBitmap.SetNum(TargetWidth * TargetHeight);
		FImageUtils::ImageResize(SrcWidth, SrcHeight, Bitmap, TargetWidth, TargetHeight, ResizedBitmap, false);
	}
	else
	{
		ResizedBitmap = MoveTemp(Bitmap);
	}

	// Compress to PNG
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (!ImageWrapper.IsValid())
	{
		return nullptr;
	}

	if (!ImageWrapper->SetRaw(ResizedBitmap.GetData(), ResizedBitmap.Num() * sizeof(FColor),
		TargetWidth, TargetHeight, ERGBFormat::BGRA, 8))
	{
		return nullptr;
	}

	TArray64<uint8> CompressedData = ImageWrapper->GetCompressed();
	if (CompressedData.Num() == 0)
	{
		return nullptr;
	}

	// Encode to base64
	FString Base64String = FBase64::Encode(CompressedData.GetData(), CompressedData.Num());

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("image"), FString::Printf(TEXT("data:image/png;base64,%s"), *Base64String));
	Result->SetNumberField(TEXT("width"), TargetWidth);
	Result->SetNumberField(TEXT("height"), TargetHeight);
	Result->SetStringField(TEXT("format"), TEXT("png"));
	Result->SetNumberField(TEXT("size_bytes"), CompressedData.Num());

	return Result;
}


bool FTakeViewportScreenshotAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("GEditor is not available");
		return false;
	}

	if (IsRunningCommandlet())
	{
		OutError = TEXT("Viewport screenshot is not available in commandlet mode");
		return false;
	}

	// Check that we have an active viewport
	FViewport* Viewport = GEditor->GetActiveViewport();
	if (!Viewport)
	{
		OutError = TEXT("No active editor viewport available");
		return false;
	}

	return true;
}


TSharedPtr<FJsonObject> FTakeViewportScreenshotAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	int32 Width = FMath::Clamp(static_cast<int32>(GetOptionalNumber(Params, TEXT("width"), 512.0)), 64, 1024);
	int32 Height = FMath::Clamp(static_cast<int32>(GetOptionalNumber(Params, TEXT("height"), 512.0)), 64, 1024);

	FViewport* Viewport = GEditor->GetActiveViewport();
	TSharedPtr<FJsonObject> Result = CaptureViewportToBase64(Viewport, Width, Height, this);
	if (!Result.IsValid())
	{
		return CreateErrorResponse(TEXT("Failed to capture viewport pixels"), TEXT("capture_failed"));
	}

	Result->SetStringField(TEXT("source"), TEXT("editor_viewport"));
	return CreateSuccessResponse(Result);
}


bool FTakePIEScreenshotAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("GEditor is not available");
		return false;
	}

	if (!GEditor->IsPlayingSessionInEditor())
	{
		OutError = TEXT("PIE is not running. Start PIE first with editor.start_pie");
		return false;
	}

	return true;
}


TSharedPtr<FJsonObject> FTakePIEScreenshotAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	int32 Width = FMath::Clamp(static_cast<int32>(GetOptionalNumber(Params, TEXT("width"), 512.0)), 64, 1024);
	int32 Height = FMath::Clamp(static_cast<int32>(GetOptionalNumber(Params, TEXT("height"), 512.0)), 64, 1024);

	// Find PIE viewport
	FViewport* PIEViewport = nullptr;
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (WorldContext.WorldType == EWorldType::PIE && WorldContext.GameViewport)
		{
			PIEViewport = WorldContext.GameViewport->Viewport;
			break;
		}
	}

	if (!PIEViewport)
	{
		return CreateErrorResponse(TEXT("Could not find PIE game viewport"), TEXT("viewport_not_found"));
	}

	TSharedPtr<FJsonObject> Result = CaptureViewportToBase64(PIEViewport, Width, Height, this);
	if (!Result.IsValid())
	{
		return CreateErrorResponse(TEXT("Failed to capture PIE viewport pixels"), TEXT("capture_failed"));
	}

	Result->SetStringField(TEXT("source"), TEXT("pie_viewport"));
	return CreateSuccessResponse(Result);
}
