param(
	[string]$EngineRoot = $(if ($env:ACTIONS_UE_ROOT) { $env:ACTIONS_UE_ROOT } else { $env:UE_ROOT }),
	[string]$PluginPath = (Join-Path $PSScriptRoot '..\..\UECliTool.uplugin'),
	[string]$PackageDir = (Join-Path $PSScriptRoot '..\..\artifacts\UECliTool')
)

$ErrorActionPreference = 'Stop'

function Resolve-FullPath {
	param([Parameter(Mandatory = $true)][string]$Path)
	$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
	throw 'UE_ROOT is not set. Set it as a repository Actions variable or runner environment variable, for example D:\Epic\UE_5.4.'
}

$EngineRoot = Resolve-FullPath $EngineRoot
$PluginPath = Resolve-FullPath $PluginPath
$PackageDir = Resolve-FullPath $PackageDir
$RunUat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'

if (!(Test-Path $RunUat)) {
	throw "RunUAT.bat was not found at $RunUat"
}

if (!(Test-Path $PluginPath)) {
	throw "Plugin descriptor was not found at $PluginPath"
}

if (Test-Path $PackageDir) {
	Remove-Item -LiteralPath $PackageDir -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null

Write-Host "Using UE_ROOT: $EngineRoot"
Write-Host "Building plugin: $PluginPath"
Write-Host "Package output: $PackageDir"

& $RunUat BuildPlugin `
	-Plugin="$PluginPath" `
	-Package="$PackageDir" `
	-TargetPlatforms=Win64 `
	-Rocket

if ($LASTEXITCODE -ne 0) {
	throw "RunUAT BuildPlugin failed with exit code $LASTEXITCODE"
}

Write-Host 'Unreal plugin build completed.'
