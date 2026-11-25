# @file    test_file_association_preservation.ps1
# @brief   Verify association operations preserve unrelated registry and workspace data.
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

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'This check requires Windows.' }
$temporaryDirectory = Join-Path ([IO.Path]::GetTempPath()) ('cupidgb-association-preservation-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null

function Get-WorkspaceSnapshot {
    param([string] $Directory)

    foreach ($entry in (Get-ChildItem -LiteralPath $Directory -Recurse -Force | Sort-Object FullName)) {
        if ($entry.PSIsContainer) {
            'Directory: ' + $entry.FullName
        } else {
            'File: ' + $entry.FullName + ': ' + (Get-FileHash -LiteralPath $entry.FullName -Algorithm SHA256).Hash
        }
    }
}

try {
    $associationTest = Join-Path $PSScriptRoot 'test_file_associations.ps1'
    $original = [Text.Encoding]::UTF8.GetBytes('Existing executable fixture must remain unchanged.')
    $fixedFolders = @(
        (Join-Path $temporaryDirectory ('CupidGB test ' + [char]0x03A9 + " ' " + [char]0x732B)),
        (Join-Path $temporaryDirectory 'CupidGB moved')
    )
    foreach ($folder in $fixedFolders) {
        New-Item -ItemType Directory -Path $folder | Out-Null
        [IO.File]::WriteAllBytes((Join-Path $folder 'cupidgb.exe'), $original)
    }

    $fixtureDirectories = @()
    foreach ($name in @('first.hive', 'second.hive')) {
        $hive = Join-Path $temporaryDirectory $name
        $result = & $associationTest -HivePath $hive
        if ($result.Status -cne 'pass' -or -not $result.ActualUserDefaultsUnchanged) {
            throw 'The custom-hive association check failed.'
        }
        foreach ($folder in $fixedFolders) {
            $bytes = [IO.File]::ReadAllBytes((Join-Path $folder 'cupidgb.exe'))
            if ([Convert]::ToBase64String($bytes) -cne [Convert]::ToBase64String($original)) {
                throw 'A custom-hive check changed an existing sibling executable.'
            }
        }
        if (-not [IO.Directory]::Exists($result.FixtureDirectory) -or
            $fixtureDirectories -contains $result.FixtureDirectory) {
            throw 'Separate custom hives reused a fixture directory.'
        }
        $fixtureDirectories += $result.FixtureDirectory
    }

    $before = @(Get-WorkspaceSnapshot $temporaryDirectory)
    $rejected = $false
    try {
        & $associationTest -HivePath (Join-Path $temporaryDirectory 'first.hive') | Out-Null
    } catch {
        $rejected = $_.Exception.Message -ceq 'Use a new private hive file path.'
    }
    if (-not $rejected) { throw 'An existing custom hive was not rejected.' }
    $after = @(Get-WorkspaceSnapshot $temporaryDirectory)
    if (($before -join "`n") -cne ($after -join "`n")) {
        throw 'Rejecting an existing custom hive changed files or directories.'
    }
    [pscustomobject]@{ Status = 'pass'; ExistingExecutablesUnchanged = $true;
        ExistingHiveRejectionUnchanged = $true; SeparateFixtureDirectories = $true }
} finally {
    $target = [IO.Path]::GetFullPath($temporaryDirectory)
    $parent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $target.StartsWith($parent, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($target) -notlike 'cupidgb-association-preservation-*') {
        throw 'Preservation-test cleanup target escaped its private workspace.'
    }
    Remove-Item -LiteralPath $target -Recurse -Force
}
