# GitHub Actions self-hosted UE runner

This repository has two CI layers:

- `lint` and `test` run on GitHub-hosted Ubuntu runners.
- `ue-build` runs on a Windows self-hosted runner with Unreal Engine installed.

## Runner labels

Register the Windows runner with these labels:

```text
ue,cyUECliTool
```

GitHub automatically adds `self-hosted`, `Windows`, and `X64`. The workflow targets:

```yaml
runs-on: [self-hosted, Windows, X64, ue]
```

## Engine path

The workflow is configured for the local source-built engine at:

```text
D:\Unreal\UnrealEngine
```

You can override it by setting `ACTIONS_UE_ROOT` in the workflow or `UE_ROOT` on the runner.

The path must contain:

```text
Engine\Build\BatchFiles\RunUAT.bat
```

## Register this machine

The runner archive should be extracted under:

```powershell
D:\actions-runner-cyUECliTool
```

Create a registration token in GitHub:

```text
Repository -> Settings -> Actions -> Runners -> New self-hosted runner -> Windows
```

Then run from a normal PowerShell session:

```powershell
.\scripts\runner\setup-github-runner.ps1 -Token "<registration-token>"
```

To install it as a Windows service:

```powershell
.\scripts\runner\setup-github-runner.ps1 -Token "<registration-token>" -InstallService
```

To run interactively instead of as a service:

```powershell
D:\actions-runner-cyUECliTool\run.cmd
```

## UE build command

The workflow calls:

```powershell
.\scripts\ci\build-ue-plugin.ps1
```

That script runs:

```text
RunUAT.bat BuildPlugin -Plugin=<repo>\UECliTool.uplugin -Package=<repo>\artifacts\UECliTool -TargetPlatforms=Win64 -Rocket
```

The packaged plugin is uploaded as the `UECliTool-Win64` workflow artifact.
