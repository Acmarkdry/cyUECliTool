// Copyright (c) 2025 zolnoor. All rights reserved.
// UMG shared helper functions — included by all UMG*Actions.cpp files.
// These were originally static functions at the top of UMGActions.cpp (pre-split).

#pragma once

#include "WidgetBlueprint.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "MCPContext.h"

// Helper to find widget blueprint anywhere under /Game/
static UWidgetBlueprint* FindWidgetBlueprintByName(const FString& BlueprintName)
{
	if (BlueprintName.StartsWith(TEXT("/Game/")))
	{
		if (UEditorAssetLibrary::DoesAssetExist(BlueprintName))
		{
			UWidgetBlueprint* Widget = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintName));
			if (Widget) return Widget;
		}
	}

	TArray<FString> PriorityPaths = {
		FString::Printf(TEXT("/Game/UI/%s"), *BlueprintName),
		FString::Printf(TEXT("/Game/Widgets/%s"), *BlueprintName),
		FString::Printf(TEXT("/Game/%s"), *BlueprintName)
	};

	for (const FString& Path : PriorityPaths)
	{
		if (UEditorAssetLibrary::DoesAssetExist(Path))
		{
			UWidgetBlueprint* Widget = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(Path));
			if (Widget) return Widget;
		}
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetClassPathName(), AssetList, true);

	for (const FAssetData& AssetData : AssetList)
	{
		if (AssetData.AssetName.ToString() == BlueprintName && AssetData.PackagePath.ToString().StartsWith(TEXT("/Game")))
		{
			UWidgetBlueprint* Widget = Cast<UWidgetBlueprint>(AssetData.GetAsset());
			if (Widget) return Widget;
		}
	}

	return nullptr;
}


static bool TryGetVector2Field(const TSharedPtr<FJsonObject>& Params, const FString& FieldName, FVector2D& OutValue)
{
	const TArray<TSharedPtr<FJsonValue>>* Array;
	if (!Params->TryGetArrayField(FieldName, Array) || Array->Num() < 2) return false;
	OutValue.X = (*Array)[0]->AsNumber();
	OutValue.Y = (*Array)[1]->AsNumber();
	return true;
}


static bool TryGetColorField(const TSharedPtr<FJsonObject>& Params, const FString& FieldName, FLinearColor& OutValue)
{
	const TArray<TSharedPtr<FJsonValue>>* Array;
	if (!Params->TryGetArrayField(FieldName, Array) || Array->Num() < 4) return false;
	OutValue = FLinearColor(
		(*Array)[0]->AsNumber(), (*Array)[1]->AsNumber(),
		(*Array)[2]->AsNumber(), (*Array)[3]->AsNumber());
	return true;
}


static void ApplyCanvasSlot(UCanvasPanelSlot* Slot, const TSharedPtr<FJsonObject>& Params)
{
	if (!Slot) return;

	const TArray<TSharedPtr<FJsonValue>>* AnchorsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("anchors"), AnchorsArray) && AnchorsArray && AnchorsArray->Num() >= 4)
	{
		const float MinX = static_cast<float>((*AnchorsArray)[0]->AsNumber());
		const float MinY = static_cast<float>((*AnchorsArray)[1]->AsNumber());
		const float MaxX = static_cast<float>((*AnchorsArray)[2]->AsNumber());
		const float MaxY = static_cast<float>((*AnchorsArray)[3]->AsNumber());
		Slot->SetAnchors(FAnchors(MinX, MinY, MaxX, MaxY));
	}

	FVector2D Alignment;
	if (TryGetVector2Field(Params, TEXT("alignment"), Alignment))
		Slot->SetAlignment(Alignment);

	FVector2D Position;
	if (TryGetVector2Field(Params, TEXT("position"), Position))
		Slot->SetPosition(Position);

	FVector2D Size;
	if (TryGetVector2Field(Params, TEXT("size"), Size))
	{
		Slot->SetSize(Size);
		Slot->SetAutoSize(false);
	}

	int32 ZOrder = 0;
	if (Params->TryGetNumberField(TEXT("z_order"), ZOrder))
		Slot->SetZOrder(ZOrder);
}


static void EnsureWidgetVariableGuids(UWidgetBlueprint* WidgetBlueprint)
{
	if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree) return;

	WidgetBlueprint->WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		if (!Widget) return;
		FName WidgetFName = Widget->GetFName();
		if (!WidgetBlueprint->WidgetVariableNameToGuidMap.Contains(WidgetFName))
		{
			WidgetBlueprint->WidgetVariableNameToGuidMap.Add(WidgetFName, FGuid::NewGuid());
		}
	});
}


static void MarkWidgetBlueprintDirty(UWidgetBlueprint* WidgetBlueprint, FMCPEditorContext& Context)
{
	if (!WidgetBlueprint) return;

	// Repair WidgetVariableNameToGuidMap before triggering compilation.
	EnsureWidgetVariableGuids(WidgetBlueprint);

	UPackage* Package = WidgetBlueprint->GetOutermost();
	Context.MarkPackageDirty(Package);

	// Use MarkBlueprintAsModified instead of MarkBlueprintAsStructurallyModified
	// to avoid ensure() assertions when the blueprint is open in Widget Designer.
	if (!WidgetBlueprint->bBeingCompiled && WidgetBlueprint->Status != BS_BeingCreated)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);
	}
}