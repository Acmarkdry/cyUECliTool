param(
	[string] $CodexHome = (Join-Path $env:USERPROFILE ".codex"),
	[switch] $Copy
)

$ErrorActionPreference = "Stop"

$PluginRoot = Split-Path -Parent $PSScriptRoot
$SourceSkill = Join-Path $PluginRoot "skills\unreal-ue-cli"
$SkillsRoot = Join-Path $CodexHome "skills"
$TargetSkill = Join-Path $SkillsRoot "unreal-ue-cli"

$SourceSkill = (Resolve-Path $SourceSkill).Path
New-Item -ItemType Directory -Path $SkillsRoot -Force | Out-Null
$SkillsRoot = (Resolve-Path $SkillsRoot).Path

function Test-UnderDirectory([string] $Child, [string] $Parent) {
	$childFull = [System.IO.Path]::GetFullPath($Child)
	$parentFull = [System.IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
	return $childFull.StartsWith($parentFull, [System.StringComparison]::OrdinalIgnoreCase)
}

if (-not (Test-UnderDirectory $TargetSkill $SkillsRoot)) {
	throw "Refusing to modify target outside Codex skills directory: $TargetSkill"
}

if (Test-Path $TargetSkill) {
	$item = Get-Item $TargetSkill -Force
	if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
		Remove-Item -LiteralPath $TargetSkill -Force
	} else {
		$stamp = Get-Date -Format "yyyyMMddHHmmss"
		$backup = "$TargetSkill.backup-$stamp"
		Move-Item -LiteralPath $TargetSkill -Destination $backup
		Write-Host "Backed up existing skill to $backup"
	}
}

if ($Copy) {
	Copy-Item -Path $SourceSkill -Destination $TargetSkill -Recurse
	Write-Host "Copied Codex skill from $SourceSkill to $TargetSkill"
} else {
	New-Item -ItemType Junction -Path $TargetSkill -Target $SourceSkill | Out-Null
	Write-Host "Linked Codex skill $TargetSkill -> $SourceSkill"
}
