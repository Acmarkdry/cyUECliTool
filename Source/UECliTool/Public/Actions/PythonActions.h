// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorAction.h"

/**
 * FExecPythonAction
 *
 * Executes Python code in Unreal's embedded Python environment via
 * IPythonScriptPlugin::ExecPythonCommand(). Captures stdout, stderr,
 * and an optional _result variable.
 *
 * Parameters:
 *   - code (required): Python code to execute
 *
 * Returns:
 *   - return_value: JSON-serialized value of _result variable (null if not set)
 *   - stdout: Captured standard output
 *   - stderr: Captured standard error
 */
class UECLITOOL_API FExecPythonAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("exec_python"); }
	virtual bool RequiresSave() const override { return false; }

private:
	/** Encode text as UTF-8 base64 for safe embedding in the Python wrapper. */
	static FString EncodeBase64Utf8(const FString& Input);
};
