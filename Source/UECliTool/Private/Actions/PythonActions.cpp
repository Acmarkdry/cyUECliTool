// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/PythonActions.h"
#include "IPythonScriptPlugin.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// =========================================================================
// FExecPythonAction
// =========================================================================

bool FExecPythonAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Code;
	if (!GetRequiredString(Params, TEXT("code"), Code, OutError))
	{
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FExecPythonAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	// Check PythonScriptPlugin availability
	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin || !PythonPlugin->IsPythonAvailable())
	{
		return CreateErrorResponse(
			TEXT("PythonScriptPlugin is not available. Enable it in .uproject."),
			TEXT("python_unavailable")
		);
	}

	FString Code = Params->GetStringField(TEXT("code"));

	FString ResultFilePath = FPaths::Combine(
		FPlatformProcess::UserTempDir(),
		FString::Printf(TEXT("__mcp_python_result_%s.json"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))
	);
	IFileManager::Get().Delete(*ResultFilePath);

	const FString CodeBase64 = EncodeBase64Utf8(Code);
	const FString ResultPathBase64 = EncodeBase64Utf8(ResultFilePath);

	// The wrapper writes the result file in the same ExecPythonCommand call.
	// This avoids relying on globals surviving across separate plugin calls.
	FString WrapperCode = FString::Printf(TEXT(
		"import base64 as __mcp_base64\n"
		"import sys as __mcp_sys\n"
		"import io as __mcp_io\n"
		"import json as __mcp_json\n"
		"import traceback as __mcp_traceback\n"
		"\n"
		"__mcp_code = __mcp_base64.b64decode('%s').decode('utf-8')\n"
		"__mcp_result_path = __mcp_base64.b64decode('%s').decode('utf-8')\n"
		"__mcp_stdout_capture = __mcp_io.StringIO()\n"
		"__mcp_stderr_capture = __mcp_io.StringIO()\n"
		"__mcp_old_stdout = __mcp_sys.stdout\n"
		"__mcp_old_stderr = __mcp_sys.stderr\n"
		"__mcp_sys.stdout = __mcp_stdout_capture\n"
		"__mcp_sys.stderr = __mcp_stderr_capture\n"
		"__mcp_success = True\n"
		"__mcp_error = ''\n"
		"__mcp_return_value = None\n"
		"__mcp_return_value_json = 'null'\n"
		"\n"
		"try:\n"
		"    __mcp_user_ns = globals()\n"
		"    if '_result' in __mcp_user_ns:\n"
		"        del __mcp_user_ns['_result']\n"
		"    exec(__mcp_code, __mcp_user_ns, __mcp_user_ns)\n"
		"    if '_result' in __mcp_user_ns:\n"
		"        __mcp_return_value = __mcp_user_ns['_result']\n"
		"        try:\n"
		"            __mcp_return_value_json = __mcp_json.dumps(__mcp_return_value, default=str, ensure_ascii=False)\n"
		"        except Exception as __mcp_e:\n"
		"            __mcp_return_value_json = __mcp_json.dumps(str(__mcp_return_value), ensure_ascii=False)\n"
		"except Exception as __mcp_e:\n"
		"    __mcp_success = False\n"
		"    __mcp_error = str(__mcp_e)\n"
		"    __mcp_stderr_capture.write(__mcp_traceback.format_exc())\n"
		"finally:\n"
		"    __mcp_sys.stdout = __mcp_old_stdout\n"
		"    __mcp_sys.stderr = __mcp_old_stderr\n"
		"    __mcp_stdout_str = __mcp_stdout_capture.getvalue()\n"
		"    __mcp_stderr_str = __mcp_stderr_capture.getvalue()\n"
		"    __mcp_envelope = __mcp_json.dumps({\n"
		"        'success': __mcp_success,\n"
		"        'error': __mcp_error,\n"
		"        'stdout': __mcp_stdout_str,\n"
		"        'stderr': __mcp_stderr_str,\n"
		"        'return_value_json': __mcp_return_value_json\n"
		"    }, ensure_ascii=False)\n"
		"    with open(__mcp_result_path, 'w', encoding='utf-8') as __mcp_f:\n"
		"        __mcp_f.write(__mcp_envelope)\n"
	), *CodeBase64, *ResultPathBase64);

	// Execute the wrapper
	bool bExecSuccess = PythonPlugin->ExecPythonCommand(*WrapperCode);

	FString StdoutStr, StderrStr, ReturnValueJson, ErrorStr;
	FString ResultJson;

	if (!FFileHelper::LoadFileToString(ResultJson, *ResultFilePath))
	{
		const FString Message = bExecSuccess
			? TEXT("Failed to read Python execution result. The code may have crashed the Python interpreter.")
			: TEXT("Python wrapper execution failed before producing a result.");
		return CreateErrorResponse(
			Message,
			TEXT("python_result_read_error")
		);
	}

	// Clean up temp file
	IFileManager::Get().Delete(*ResultFilePath);

	// Parse the envelope JSON
	TSharedPtr<FJsonObject> Envelope;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResultJson);
	if (!FJsonSerializer::Deserialize(Reader, Envelope) || !Envelope.IsValid())
	{
		return CreateErrorResponse(
			TEXT("Failed to parse Python execution result JSON."),
			TEXT("python_result_parse_error")
		);
	}

	bool bSuccess = Envelope->GetBoolField(TEXT("success"));
	StdoutStr = Envelope->GetStringField(TEXT("stdout"));
	StderrStr = Envelope->GetStringField(TEXT("stderr"));
	ErrorStr = Envelope->GetStringField(TEXT("error"));
	ReturnValueJson = Envelope->GetStringField(TEXT("return_value_json"));

	if (!bSuccess)
	{
		TSharedPtr<FJsonObject> ErrorResult = MakeShared<FJsonObject>();
		ErrorResult->SetStringField(TEXT("stderr"), StderrStr);
		ErrorResult->SetStringField(TEXT("stdout"), StdoutStr);

		TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetStringField(TEXT("status"), TEXT("error"));
		Response->SetBoolField(TEXT("success"), false);
		Response->SetStringField(TEXT("error"), ErrorStr);
		Response->SetStringField(TEXT("error_type"), TEXT("python_exception"));
		Response->SetStringField(TEXT("stderr"), StderrStr);
		return Response;
	}

	// Parse return_value from its JSON string
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("stdout"), StdoutStr);
	ResultData->SetStringField(TEXT("stderr"), StderrStr);

	// Parse return_value_json into a proper JSON value. Wrap the value so UE's
	// JSON parser also handles top-level strings, booleans, numbers, and null.
	TSharedPtr<FJsonObject> WrappedReturnObject;
	const FString WrappedReturnJson = FString::Printf(TEXT("{\"value\":%s}"), *ReturnValueJson);
	TSharedRef<TJsonReader<>> ReturnReader = TJsonReaderFactory<>::Create(WrappedReturnJson);
	if (
		FJsonSerializer::Deserialize(ReturnReader, WrappedReturnObject)
		&& WrappedReturnObject.IsValid()
		&& WrappedReturnObject->TryGetField(TEXT("value")).IsValid()
	)
	{
		ResultData->SetField(TEXT("return_value"), WrappedReturnObject->TryGetField(TEXT("value")));
	}
	else
	{
		ResultData->SetField(TEXT("return_value"), MakeShared<FJsonValueNull>());
	}

	return CreateSuccessResponse(ResultData);
}

FString FExecPythonAction::EncodeBase64Utf8(const FString& Input)
{
	FTCHARToUTF8 Utf8(*Input);
	return FBase64::Encode(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
}
