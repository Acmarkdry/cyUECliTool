// Copyright (c) 2025 zolnoor. All rights reserved.
// Animation Analysis Actions — v0.4.0 platform extensions
// Requirements: 9.1, 9.2, 9.3, 10.1, 10.2, 10.3, 11.1, 11.2, 11.3, 12.1, 12.2

#include "Actions/AnimAnalysisActions.h"
#include "Actions/AnimGraphActions.h"
#include "MCPCommonUtils.h"
#include "MCPContext.h"

// Core UE
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"

// AnimGraph / AnimBlueprint
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimStateNode.h"
#include "AnimStateEntryNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"


// ============================================================================
// Local Helpers
// ============================================================================

/**
 * Find an animation asset by name or path. Searches /Game/ paths and the
 * Asset Registry. Returns nullptr if not found.
 */
template<typename T>
static T* FindAnimAssetByNameOrPath(const FString& AssetName)
{
	// Direct path load
	if (AssetName.StartsWith(TEXT("/Game/")) || AssetName.StartsWith(TEXT("/Engine/")))
	{
		T* Asset = LoadObject<T>(nullptr, *AssetName);
		if (Asset) return Asset;

		// Try with explicit class path suffix
		if (UEditorAssetLibrary::DoesAssetExist(AssetName))
		{
			UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetName);
			T* Casted = Cast<T>(Loaded);
			if (Casted) return Casted;
		}
	}

	// Search common paths
	TArray<FString> PriorityPaths = {
		FString::Printf(TEXT("/Game/Animations/%s"), *AssetName),
		FString::Printf(TEXT("/Game/Characters/Animations/%s"), *AssetName),
		FString::Printf(TEXT("/Game/%s"), *AssetName),
	};

	for (const FString& Path : PriorityPaths)
	{
		if (UEditorAssetLibrary::DoesAssetExist(Path))
		{
			UObject* Loaded = UEditorAssetLibrary::LoadAsset(Path);
			T* Casted = Cast<T>(Loaded);
			if (Casted) return Casted;
		}
	}

	// Asset Registry fallback
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssetsByClass(T::StaticClass()->GetClassPathName(), AssetList, true);

	for (const FAssetData& AssetData : AssetList)
	{
		if (AssetData.AssetName.ToString() == AssetName && AssetData.PackagePath.ToString().StartsWith(TEXT("/Game")))
		{
			return Cast<T>(AssetData.GetAsset());
		}
	}

	return nullptr;
}

/**
 * Find an AnimBlueprint by name or path.
 */
static UAnimBlueprint* FindAnimBlueprintByName(const FString& BlueprintName)
{
	// Try the standard blueprint finder first
	UBlueprint* BP = FMCPCommonUtils::FindBlueprint(BlueprintName);
	if (BP)
	{
		UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(BP);
		if (AnimBP) return AnimBP;
	}

	// Fallback: search Asset Registry for UAnimBlueprint specifically
	return FindAnimAssetByNameOrPath<UAnimBlueprint>(BlueprintName);
}

/**
 * Collect all anim asset references from an AnimBlueprint by scanning all graphs.
 */
static TArray<FString> CollectAnimAssetReferences(UAnimBlueprint* AnimBP)
{
	TArray<FString> References;
	TSet<FString> SeenPaths;

	auto ScanGraph = [&](UEdGraph* Graph)
	{
		if (!Graph) return;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;

			// AnimSequence Player
			if (const UAnimGraphNode_SequencePlayer* SeqPlayer = Cast<UAnimGraphNode_SequencePlayer>(Node))
			{
				if (SeqPlayer->Node.GetSequence())
				{
					FString Path = SeqPlayer->Node.GetSequence()->GetPathName();
					if (!SeenPaths.Contains(Path))
					{
						SeenPaths.Add(Path);
						References.Add(Path);
					}
				}
			}

			// BlendSpace Player
			if (const UAnimGraphNode_BlendSpacePlayer* BSPlayer = Cast<UAnimGraphNode_BlendSpacePlayer>(Node))
			{
				if (BSPlayer->Node.GetBlendSpace())
				{
					FString Path = BSPlayer->Node.GetBlendSpace()->GetPathName();
					if (!SeenPaths.Contains(Path))
					{
						SeenPaths.Add(Path);
						References.Add(Path);
					}
				}
			}

			// Recurse into sub-graphs
			TArray<UEdGraph*> SubGraphs = Node->GetSubGraphs();
			for (UEdGraph* Sub : SubGraphs)
			{
				if (Sub)
				{
					// Use a simple lambda to avoid deep recursion issues
					for (UEdGraphNode* SubNode : Sub->Nodes)
					{
						if (!SubNode) continue;
						if (const UAnimGraphNode_SequencePlayer* SubSeq = Cast<UAnimGraphNode_SequencePlayer>(SubNode))
						{
							if (SubSeq->Node.GetSequence())
							{
								FString Path = SubSeq->Node.GetSequence()->GetPathName();
								if (!SeenPaths.Contains(Path))
								{
									SeenPaths.Add(Path);
									References.Add(Path);
								}
							}
						}
						if (const UAnimGraphNode_BlendSpacePlayer* SubBS = Cast<UAnimGraphNode_BlendSpacePlayer>(SubNode))
						{
							if (SubBS->Node.GetBlendSpace())
							{
								FString Path = SubBS->Node.GetBlendSpace()->GetPathName();
								if (!SeenPaths.Contains(Path))
								{
									SeenPaths.Add(Path);
									References.Add(Path);
								}
							}
						}
					}
				}
			}
		}
	};

	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		ScanGraph(Graph);
	}
	for (UEdGraph* Graph : AnimBP->UbergraphPages)
	{
		ScanGraph(Graph);
	}

	return References;
}

/**
 * Collect state machine summaries from an AnimBlueprint.
 */
static TArray<TSharedPtr<FJsonValue>> CollectStateMachineSummaries(UAnimBlueprint* AnimBP)
{
	TArray<TSharedPtr<FJsonValue>> StateMachines;

	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		if (!Graph) continue;

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node);
			if (!SMNode || !SMNode->EditorStateMachineGraph) continue;

			UEdGraph* SMGraph = SMNode->EditorStateMachineGraph;

			int32 StateCount = 0;
			int32 TransitionCount = 0;
			FString EntryStateName;

			for (UEdGraphNode* SMChild : SMGraph->Nodes)
			{
				if (!SMChild) continue;

				if (Cast<UAnimStateNode>(SMChild))
				{
					StateCount++;
				}
				else if (Cast<UAnimStateTransitionNode>(SMChild))
				{
					TransitionCount++;
				}
				else if (UAnimStateEntryNode* EntryNode = Cast<UAnimStateEntryNode>(SMChild))
				{
					for (UEdGraphPin* Pin : EntryNode->Pins)
					{
						if (Pin && Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() > 0)
						{
							UEdGraphNode* LinkedNode = Pin->LinkedTo[0]->GetOwningNode();
							if (UAnimStateNode* DefaultState = Cast<UAnimStateNode>(LinkedNode))
							{
								EntryStateName = DefaultState->GetStateName();
							}
						}
					}
				}
			}

			TSharedPtr<FJsonObject> SMObj = MakeShared<FJsonObject>();
			SMObj->SetStringField(TEXT("name"), SMGraph->GetName());
			SMObj->SetNumberField(TEXT("state_count"), StateCount);
			SMObj->SetNumberField(TEXT("transition_count"), TransitionCount);
			SMObj->SetStringField(TEXT("entry_state"), EntryStateName);
			StateMachines.Add(MakeShared<FJsonValueObject>(SMObj));
		}
	}

	return StateMachines;
}


// ============================================================================
// FDescribeAnimBlueprintFullAction
// Requirements: 9.1, 9.2, 9.3
// ============================================================================

bool FDescribeAnimBlueprintFullAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString BlueprintName;
	if (!GetRequiredString(Params, TEXT("blueprint_name"), BlueprintName, OutError))
	{
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FDescribeAnimBlueprintFullAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName;
	FString Error;
	GetRequiredString(Params, TEXT("blueprint_name"), BlueprintName, Error);

	UAnimBlueprint* AnimBP = FindAnimBlueprintByName(BlueprintName);
	if (!AnimBP)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Animation Blueprint '%s' not found"), *BlueprintName),
			TEXT("asset_not_found"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("blueprint_name"), AnimBP->GetName());

	// Skeleton reference (Req 9.1)
	FString SkeletonPath;
	if (AnimBP->TargetSkeleton)
	{
		SkeletonPath = AnimBP->TargetSkeleton->GetPathName();
	}
	Result->SetStringField(TEXT("skeleton"), SkeletonPath);

	// State machines with summaries (Req 9.1, 9.2)
	Result->SetArrayField(TEXT("state_machines"), CollectStateMachineSummaries(AnimBP));

	// Anim asset references (Req 9.1)
	TArray<FString> AssetRefs = CollectAnimAssetReferences(AnimBP);
	TArray<TSharedPtr<FJsonValue>> AssetRefsArr;
	for (const FString& Ref : AssetRefs)
	{
		AssetRefsArr.Add(MakeShared<FJsonValueString>(Ref));
	}
	Result->SetArrayField(TEXT("anim_asset_references"), AssetRefsArr);

	// Variables (Req 9.1)
	TArray<TSharedPtr<FJsonValue>> VarsArr;
	for (const FBPVariableDescription& Var : AnimBP->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
		VarsArr.Add(MakeShared<FJsonValueObject>(VarObj));
	}
	Result->SetArrayField(TEXT("variables"), VarsArr);

	// EventGraph summary (Req 9.1)
	TSharedPtr<FJsonObject> EventGraphSummary = MakeShared<FJsonObject>();
	int32 EventGraphNodeCount = 0;
	TArray<TSharedPtr<FJsonValue>> EventNodes;

	for (UEdGraph* Graph : AnimBP->UbergraphPages)
	{
		if (!Graph) continue;
		EventGraphNodeCount += Graph->Nodes.Num();

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			FString NodeClass = Node->GetClass()->GetName();
			if (NodeClass.Contains(TEXT("K2Node_Event")) || NodeClass.Contains(TEXT("AnimGraphNode")))
			{
				FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
				if (!Title.IsEmpty())
				{
					EventNodes.Add(MakeShared<FJsonValueString>(Title));
				}
			}
		}
	}
	EventGraphSummary->SetNumberField(TEXT("node_count"), EventGraphNodeCount);
	EventGraphSummary->SetArrayField(TEXT("event_nodes"), EventNodes);
	Result->SetObjectField(TEXT("event_graph_summary"), EventGraphSummary);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// FAnimDescribeMontageAction
// Requirements: 10.1, 10.3
// ============================================================================

bool FAnimDescribeMontageAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString AssetName;
	if (!GetRequiredString(Params, TEXT("asset_name"), AssetName, OutError))
	{
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAnimDescribeMontageAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString AssetName;
	FString Error;
	GetRequiredString(Params, TEXT("asset_name"), AssetName, Error);

	UAnimMontage* Montage = FindAnimAssetByNameOrPath<UAnimMontage>(AssetName);
	if (!Montage)
	{
		// Check if asset exists but is wrong type
		UObject* GenericAsset = FindAnimAssetByNameOrPath<UAnimSequenceBase>(AssetName);
		if (GenericAsset)
		{
			return CreateErrorResponse(
				FString::Printf(TEXT("Asset '%s' is not an AnimMontage (found %s)"), *AssetName, *GenericAsset->GetClass()->GetName()),
				TEXT("type_mismatch"));
		}
		return CreateErrorResponse(
			FString::Printf(TEXT("AnimMontage '%s' not found"), *AssetName),
			TEXT("asset_not_found"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("montage_name"), Montage->GetName());

	// Sections (Req 10.1)
	TArray<TSharedPtr<FJsonValue>> SectionsArr;
	for (int32 i = 0; i < Montage->CompositeSections.Num(); i++)
	{
		const FCompositeSection& Section = Montage->CompositeSections[i];
		TSharedPtr<FJsonObject> SectionObj = MakeShared<FJsonObject>();
		SectionObj->SetStringField(TEXT("name"), Section.SectionName.ToString());
		SectionObj->SetNumberField(TEXT("start_time"), Section.GetTime());

		// Calculate end time: next section start or montage end
		float EndTime = Montage->GetPlayLength();
		if (i + 1 < Montage->CompositeSections.Num())
		{
			EndTime = Montage->CompositeSections[i + 1].GetTime();
		}
		SectionObj->SetNumberField(TEXT("end_time"), EndTime);

		SectionsArr.Add(MakeShared<FJsonValueObject>(SectionObj));
	}
	Result->SetArrayField(TEXT("sections"), SectionsArr);

	// Slot name (Req 10.1)
	FString SlotName;
	if (Montage->SlotAnimTracks.Num() > 0)
	{
		SlotName = Montage->SlotAnimTracks[0].SlotName.ToString();
	}
	Result->SetStringField(TEXT("slot_name"), SlotName);

	// Notifies (Req 10.1)
	TArray<TSharedPtr<FJsonValue>> NotifiesArr;
	for (const FAnimNotifyEvent& NotifyEvent : Montage->Notifies)
	{
		TSharedPtr<FJsonObject> NotifyObj = MakeShared<FJsonObject>();
		NotifyObj->SetStringField(TEXT("name"), NotifyEvent.NotifyName.ToString());

		FString NotifyType;
		if (NotifyEvent.Notify)
		{
			NotifyType = NotifyEvent.Notify->GetClass()->GetName();
		}
		else if (NotifyEvent.NotifyStateClass)
		{
			NotifyType = NotifyEvent.NotifyStateClass->GetClass()->GetName();
		}
		else
		{
			NotifyType = TEXT("Custom");
		}
		NotifyObj->SetStringField(TEXT("type"), NotifyType);
		NotifyObj->SetNumberField(TEXT("trigger_time"), NotifyEvent.GetTriggerTime());
		NotifyObj->SetStringField(TEXT("track"), FString::FromInt(NotifyEvent.TrackIndex));

		NotifiesArr.Add(MakeShared<FJsonValueObject>(NotifyObj));
	}
	Result->SetArrayField(TEXT("notifies"), NotifiesArr);

	// Anim sequences referenced (Req 10.1)
	TArray<TSharedPtr<FJsonValue>> AnimSeqArr;
	TSet<FString> SeenSeqs;
	for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
	{
		for (const FAnimSegment& Segment : SlotTrack.AnimTrack.AnimSegments)
		{
			if (Segment.GetAnimReference())
			{
				FString Path = Segment.GetAnimReference()->GetPathName();
				if (!SeenSeqs.Contains(Path))
				{
					SeenSeqs.Add(Path);
					AnimSeqArr.Add(MakeShared<FJsonValueString>(Path));
				}
			}
		}
	}
	Result->SetArrayField(TEXT("anim_sequences"), AnimSeqArr);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// FAnimDescribeBlendSpaceAction
// Requirements: 10.2, 10.3
// ============================================================================

bool FAnimDescribeBlendSpaceAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString AssetName;
	if (!GetRequiredString(Params, TEXT("asset_name"), AssetName, OutError))
	{
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAnimDescribeBlendSpaceAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString AssetName;
	FString Error;
	GetRequiredString(Params, TEXT("asset_name"), AssetName, Error);

	UBlendSpace* BlendSpace = FindAnimAssetByNameOrPath<UBlendSpace>(AssetName);
	if (!BlendSpace)
	{
		// Check if asset exists but is wrong type
		UObject* GenericAsset = FindAnimAssetByNameOrPath<UAnimSequenceBase>(AssetName);
		if (GenericAsset)
		{
			return CreateErrorResponse(
				FString::Printf(TEXT("Asset '%s' is not a BlendSpace (found %s)"), *AssetName, *GenericAsset->GetClass()->GetName()),
				TEXT("type_mismatch"));
		}
		return CreateErrorResponse(
			FString::Printf(TEXT("BlendSpace '%s' not found"), *AssetName),
			TEXT("asset_not_found"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("blendspace_name"), BlendSpace->GetName());

	// Axis configuration (Req 10.2)
	TArray<TSharedPtr<FJsonValue>> AxesArr;

	// X axis
	{
		TSharedPtr<FJsonObject> AxisObj = MakeShared<FJsonObject>();
		AxisObj->SetStringField(TEXT("axis"), TEXT("X"));
		AxisObj->SetStringField(TEXT("parameter_name"), BlendSpace->GetBlendParameter(0).DisplayName);
		AxisObj->SetNumberField(TEXT("min"), BlendSpace->GetBlendParameter(0).Min);
		AxisObj->SetNumberField(TEXT("max"), BlendSpace->GetBlendParameter(0).Max);
		AxesArr.Add(MakeShared<FJsonValueObject>(AxisObj));
	}

	// Y axis
	{
		TSharedPtr<FJsonObject> AxisObj = MakeShared<FJsonObject>();
		AxisObj->SetStringField(TEXT("axis"), TEXT("Y"));
		AxisObj->SetStringField(TEXT("parameter_name"), BlendSpace->GetBlendParameter(1).DisplayName);
		AxisObj->SetNumberField(TEXT("min"), BlendSpace->GetBlendParameter(1).Min);
		AxisObj->SetNumberField(TEXT("max"), BlendSpace->GetBlendParameter(1).Max);
		AxesArr.Add(MakeShared<FJsonValueObject>(AxisObj));
	}

	Result->SetArrayField(TEXT("axes"), AxesArr);

	// Sample points (Req 10.2)
	TArray<TSharedPtr<FJsonValue>> SamplesArr;
	TSet<FString> SeenAnimSeqs;
	TArray<TSharedPtr<FJsonValue>> AnimSeqArr;

	const TArray<FBlendSample>& Samples = BlendSpace->GetBlendSamples();
	for (const FBlendSample& Sample : Samples)
	{
		TSharedPtr<FJsonObject> SampleObj = MakeShared<FJsonObject>();
		SampleObj->SetNumberField(TEXT("x"), Sample.SampleValue.X);
		SampleObj->SetNumberField(TEXT("y"), Sample.SampleValue.Y);

		if (Sample.Animation)
		{
			FString AnimPath = Sample.Animation->GetPathName();
			SampleObj->SetStringField(TEXT("animation"), AnimPath);

			if (!SeenAnimSeqs.Contains(AnimPath))
			{
				SeenAnimSeqs.Add(AnimPath);
				AnimSeqArr.Add(MakeShared<FJsonValueString>(AnimPath));
			}
		}
		else
		{
			SampleObj->SetStringField(TEXT("animation"), TEXT("None"));
		}

		SamplesArr.Add(MakeShared<FJsonValueObject>(SampleObj));
	}

	Result->SetArrayField(TEXT("sample_points"), SamplesArr);
	Result->SetArrayField(TEXT("anim_sequences"), AnimSeqArr);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// FAnimListNotifiesAction
// Requirements: 11.1
// ============================================================================

bool FAnimListNotifiesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString AssetName;
	if (!GetRequiredString(Params, TEXT("asset_name"), AssetName, OutError))
	{
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAnimListNotifiesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString AssetName;
	FString Error;
	GetRequiredString(Params, TEXT("asset_name"), AssetName, Error);

	UAnimSequenceBase* AnimAsset = FindAnimAssetByNameOrPath<UAnimSequenceBase>(AssetName);
	if (!AnimAsset)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Animation asset '%s' not found"), *AssetName),
			TEXT("asset_not_found"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_name"), AnimAsset->GetName());
	Result->SetStringField(TEXT("asset_type"), AnimAsset->GetClass()->GetName());

	TArray<TSharedPtr<FJsonValue>> NotifiesArr;

	for (const FAnimNotifyEvent& NotifyEvent : AnimAsset->Notifies)
	{
		TSharedPtr<FJsonObject> NotifyObj = MakeShared<FJsonObject>();
		NotifyObj->SetStringField(TEXT("name"), NotifyEvent.NotifyName.ToString());

		FString NotifyType;
		bool bIsState = false;
		if (NotifyEvent.Notify)
		{
			NotifyType = NotifyEvent.Notify->GetClass()->GetName();
		}
		else if (NotifyEvent.NotifyStateClass)
		{
			NotifyType = NotifyEvent.NotifyStateClass->GetClass()->GetName();
			bIsState = true;
		}
		else
		{
			NotifyType = TEXT("Custom");
		}

		NotifyObj->SetStringField(TEXT("type"), NotifyType);
		NotifyObj->SetStringField(TEXT("category"), bIsState ? TEXT("AnimNotifyState") : TEXT("AnimNotify"));
		NotifyObj->SetNumberField(TEXT("trigger_time"), NotifyEvent.GetTriggerTime());
		NotifyObj->SetNumberField(TEXT("duration"), NotifyEvent.GetDuration());
		NotifyObj->SetStringField(TEXT("track"), FString::FromInt(NotifyEvent.TrackIndex));

		NotifiesArr.Add(MakeShared<FJsonValueObject>(NotifyObj));
	}

	Result->SetArrayField(TEXT("notifies"), NotifiesArr);
	Result->SetNumberField(TEXT("notify_count"), NotifiesArr.Num());

	return CreateSuccessResponse(Result);
}


// ============================================================================
// FAnimAddNotifyAction
// Requirements: 11.2
// ============================================================================

bool FAnimAddNotifyAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString AssetName;
	if (!GetRequiredString(Params, TEXT("asset_name"), AssetName, OutError))
	{
		return false;
	}

	FString NotifyName;
	if (!GetRequiredString(Params, TEXT("notify_name"), NotifyName, OutError))
	{
		return false;
	}

	if (!Params->HasField(TEXT("time")))
	{
		OutError = TEXT("Required parameter 'time' is missing");
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FAnimAddNotifyAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString AssetName, NotifyName, Error;
	GetRequiredString(Params, TEXT("asset_name"), AssetName, Error);
	GetRequiredString(Params, TEXT("notify_name"), NotifyName, Error);
	double Time = Params->GetNumberField(TEXT("time"));
	int32 TrackIndex = static_cast<int32>(GetOptionalNumber(Params, TEXT("track_index"), 0));

	UAnimSequenceBase* AnimAsset = FindAnimAssetByNameOrPath<UAnimSequenceBase>(AssetName);
	if (!AnimAsset)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Animation asset '%s' not found"), *AssetName),
			TEXT("asset_not_found"));
	}

	// Validate time is within asset duration
	float PlayLength = AnimAsset->GetPlayLength();
	if (Time < 0.0 || Time > PlayLength)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Time %.3f is out of range [0, %.3f] for asset '%s'"),
				Time, PlayLength, *AssetName),
			TEXT("invalid_parameter"));
	}

	// Try to find the notify class
	FString NotifyClassName = GetOptionalString(Params, TEXT("notify_class"));
	UClass* NotifyClass = nullptr;

	if (!NotifyClassName.IsEmpty())
	{
		// Try to resolve the class
		NotifyClass = FindFirstObject<UClass>(*NotifyClassName, EFindFirstObjectOptions::NativeFirst);
		if (!NotifyClass)
		{
			// Try with AnimNotify_ prefix
			NotifyClass = FindFirstObject<UClass>(
				*FString::Printf(TEXT("AnimNotify_%s"), *NotifyClassName),
				EFindFirstObjectOptions::NativeFirst);
		}
	}

	// Create the notify event
	FAnimNotifyEvent NewNotifyEvent;
	NewNotifyEvent.NotifyName = FName(*NotifyName);

	// Set trigger time using the frame-based API
	FFrameRate FrameRate = AnimAsset->GetSamplingFrameRate();
	FFrameNumber Frame = FrameRate.AsFrameNumber(Time);
	NewNotifyEvent.Link.SetTime(Frame, FrameRate);

	NewNotifyEvent.TrackIndex = TrackIndex;

	// Create the notify instance if a class was specified
	if (NotifyClass && NotifyClass->IsChildOf(UAnimNotify::StaticClass()))
	{
		UAnimNotify* NewNotify = NewObject<UAnimNotify>(AnimAsset, NotifyClass, NAME_None, RF_Transactional);
		NewNotifyEvent.Notify = NewNotify;
	}

	AnimAsset->Notifies.Add(NewNotifyEvent);
	AnimAsset->MarkPackageDirty();
	Context.MarkPackageDirty(AnimAsset->GetOutermost());

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_name"), AnimAsset->GetName());
	Result->SetStringField(TEXT("notify_name"), NotifyName);
	Result->SetNumberField(TEXT("time"), Time);
	Result->SetNumberField(TEXT("track_index"), TrackIndex);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// FAnimRemoveNotifyAction
// Requirements: 11.3
// ============================================================================

bool FAnimRemoveNotifyAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString AssetName;
	if (!GetRequiredString(Params, TEXT("asset_name"), AssetName, OutError))
	{
		return false;
	}

	FString NotifyName;
	if (!GetRequiredString(Params, TEXT("notify_name"), NotifyName, OutError))
	{
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FAnimRemoveNotifyAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString AssetName, NotifyName, Error;
	GetRequiredString(Params, TEXT("asset_name"), AssetName, Error);
	GetRequiredString(Params, TEXT("notify_name"), NotifyName, Error);

	UAnimSequenceBase* AnimAsset = FindAnimAssetByNameOrPath<UAnimSequenceBase>(AssetName);
	if (!AnimAsset)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Animation asset '%s' not found"), *AssetName),
			TEXT("asset_not_found"));
	}

	// Find the notify by name (optionally filter by time if provided)
	double FilterTime = GetOptionalNumber(Params, TEXT("time"), -1.0);
	int32 RemoveIndex = -1;

	for (int32 i = 0; i < AnimAsset->Notifies.Num(); i++)
	{
		const FAnimNotifyEvent& NotifyEvent = AnimAsset->Notifies[i];
		if (NotifyEvent.NotifyName.ToString() == NotifyName)
		{
			// If time filter is specified, match on time too
			if (FilterTime >= 0.0)
			{
				if (FMath::IsNearlyEqual(NotifyEvent.GetTriggerTime(), static_cast<float>(FilterTime), 0.01f))
				{
					RemoveIndex = i;
					break;
				}
			}
			else
			{
				RemoveIndex = i;
				break;
			}
		}
	}

	if (RemoveIndex < 0)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("AnimNotify '%s' not found in asset '%s'"), *NotifyName, *AssetName),
			TEXT("not_found"));
	}

	float RemovedTime = AnimAsset->Notifies[RemoveIndex].GetTriggerTime();
	AnimAsset->Notifies.RemoveAt(RemoveIndex);
	AnimAsset->MarkPackageDirty();
	Context.MarkPackageDirty(AnimAsset->GetOutermost());

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_name"), AnimAsset->GetName());
	Result->SetStringField(TEXT("notify_name"), NotifyName);
	Result->SetNumberField(TEXT("removed_at_time"), RemovedTime);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// FAnimGetSkeletonHierarchyAction
// Requirements: 12.1, 12.2
// ============================================================================

bool FAnimGetSkeletonHierarchyAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString SkeletonName;
	if (!GetRequiredString(Params, TEXT("skeleton_name"), SkeletonName, OutError))
	{
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAnimGetSkeletonHierarchyAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SkeletonName;
	FString Error;
	GetRequiredString(Params, TEXT("skeleton_name"), SkeletonName, Error);
	bool bCompact = GetOptionalBool(Params, TEXT("compact"), false);

	USkeleton* Skeleton = FindAnimAssetByNameOrPath<USkeleton>(SkeletonName);
	if (!Skeleton)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Skeleton '%s' not found"), *SkeletonName),
			TEXT("asset_not_found"));
	}

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	int32 BoneCount = RefSkeleton.GetNum();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("skeleton_name"), Skeleton->GetName());
	Result->SetNumberField(TEXT("bone_count"), BoneCount);
	Result->SetBoolField(TEXT("compact"), bCompact);

	TArray<TSharedPtr<FJsonValue>> BonesArr;

	for (int32 i = 0; i < BoneCount; i++)
	{
		TSharedPtr<FJsonObject> BoneObj = MakeShared<FJsonObject>();
		BoneObj->SetStringField(TEXT("name"), RefSkeleton.GetBoneName(i).ToString());
		BoneObj->SetNumberField(TEXT("index"), i);
		BoneObj->SetNumberField(TEXT("parent_index"), RefSkeleton.GetParentIndex(i));

		if (!bCompact)
		{
			// Include transform data in non-compact mode
			const FTransform& BoneTransform = RefSkeleton.GetRefBonePose()[i];
			FVector Translation = BoneTransform.GetTranslation();
			FRotator Rotation = BoneTransform.GetRotation().Rotator();
			FVector Scale = BoneTransform.GetScale3D();

			TSharedPtr<FJsonObject> TransformObj = MakeShared<FJsonObject>();

			TArray<TSharedPtr<FJsonValue>> TransArr;
			TransArr.Add(MakeShared<FJsonValueNumber>(Translation.X));
			TransArr.Add(MakeShared<FJsonValueNumber>(Translation.Y));
			TransArr.Add(MakeShared<FJsonValueNumber>(Translation.Z));
			TransformObj->SetArrayField(TEXT("translation"), TransArr);

			TArray<TSharedPtr<FJsonValue>> RotArr;
			RotArr.Add(MakeShared<FJsonValueNumber>(Rotation.Pitch));
			RotArr.Add(MakeShared<FJsonValueNumber>(Rotation.Yaw));
			RotArr.Add(MakeShared<FJsonValueNumber>(Rotation.Roll));
			TransformObj->SetArrayField(TEXT("rotation"), RotArr);

			TArray<TSharedPtr<FJsonValue>> ScaleArr;
			ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.X));
			ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.Y));
			ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.Z));
			TransformObj->SetArrayField(TEXT("scale"), ScaleArr);

			BoneObj->SetObjectField(TEXT("transform"), TransformObj);
		}

		BonesArr.Add(MakeShared<FJsonValueObject>(BoneObj));
	}

	Result->SetArrayField(TEXT("bones"), BonesArr);

	return CreateSuccessResponse(Result);
}
