[CmdletBinding(SupportsShouldProcess)]
param([string] $ExecutablePath = (Join-Path $PSScriptRoot 'cupidgb.exe'))

$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'This script requires Windows.' }
Import-Module (Join-Path $PSScriptRoot 'CupidGB.FileAssociations.psm1') -Force
$result = Register-CupidGBFileAssociations -ExecutablePath $ExecutablePath -WhatIf:$WhatIfPreference
if ($result.Changed) { Send-CupidGBShellAssociationChange }
$result
