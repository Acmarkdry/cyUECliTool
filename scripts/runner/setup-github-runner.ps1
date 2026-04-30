param(
	[Parameter(Mandatory = $true)]
	[string]$Token,

	[string]$RepoUrl = 'https://github.com/Acmarkdry/cyUECliTool',
	[string]$RunnerRoot = 'D:\actions-runner-cyUECliTool',
	[string]$RunnerName = "$env:COMPUTERNAME-cyUECliTool",
	[string]$Labels = 'ue,cyUECliTool',
	[string]$WorkDir = '_work',
	[switch]$InstallService,
	[string]$ServiceUser,
	[string]$ServicePassword
)

$ErrorActionPreference = 'Stop'

function Resolve-FullPath {
	param([Parameter(Mandatory = $true)][string]$Path)
	$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

$RunnerRoot = Resolve-FullPath $RunnerRoot
$Zip = Get-ChildItem -LiteralPath $RunnerRoot -Filter 'actions-runner-win-x64-*.zip' |
	Sort-Object LastWriteTime -Descending |
	Select-Object -First 1

if ($null -eq $Zip) {
	throw "No GitHub Actions runner zip was found in $RunnerRoot. Download actions-runner-win-x64-*.zip there first."
}

if (!(Test-Path (Join-Path $RunnerRoot 'config.cmd'))) {
	Write-Host "Extracting $($Zip.FullName) ..."
	Expand-Archive -LiteralPath $Zip.FullName -DestinationPath $RunnerRoot -Force
}

Push-Location $RunnerRoot
try {
	if (!(Test-Path '.\.runner')) {
		.\config.cmd --unattended `
			--url $RepoUrl `
			--token $Token `
			--name $RunnerName `
			--labels $Labels `
			--work $WorkDir `
			--replace
	} else {
		Write-Host 'Runner is already configured. Skipping config.cmd.'
	}

	if ($InstallService) {
		if ($ServiceUser -and $ServicePassword) {
			.\svc.cmd install $ServiceUser $ServicePassword
		} else {
			.\svc.cmd install
		}
		.\svc.cmd start
		Write-Host 'GitHub Actions runner service started.'
	} else {
		Write-Host "Runner configured. Start it interactively with: $RunnerRoot\run.cmd"
	}
} finally {
	Pop-Location
}
