param(
	[Parameter(ValueFromRemainingArguments = $true)]
	[string[]] $Arguments
)

$ErrorActionPreference = "Stop"

$PluginRoot = $PSScriptRoot
$VenvPython = Join-Path $PluginRoot "Python\.venv\Scripts\python.exe"
$UeEntry = Join-Path $PluginRoot "Python\ue.py"

if (Test-Path $VenvPython) {
	$Python = $VenvPython
} else {
	$Python = "python"
}

& $Python $UeEntry @Arguments
exit $LASTEXITCODE
