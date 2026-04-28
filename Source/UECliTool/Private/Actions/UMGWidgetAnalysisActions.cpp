// Copyright (c) 2025 zolnoor. All rights reserved.
// UMG Widget Analysis Actions — v0.4.0 platform extensions
// Requirements: 5.1, 5.2, 5.3, 6.1, 6.2, 6.3, 6.4, 7.1, 7.2, 8.1, 8.2

#include "Actions/UMGWidgetAnalysisActions.h"
#include "MCPCommonUtils.h"
#include "UMGCommonHelpers.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBox.h"
#include "Components/Overlay.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "K2Node_ComponentBoundEvent.h"
#include "Dom/JsonValue.h"

// MVVM includes
#include "MVVMBlueprintView.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "MVVMBlueprintViewModelContext.h"
#include "MVVMBlueprintViewBinding.h"
#include "MVVMPropertyPath.h"
#include "Types/MVVMBindingMode.h"


// ============================================================================
// Local Helpers
// ============================================================================

/**
 * Recursively build a JSON component tree from a UWidget hierarchy.
 */
static TSharedPtr<FJsonObject> BuildComponentTreeJson(UWidget* Widget, UWidgetBlueprint* WidgetBP)
{
	if (!Widget) return nullptr;

	TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("name"), Widget->GetName());
	Node->SetStringField(TEXT("type"), Widget->GetClass()->GetName());

	// Visibility
	FString VisStr;
	switch (Widget->GetVisibility())
	{
	case ESlateVisibility::Visible:           VisStr = TEXT("Visible"); break;
	case ESlateVisibility::Collapsed:         VisStr = TEXT("Collapsed"); break;
	case ESlateVisibility::Hidden:            VisStr = TEXT("Hidden"); break;
	case ESlateVisibility::HitTestInvisible:  VisStr = TEXT("HitTestInvisible"); break;
	case ESlateVisibility::SelfHitTestInvisible: VisStr = TEXT("SelfHitTestInvisible"); break;
	default:                                  VisStr = TEXT("Unknown"); break;
	}
	Node->SetStringField(TEXT("visibility"), VisStr);

	// Slot info (if parented to a CanvasPanel)
	TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot);
	if (CanvasSlot)
	{
		TArray<TSharedPtr<FJsonValue>> PosArr;
		PosArr.Add(MakeShared<FJsonValueNumber>(CanvasSlot->GetPosition().X));
		PosArr.Add(MakeShared<FJsonValueNumber>(CanvasSlot->GetPosition().Y));
		SlotObj->SetArrayField(TEXT("position"), PosArr);

		TArray<TSharedPtr<FJsonValue>> SizeArr;
		SizeArr.Add(MakeShared<FJsonValueNumber>(CanvasSlot->GetSize().X));
		SizeArr.Add(MakeShared<FJsonValueNumber>(CanvasSlot->GetSize().Y));
		SlotObj->SetArrayField(TEXT("size"), SizeArr);

		SlotObj->SetBoolField(TEXT("auto_size"), CanvasSlot->GetAutoSize());
	}
	Node->SetObjectField(TEXT("slot"), SlotObj);

	// Children
	TArray<TSharedPtr<FJsonValue>> ChildrenArr;
	UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
	if (Panel)
	{
		for (int32 i = 0; i < Panel->GetChildrenCount(); i++)
		{
			UWidget* Child = Panel->GetChildAt(i);
			TSharedPtr<FJsonObject> ChildJson = BuildComponentTreeJson(Child, WidgetBP);
			if (ChildJson.IsValid())
			{
				ChildrenArr.Add(MakeShared<FJsonValueObject>(ChildJson));
			}
		}
	}
	Node->SetArrayField(TEXT("children"), ChildrenArr);

	return Node;
}

/**
 * Collect event bindings from the Widget Blueprint's EventGraph.
 */
static TArray<TSharedPtr<FJsonValue>> CollectEventBindings(UWidgetBlueprint* WidgetBP)
{
	TArray<TSharedPtr<FJsonValue>> Bindings;

	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(WidgetBP);
	if (!EventGraph) return Bindings;

	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		UK2Node_ComponentBoundEvent* BoundEvent = Cast<UK2Node_ComponentBoundEvent>(Node);
		if (!BoundEvent) continue;

		TSharedPtr<FJsonObject> Binding = MakeShared<FJsonObject>();
		Binding->SetStringField(TEXT("event_name"), BoundEvent->DelegatePropertyName.ToString());
		Binding->SetStringField(TEXT("widget_name"), BoundEvent->ComponentPropertyName.ToString());
		Binding->SetStringField(TEXT("function"), BoundEvent->GetFunctionName().ToString());
		Bindings.Add(MakeShared<FJsonValueObject>(Binding));
	}

	return Bindings;
}

/**
 * Collect UMG animation info from a Widget Blueprint.
 */
static TArray<TSharedPtr<FJsonValue>> CollectAnimations(UWidgetBlueprint* WidgetBP)
{
	TArray<TSharedPtr<FJsonValue>> AnimArr;

	for (UWidgetAnimation* Anim : WidgetBP->Animations)
	{
		if (!Anim) continue;

		TSharedPtr<FJsonObject> AnimObj = MakeShared<FJsonObject>();
		AnimObj->SetStringField(TEXT("name"), Anim->GetName());

		UMovieScene* MovieScene = Anim->GetMovieScene();
		if (MovieScene)
		{
			FFrameRate TickResolution = MovieScene->GetTickResolution();
			FFrameRate DisplayRate = MovieScene->GetDisplayRate();
			FFrameNumber StartFrame = MovieScene->GetPlaybackRange().GetLowerBoundValue();
			FFrameNumber EndFrame = MovieScene->GetPlaybackRange().GetUpperBoundValue();
			double Duration = TickResolution.IsValid()
				? (EndFrame - StartFrame).Value / TickResolution.AsDecimal()
				: 0.0;
			AnimObj->SetNumberField(TEXT("duration"), Duration);

			// Collect track names
			TArray<TSharedPtr<FJsonValue>> TrackNames;
			for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
			{
				for (UMovieSceneTrack* Track : Binding.GetTracks())
				{
					if (Track)
					{
						TrackNames.Add(MakeShared<FJsonValueString>(Track->GetTrackName().ToString()));
					}
				}
			}
			AnimObj->SetArrayField(TEXT("tracks"), TrackNames);
		}
		else
		{
			AnimObj->SetNumberField(TEXT("duration"), 0.0);
			AnimObj->SetArrayField(TEXT("tracks"), TArray<TSharedPtr<FJsonValue>>());
		}

		AnimArr.Add(MakeShared<FJsonValueObject>(AnimObj));
	}

	return AnimArr;
}

/**
 * Collect MVVM bindings from a Widget Blueprint.
 */
static TArray<TSharedPtr<FJsonValue>> CollectMVVMBindings(UWidgetBlueprint* WidgetBP)
{
	TArray<TSharedPtr<FJsonValue>> BindingsArr;

	UMVVMWidgetBlueprintExtension_View* MVVMExt = UMVVMWidgetBlueprintExtension_View::GetExtension<UMVVMWidgetBlueprintExtension_View>(WidgetBP);
	if (!MVVMExt) return BindingsArr;

	UMVVMBlueprintView* View = MVVMExt->GetBlueprintView();
	if (!View) return BindingsArr;

	for (const FMVVMBlueprintViewBinding& Binding : View->GetBindings())
	{
		TSharedPtr<FJsonObject> BindObj = MakeShared<FJsonObject>();

		// Source (ViewModel) path
		const TArrayView<const FMVVMBlueprintPropertyPath> Sources = Binding.GetSources();
		if (Sources.Num() > 0)
		{
			BindObj->SetStringField(TEXT("viewmodel_property"), Sources[0].GetPropertyPath().ToString());
		}

		// Destination (Widget) path
		const FMVVMBlueprintPropertyPath& Dest = Binding.GetDestinationPath();
		BindObj->SetStringField(TEXT("widget_property"), Dest.GetPropertyPath().ToString());

		// Binding mode
		FString ModeStr;
		switch (Binding.BindingType)
		{
		case EMVVMBindingMode::OneTimeToDestination:   ModeStr = TEXT("OneTime"); break;
		case EMVVMBindingMode::OneWayToDestination:    ModeStr = TEXT("OneWay"); break;
		case EMVVMBindingMode::TwoWay:                 ModeStr = TEXT("TwoWay"); break;
		case EMVVMBindingMode::OneWayToSource:         ModeStr = TEXT("OneWayToSource"); break;
		default:                                       ModeStr = TEXT("Unknown"); break;
		}
		BindObj->SetStringField(TEXT("mode"), ModeStr);

		BindingsArr.Add(MakeShared<FJsonValueObject>(BindObj));
	}

	return BindingsArr;
}

/**
 * Collect Widget variables from a Widget Blueprint.
 */
static TArray<TSharedPtr<FJsonValue>> CollectVariables(UWidgetBlueprint* WidgetBP)
{
	TArray<TSharedPtr<FJsonValue>> VarsArr;

	for (const FBPVariableDescription& Var : WidgetBP->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
		VarObj->SetStringField(TEXT("category"), Var.Category.ToString());
		VarsArr.Add(MakeShared<FJsonValueObject>(VarObj));
	}

	return VarsArr;
}


// ============================================================================
// FDescribeWidgetBlueprintFullAction
// Requirements: 5.1, 5.2, 5.3
// ============================================================================

bool FDescribeWidgetBlueprintFullAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing required parameter 'widget_name'");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FDescribeWidgetBlueprintFullAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString WidgetName = Params->GetStringField(TEXT("widget_name"));

	UWidgetBlueprint* WidgetBP = FindWidgetBlueprintByName(WidgetName);
	if (!WidgetBP)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Widget Blueprint '%s' not found"), *WidgetName),
			TEXT("asset_not_found"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_name"), WidgetBP->GetName());
	Result->SetStringField(TEXT("asset_path"), WidgetBP->GetPathName());

	// Component hierarchy tree
	if (WidgetBP->WidgetTree && WidgetBP->WidgetTree->RootWidget)
	{
		TSharedPtr<FJsonObject> Tree = BuildComponentTreeJson(WidgetBP->WidgetTree->RootWidget, WidgetBP);
		Result->SetObjectField(TEXT("component_tree"), Tree);
	}
	else
	{
		Result->SetObjectField(TEXT("component_tree"), MakeShared<FJsonObject>());
	}

	// Event bindings
	Result->SetArrayField(TEXT("event_bindings"), CollectEventBindings(WidgetBP));

	// Animations
	Result->SetArrayField(TEXT("animations"), CollectAnimations(WidgetBP));

	// MVVM bindings
	Result->SetArrayField(TEXT("mvvm_bindings"), CollectMVVMBindings(WidgetBP));

	// Variables
	Result->SetArrayField(TEXT("variables"), CollectVariables(WidgetBP));

	return Result;
}


// ============================================================================
// FWidgetListAnimationsAction
// Requirements: 6.1
// ============================================================================

bool FWidgetListAnimationsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing required parameter 'widget_name'");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FWidgetListAnimationsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString WidgetName = Params->GetStringField(TEXT("widget_name"));

	UWidgetBlueprint* WidgetBP = FindWidgetBlueprintByName(WidgetName);
	if (!WidgetBP)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Widget Blueprint '%s' not found"), *WidgetName),
			TEXT("asset_not_found"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_name"), WidgetBP->GetName());
	Result->SetArrayField(TEXT("animations"), CollectAnimations(WidgetBP));

	return Result;
}


// ============================================================================
// FWidgetCreateAnimationAction
// Requirements: 6.2, 6.4
// ============================================================================

bool FWidgetCreateAnimationAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing required parameter 'widget_name'");
		return false;
	}
	if (!Params->HasField(TEXT("animation_name")))
	{
		OutError = TEXT("Missing required parameter 'animation_name'");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FWidgetCreateAnimationAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString WidgetName = Params->GetStringField(TEXT("widget_name"));
	FString AnimName = Params->GetStringField(TEXT("animation_name"));
	double Duration = GetOptionalNumber(Params, TEXT("duration"), 1.0);

	UWidgetBlueprint* WidgetBP = FindWidgetBlueprintByName(WidgetName);
	if (!WidgetBP)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Widget Blueprint '%s' not found"), *WidgetName),
			TEXT("asset_not_found"));
	}

	// Check for name conflict (Req 6.4)
	for (UWidgetAnimation* Existing : WidgetBP->Animations)
	{
		if (Existing && Existing->GetName() == AnimName)
		{
			return CreateErrorResponse(
				FString::Printf(TEXT("Animation '%s' already exists in Widget Blueprint '%s'"), *AnimName, *WidgetName),
				TEXT("name_conflict"));
		}
	}

	// Create the animation
	UWidgetAnimation* NewAnim = NewObject<UWidgetAnimation>(WidgetBP, FName(*AnimName), RF_Transactional);
	if (!NewAnim)
	{
		return CreateErrorResponse(TEXT("Failed to create UWidgetAnimation object"));
	}

	// Initialize the MovieScene
	UMovieScene* MovieScene = NewObject<UMovieScene>(NewAnim, FName(*(AnimName + TEXT("_MovieScene"))), RF_Transactional);
	if (MovieScene)
	{
		NewAnim->MovieScene = MovieScene;

		// Set playback range based on duration
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FFrameNumber EndFrame = (Duration * TickResolution.AsDecimal());
		MovieScene->SetPlaybackRange(FFrameNumber(0), EndFrame.Value);
	}

	WidgetBP->Animations.Add(NewAnim);

	MarkWidgetBlueprintDirty(WidgetBP, Context);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_name"), WidgetBP->GetName());
	Result->SetStringField(TEXT("animation_name"), AnimName);
	Result->SetNumberField(TEXT("duration"), Duration);
	return Result;
}


// ============================================================================
// FWidgetAddAnimationTrackAction
// Requirements: 6.3
// ============================================================================

bool FWidgetAddAnimationTrackAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing required parameter 'widget_name'");
		return false;
	}
	if (!Params->HasField(TEXT("animation_name")))
	{
		OutError = TEXT("Missing required parameter 'animation_name'");
		return false;
	}
	if (!Params->HasField(TEXT("component_name")))
	{
		OutError = TEXT("Missing required parameter 'component_name'");
		return false;
	}
	if (!Params->HasField(TEXT("property_name")))
	{
		OutError = TEXT("Missing required parameter 'property_name'");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FWidgetAddAnimationTrackAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString WidgetName = Params->GetStringField(TEXT("widget_name"));
	FString AnimName = Params->GetStringField(TEXT("animation_name"));
	FString ComponentName = Params->GetStringField(TEXT("component_name"));
	FString PropertyName = Params->GetStringField(TEXT("property_name"));

	UWidgetBlueprint* WidgetBP = FindWidgetBlueprintByName(WidgetName);
	if (!WidgetBP)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Widget Blueprint '%s' not found"), *WidgetName),
			TEXT("asset_not_found"));
	}

	// Find the animation
	UWidgetAnimation* TargetAnim = nullptr;
	for (UWidgetAnimation* Anim : WidgetBP->Animations)
	{
		if (Anim && Anim->GetName() == AnimName)
		{
			TargetAnim = Anim;
			break;
		}
	}

	if (!TargetAnim)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Animation '%s' not found in Widget Blueprint '%s'"), *AnimName, *WidgetName),
			TEXT("animation_not_found"));
	}

	UMovieScene* MovieScene = TargetAnim->GetMovieScene();
	if (!MovieScene)
	{
		return CreateErrorResponse(TEXT("Animation has no MovieScene"));
	}

	// Find the target component in the widget tree
	UWidget* TargetWidget = WidgetBP->WidgetTree ? WidgetBP->WidgetTree->FindWidget(FName(*ComponentName)) : nullptr;
	if (!TargetWidget)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Component '%s' not found in Widget Blueprint '%s'"), *ComponentName, *WidgetName),
			TEXT("component_not_found"));
	}

	// Resolve the property to a track type
	// Supported animatable properties: Opacity, RenderTransform, ColorAndOpacity, etc.
	// We use the Sequencer's property track system via MovieScene
	FGuid ObjectBindingGuid;

	// Check if a binding already exists for this component
	for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
	{
		if (Binding.GetName() == ComponentName)
		{
			ObjectBindingGuid = Binding.GetObjectGuid();
			break;
		}
	}

	// If no binding exists, create one
	if (!ObjectBindingGuid.IsValid())
	{
		ObjectBindingGuid = FGuid::NewGuid();
		MovieScene->AddPossessable(ComponentName, TargetWidget->GetClass());

		// Find the possessable we just added and set its GUID
		// The AddPossessable returns the index, but we need to find it by name
		for (int32 i = 0; i < MovieScene->GetPossessableCount(); i++)
		{
			const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(i);
			if (Possessable.GetName() == ComponentName)
			{
				ObjectBindingGuid = Possessable.GetGuid();
				break;
			}
		}
	}

	// Map property name to track class
	// Common animatable UMG properties: Opacity, RenderTransform, ColorAndOpacity
	FName PropertyFName(*PropertyName);

	// Add a float track for scalar properties (Opacity), or a transform track, etc.
	UMovieSceneTrack* NewTrack = nullptr;

	// Use FindTrackClass to get the appropriate track type for the property
	// For simplicity, we add a generic property track
	UMovieScenePropertyTrack* PropertyTrack = MovieScene->AddTrack<UMovieScenePropertyTrack>(ObjectBindingGuid);
	if (PropertyTrack)
	{
		PropertyTrack->SetPropertyNameAndPath(PropertyFName, PropertyName);
		NewTrack = PropertyTrack;
	}

	if (!NewTrack)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Failed to add track for property '%s' on component '%s'"), *PropertyName, *ComponentName));
	}

	MarkWidgetBlueprintDirty(WidgetBP, Context);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_name"), WidgetBP->GetName());
	Result->SetStringField(TEXT("animation_name"), AnimName);
	Result->SetStringField(TEXT("component_name"), ComponentName);
	Result->SetStringField(TEXT("property_name"), PropertyName);
	return Result;
}


// ============================================================================
// FWidgetGetReferencesAction
// Requirements: 7.1
// ============================================================================

bool FWidgetGetReferencesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing required parameter 'widget_name'");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FWidgetGetReferencesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString WidgetName = Params->GetStringField(TEXT("widget_name"));

	UWidgetBlueprint* WidgetBP = FindWidgetBlueprintByName(WidgetName);
	if (!WidgetBP)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Widget Blueprint '%s' not found"), *WidgetName),
			TEXT("asset_not_found"));
	}

	// Collect child Widget Blueprint references by scanning the widget tree
	// for UserWidget sub-instances (widgets that reference other Widget Blueprints)
	TArray<TSharedPtr<FJsonValue>> ReferencesArr;
	TSet<FString> SeenPaths;

	if (WidgetBP->WidgetTree)
	{
		WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (!Widget) return;

			// Check if this widget is a UserWidget (sub-widget reference)
			UUserWidget* UserWidget = Cast<UUserWidget>(Widget);
			if (UserWidget)
			{
				UClass* WidgetClass = UserWidget->GetClass();
				if (WidgetClass)
				{
					UBlueprint* ReferencedBP = Cast<UBlueprint>(WidgetClass->ClassGeneratedBy);
					if (ReferencedBP)
					{
						FString Path = ReferencedBP->GetPathName();
						if (!SeenPaths.Contains(Path))
						{
							SeenPaths.Add(Path);
							TSharedPtr<FJsonObject> RefObj = MakeShared<FJsonObject>();
							RefObj->SetStringField(TEXT("name"), ReferencedBP->GetName());
							RefObj->SetStringField(TEXT("asset_path"), Path);
							RefObj->SetStringField(TEXT("component_name"), Widget->GetName());
							ReferencesArr.Add(MakeShared<FJsonValueObject>(RefObj));
						}
					}
				}
			}
		});
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_name"), WidgetBP->GetName());
	Result->SetStringField(TEXT("asset_path"), WidgetBP->GetPathName());
	Result->SetArrayField(TEXT("references"), ReferencesArr);
	Result->SetNumberField(TEXT("reference_count"), ReferencesArr.Num());
	return Result;
}


// ============================================================================
// FWidgetGetReferencersAction
// Requirements: 7.2
// ============================================================================

bool FWidgetGetReferencersAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing required parameter 'widget_name'");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FWidgetGetReferencersAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString WidgetName = Params->GetStringField(TEXT("widget_name"));

	UWidgetBlueprint* WidgetBP = FindWidgetBlueprintByName(WidgetName);
	if (!WidgetBP)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Widget Blueprint '%s' not found"), *WidgetName),
			TEXT("asset_not_found"));
	}

	// Use the Asset Registry to find referencers
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FName PackageName = WidgetBP->GetOutermost()->GetFName();
	TArray<FName> Referencers;
	AssetRegistry.GetReferencers(PackageName, Referencers);

	TArray<TSharedPtr<FJsonValue>> ReferencersArr;
	for (const FName& ReferencerName : Referencers)
	{
		FString ReferencerPath = ReferencerName.ToString();

		// Skip engine/script packages
		if (ReferencerPath.StartsWith(TEXT("/Script/")) || ReferencerPath.StartsWith(TEXT("/Engine/")))
		{
			continue;
		}

		TSharedPtr<FJsonObject> RefObj = MakeShared<FJsonObject>();
		RefObj->SetStringField(TEXT("package_name"), ReferencerPath);

		// Try to get asset type info
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPackageName(ReferencerName, Assets, true);
		if (Assets.Num() > 0)
		{
			RefObj->SetStringField(TEXT("asset_name"), Assets[0].AssetName.ToString());
			RefObj->SetStringField(TEXT("asset_class"), Assets[0].AssetClassPath.GetAssetName().ToString());
		}

		ReferencersArr.Add(MakeShared<FJsonValueObject>(RefObj));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_name"), WidgetBP->GetName());
	Result->SetStringField(TEXT("asset_path"), WidgetBP->GetPathName());
	Result->SetArrayField(TEXT("referencers"), ReferencersArr);
	Result->SetNumberField(TEXT("referencer_count"), ReferencersArr.Num());
	return Result;
}


// ============================================================================
// FWidgetBatchGetStylesAction
// Requirements: 8.1, 8.2
// ============================================================================

bool FWidgetBatchGetStylesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("widget_name")))
	{
		OutError = TEXT("Missing required parameter 'widget_name'");
		return false;
	}
	return true;
}

/**
 * Extract style properties from a single widget component.
 */
static TSharedPtr<FJsonObject> ExtractWidgetStyle(UWidget* Widget)
{
	if (!Widget) return nullptr;

	TSharedPtr<FJsonObject> StyleObj = MakeShared<FJsonObject>();
	StyleObj->SetStringField(TEXT("name"), Widget->GetName());
	StyleObj->SetStringField(TEXT("type"), Widget->GetClass()->GetName());

	// Render opacity (common to all widgets)
	StyleObj->SetNumberField(TEXT("render_opacity"), Widget->GetRenderOpacity());

	// TextBlock-specific styles
	UTextBlock* TextBlock = Cast<UTextBlock>(Widget);
	if (TextBlock)
	{
		FSlateFontInfo FontInfo = TextBlock->GetFont();
		StyleObj->SetNumberField(TEXT("font_size"), FontInfo.Size);
		StyleObj->SetStringField(TEXT("font_family"), FontInfo.FontObject ? FontInfo.FontObject->GetName() : TEXT("Default"));

		FLinearColor Color = TextBlock->GetColorAndOpacity().GetSpecifiedColor();
		TArray<TSharedPtr<FJsonValue>> ColorArr;
		ColorArr.Add(MakeShared<FJsonValueNumber>(Color.R));
		ColorArr.Add(MakeShared<FJsonValueNumber>(Color.G));
		ColorArr.Add(MakeShared<FJsonValueNumber>(Color.B));
		ColorArr.Add(MakeShared<FJsonValueNumber>(Color.A));
		StyleObj->SetArrayField(TEXT("color"), ColorArr);
	}

	// RichTextBlock-specific styles
	URichTextBlock* RichText = Cast<URichTextBlock>(Widget);
	if (RichText)
	{
		StyleObj->SetStringField(TEXT("text_style_class"), TEXT("RichTextBlock"));
	}

	// Image-specific styles
	UImage* Image = Cast<UImage>(Widget);
	if (Image)
	{
		FLinearColor Tint = Image->GetColorAndOpacity();
		TArray<TSharedPtr<FJsonValue>> TintArr;
		TintArr.Add(MakeShared<FJsonValueNumber>(Tint.R));
		TintArr.Add(MakeShared<FJsonValueNumber>(Tint.G));
		TintArr.Add(MakeShared<FJsonValueNumber>(Tint.B));
		TintArr.Add(MakeShared<FJsonValueNumber>(Tint.A));
		StyleObj->SetArrayField(TEXT("color"), TintArr);
	}

	// Border-specific styles
	UBorder* Border = Cast<UBorder>(Widget);
	if (Border)
	{
		FLinearColor BrushColor = Border->GetBrushColor();
		TArray<TSharedPtr<FJsonValue>> BrushArr;
		BrushArr.Add(MakeShared<FJsonValueNumber>(BrushColor.R));
		BrushArr.Add(MakeShared<FJsonValueNumber>(BrushColor.G));
		BrushArr.Add(MakeShared<FJsonValueNumber>(BrushColor.B));
		BrushArr.Add(MakeShared<FJsonValueNumber>(BrushColor.A));
		StyleObj->SetArrayField(TEXT("brush_color"), BrushArr);

		FMargin ContentPadding = Border->GetPadding();
		TSharedPtr<FJsonObject> PaddingObj = MakeShared<FJsonObject>();
		PaddingObj->SetNumberField(TEXT("left"), ContentPadding.Left);
		PaddingObj->SetNumberField(TEXT("top"), ContentPadding.Top);
		PaddingObj->SetNumberField(TEXT("right"), ContentPadding.Right);
		PaddingObj->SetNumberField(TEXT("bottom"), ContentPadding.Bottom);
		StyleObj->SetObjectField(TEXT("padding"), PaddingObj);
	}

	// ProgressBar-specific styles
	UProgressBar* ProgressBar = Cast<UProgressBar>(Widget);
	if (ProgressBar)
	{
		StyleObj->SetNumberField(TEXT("percent"), ProgressBar->GetPercent());

		FLinearColor FillColor = ProgressBar->GetFillColorAndOpacity();
		TArray<TSharedPtr<FJsonValue>> FillArr;
		FillArr.Add(MakeShared<FJsonValueNumber>(FillColor.R));
		FillArr.Add(MakeShared<FJsonValueNumber>(FillColor.G));
		FillArr.Add(MakeShared<FJsonValueNumber>(FillColor.B));
		FillArr.Add(MakeShared<FJsonValueNumber>(FillColor.A));
		StyleObj->SetArrayField(TEXT("fill_color"), FillArr);
	}

	// Slot-based padding/margin (CanvasPanelSlot)
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot);
	if (CanvasSlot)
	{
		TArray<TSharedPtr<FJsonValue>> PosArr;
		PosArr.Add(MakeShared<FJsonValueNumber>(CanvasSlot->GetPosition().X));
		PosArr.Add(MakeShared<FJsonValueNumber>(CanvasSlot->GetPosition().Y));
		StyleObj->SetArrayField(TEXT("position"), PosArr);

		TArray<TSharedPtr<FJsonValue>> SizeArr;
		SizeArr.Add(MakeShared<FJsonValueNumber>(CanvasSlot->GetSize().X));
		SizeArr.Add(MakeShared<FJsonValueNumber>(CanvasSlot->GetSize().Y));
		StyleObj->SetArrayField(TEXT("size"), SizeArr);
	}

	return StyleObj;
}

TSharedPtr<FJsonObject> FWidgetBatchGetStylesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString WidgetName = Params->GetStringField(TEXT("widget_name"));
	FString FilterType = GetOptionalString(Params, TEXT("filter_type"));

	UWidgetBlueprint* WidgetBP = FindWidgetBlueprintByName(WidgetName);
	if (!WidgetBP)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Widget Blueprint '%s' not found"), *WidgetName),
			TEXT("asset_not_found"));
	}

	TArray<TSharedPtr<FJsonValue>> StylesArr;

	if (WidgetBP->WidgetTree)
	{
		WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (!Widget) return;

			// Apply type filter if specified (Req 8.2)
			if (!FilterType.IsEmpty())
			{
				FString ClassName = Widget->GetClass()->GetName();
				if (!ClassName.Contains(FilterType))
				{
					return;
				}
			}

			TSharedPtr<FJsonObject> StyleObj = ExtractWidgetStyle(Widget);
			if (StyleObj.IsValid())
			{
				StylesArr.Add(MakeShared<FJsonValueObject>(StyleObj));
			}
		});
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_name"), WidgetBP->GetName());
	Result->SetStringField(TEXT("asset_path"), WidgetBP->GetPathName());
	if (!FilterType.IsEmpty())
	{
		Result->SetStringField(TEXT("filter_type"), FilterType);
	}
	Result->SetArrayField(TEXT("styles"), StylesArr);
	Result->SetNumberField(TEXT("component_count"), StylesArr.Num());
	return Result;
}
