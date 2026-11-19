# @file    Unregister-CupidGB.ps1
# @brief   Remove file associations owned by the selected CupidGB installation.
# @author  cupidthecat
# @date    2026-11-16
#
# Copyright (C) 2026 cupidthecat
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://gnu.org>.

[CmdletBinding(SupportsShouldProcess)]
param([string] $ExecutablePath = (Join-Path $PSScriptRoot 'cupidgb.exe'))

$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'This script requires Windows.' }
Import-Module (Join-Path $PSScriptRoot 'CupidGB.FileAssociations.psm1') -Force
$result = Unregister-CupidGBFileAssociations -ExecutablePath $ExecutablePath -WhatIf:$WhatIfPreference
if ($result.Changed) { Send-CupidGBShellAssociationChange }
$result
