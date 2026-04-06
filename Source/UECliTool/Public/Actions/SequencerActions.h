// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actions/EditorAction.h"

class ULevelSequence;
class UMovieScene;

// ============================================================================
// P8: Sequencer Actions
// ============================================================================

class UECLITOOL_API FSequencerAction : public FEditorAction
{
protected:
	ULevelSequence* FindLevelSequence(const FString& Name, FString& OutError) const;
};

/** Create a new LevelSequence asset. */
class UECLITOOL_API FCreateLevelSequenceAction : public FSequencerAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("create_level_sequence"); }
};

/** Describe a LevelSequence structure (bindings, tracks, sections). */
class UECLITOOL_API FDescribeLevelSequenceAction : public FSequencerAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("describe_level_sequence"); }
	virtual bool RequiresSave() const override { return false; }
};

/** Add a possessable binding (bind a level actor to the sequence). */
class UECLITOOL_API FAddSequencerPossessableAction : public FSequencerAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("add_sequencer_possessable"); }
};

/** Add a track to a binding. */
class UECLITOOL_API FAddSequencerTrackAction : public FSequencerAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("add_sequencer_track"); }
};

/** Set the playback range of a sequence. */
class UECLITOOL_API FSetSequencerRangeAction : public FSequencerAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("set_sequencer_range"); }
};
