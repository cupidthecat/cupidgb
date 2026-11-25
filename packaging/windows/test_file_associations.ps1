# @file    test_file_associations.ps1
# @brief   Check cartridge file associations in an isolated Windows registry hive.
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
param([string] $HivePath)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'This check requires Windows.' }
$temporaryDirectory = $null
if (-not $HivePath) {
    $temporaryDirectory = Join-Path ([IO.Path]::GetTempPath()) ('cupidgb-associations-' + [guid]::NewGuid())
    New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null
    $HivePath = Join-Path $temporaryDirectory 'associations.hive'
}
$HivePath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($HivePath)
if (Test-Path -LiteralPath $HivePath) { throw 'Use a new private hive file path.' }
Import-Module (Join-Path $PSScriptRoot 'CupidGB.FileAssociations.psm1') -Force

if (-not ('CupidGB.PrivateHiveTest' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
namespace CupidGB {
    public static class PrivateHiveTest {
        [DllImport("advapi32.dll", CharSet=CharSet.Unicode, EntryPoint="RegLoadAppKeyW")]
        public static extern int Load(string file, out SafeRegistryHandle handle,
                                      uint access, uint options, uint reserved);
        [DllImport("shell32.dll", CharSet=CharSet.Unicode)]
        private static extern IntPtr CommandLineToArgvW(string command, out int count);
        [DllImport("kernel32.dll")]
        private static extern IntPtr LocalFree(IntPtr memory);
        public static string[] Arguments(string command) {
            int count;
            IntPtr memory=CommandLineToArgvW(command, out count);
            if(memory==IntPtr.Zero) throw new InvalidOperationException("Cannot parse command line");
            try {
                string[] result=new string[count];
                for(int i=0;i<count;i++)
                    result[i]=Marshal.PtrToStringUni(Marshal.ReadIntPtr(memory,i*IntPtr.Size));
                return result;
            } finally { LocalFree(memory); }
        }
    }
}
'@
}

function Assert-Check {
    param([bool] $Condition, [string] $Message)
    if (-not $Condition) { throw $Message }
}
function Get-Value {
    param([Microsoft.Win32.RegistryKey] $Root, [string] $Path, [string] $Name = '')
    $key = $Root.OpenSubKey($Path)
    try { if ($key) { return $key.GetValue($Name) } }
    finally { if ($key) { $key.Dispose() } }
    return $null
}
function Set-Value {
    param([Microsoft.Win32.RegistryKey] $Root, [string] $Path, [string] $Name, [string] $Value)
    $key = $Root.CreateSubKey($Path)
    try { $key.SetValue($Name, $Value) } finally { $key.Dispose() }
}
function Get-ActualDefaults {
    $root = [Microsoft.Win32.Registry]::CurrentUser
    foreach ($extension in @('.gb', '.gbc', '.gbs')) {
        $classes = 'Software\Classes\' + $extension
        $choice = 'Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\' + $extension + '\UserChoice'
        @($extension, (Get-Value $root $classes), (Get-Value $root $choice 'ProgId'),
            (Get-Value $root $choice 'Hash')) | ConvertTo-Json -Compress
    }
}

$actualBefore = @(Get-ActualDefaults)
$handle = $null
# REG_PROCESS_APPKEY keeps this file-backed hive outside HKCU/HKLM and closes
# it when the returned handles are disposed. No elevation or hive mount is used.
$status = [CupidGB.PrivateHiveTest]::Load($HivePath, [ref]$handle, 0x2001F, 1, 0)
if ($status -ne 0) { throw [ComponentModel.Win32Exception]::new($status) }
$root = [Microsoft.Win32.RegistryKey]::FromHandle($handle)
$directory = [IO.Path]::GetDirectoryName($HivePath)
$fixtureDirectory = Join-Path $directory ('cupidgb-association-fixtures-' + [guid]::NewGuid())
try {
    # A supplied hive may live beside existing installations. Write fixtures only
    # inside a newly created private directory, never fixed sibling paths.
    New-Item -ItemType Directory -Path $fixtureDirectory | Out-Null
    $folder = Join-Path $fixtureDirectory ('CupidGB test ' + [char]0x03A9 + " ' " + [char]0x732B)
    $moved = Join-Path $fixtureDirectory 'CupidGB moved'
    New-Item -ItemType Directory -Path $folder, $moved | Out-Null
    $exe = Join-Path $folder 'cupidgb.exe'
    $next = Join-Path $moved 'cupidgb.exe'
    [IO.File]::WriteAllBytes($exe, [byte[]]@(0x4D, 0x5A))
    [IO.File]::WriteAllBytes($next, [byte[]]@(0x4D, 0x5A))
    foreach ($extension in @('.gb', '.gbc', '.gbs')) {
        Set-Value $root ('Software\Classes\' + $extension) '' 'Other.Cartridge'
        Set-Value $root ('Software\Classes\' + $extension + '\OpenWithProgids') 'Other.Cartridge' ''
        $choice = 'Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\' + $extension + '\UserChoice'
        Set-Value $root $choice 'ProgId' 'Other.Cartridge'
        Set-Value $root $choice 'Hash' 'unchanged'
    }
    $preview = Register-CupidGBFileAssociations -ExecutablePath $exe -UserRoot $root -WhatIf
    Assert-Check (-not $preview.Changed) 'WhatIf reported changes.'
    Assert-Check ($null -eq (Get-Value $root 'Software\CupidGB\Capabilities' 'ApplicationName')) 'WhatIf wrote to the hive.'

    $first = Register-CupidGBFileAssociations -ExecutablePath $exe -UserRoot $root
    Assert-Check $first.Changed 'Registration made no changes.'
    Assert-Check ((Get-Value $root 'Software\Classes\CupidGB.GameBoy\shell\open\command') -ceq ('"{0}" "%1"' -f $exe)) 'Executable or cartridge quoting changed.'
    $cartridge = Join-Path $folder ('Game & 100% ' + [char]0x732B + '.gbc')
    $command = (Get-Value $root 'Software\Classes\CupidGB.GameBoyColor\shell\open\command').Replace('%1', $cartridge)
    $arguments = [CupidGB.PrivateHiveTest]::Arguments($command)
    Assert-Check ($arguments.Count -eq 2 -and $arguments[0] -ceq $exe -and
        $arguments[1] -ceq $cartridge) 'Windows argument parsing changed a cartridge path.'
    Assert-Check ((Get-Value $root 'Software\Classes\Applications\cupidgb.exe\SupportedTypes' '.gbc') -ceq '') 'SupportedTypes is absent.'
    Assert-Check ((Get-Value $root 'Software\Classes\Applications\cupidgb.exe\SupportedTypes' '.gbs') -ceq '') 'Music SupportedTypes is absent.'
    Assert-Check ((Get-Value $root 'Software\CupidGB\Capabilities\FileAssociations' '.gbs') -ceq 'CupidGB.Music') 'Music capabilities are absent.'
    $music = Join-Path $folder ('Track & 100% ' + [char]0x732B + '.gbs')
    $musicCommand = (Get-Value $root 'Software\Classes\CupidGB.Music\shell\open\command').Replace('%1', $music)
    $musicArguments = [CupidGB.PrivateHiveTest]::Arguments($musicCommand)
    Assert-Check ($musicArguments.Count -eq 2 -and $musicArguments[0] -ceq $exe -and
        $musicArguments[1] -ceq $music) 'Windows argument parsing changed a music path.'
    Assert-Check ((Get-Value $root 'Software\RegisteredApplications' 'CupidGB') -ceq 'Software\CupidGB\Capabilities') 'Capabilities are not advertised.'
    $again = Register-CupidGBFileAssociations -ExecutablePath $exe -UserRoot $root
    Assert-Check (-not $again.Changed) 'Repeated registration was not idempotent.'

    $replacement = Register-CupidGBFileAssociations -ExecutablePath $next -UserRoot $root
    Assert-Check $replacement.Changed 'Moving the application did not update its paths.'
    $stale = Unregister-CupidGBFileAssociations -ExecutablePath $exe -UserRoot $root
    Assert-Check (-not $stale.Changed) 'An old folder removed a newer registration.'
    Assert-Check ((Get-Value $root 'Software\Classes\CupidGB.GameBoy\shell\open\command') -ceq ('"{0}" "%1"' -f $next)) 'Stale removal changed the open command.'

    Set-Value $root 'Software\Classes\CupidGB.GameBoy' 'UnrelatedValue' 'preserve'
    [IO.File]::Delete($next)
    $removed = Unregister-CupidGBFileAssociations -ExecutablePath $next.ToUpperInvariant() -UserRoot $root
    Assert-Check $removed.Changed 'Unregistration made no changes.'
    Assert-Check ((Get-Value $root 'Software\Classes\CupidGB.GameBoy' 'UnrelatedValue') -ceq 'preserve') 'Unregistration removed an unrelated value.'
    Assert-Check ($null -eq (Get-Value $root 'Software\Classes\CupidGB.GameBoy\shell\open\command')) 'The owned open command remains.'
    Assert-Check ($null -eq (Get-Value $root 'Software\Classes\CupidGB.Music\shell\open\command')) 'The owned music command remains.'
    Assert-Check ($null -eq (Get-Value $root 'Software\RegisteredApplications' 'CupidGB')) 'The owned capability entry remains.'
    Assert-Check (-not (Unregister-CupidGBFileAssociations -ExecutablePath $next -UserRoot $root).Changed) 'Repeated removal was not idempotent.'
    foreach ($extension in @('.gb', '.gbc', '.gbs')) {
        Assert-Check ((Get-Value $root ('Software\Classes\' + $extension)) -ceq 'Other.Cartridge') 'An existing extension default changed.'
        Assert-Check ((Get-Value $root ('Software\Classes\' + $extension + '\OpenWithProgids') 'Other.Cartridge') -ceq '') 'Another Open With entry changed.'
        $choice = 'Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\' + $extension + '\UserChoice'
        Assert-Check ((Get-Value $root $choice 'Hash') -ceq 'unchanged') 'UserChoice changed.'
    }
    $collision = $false
    try { Register-CupidGBFileAssociations -ExecutablePath $exe -UserRoot $root | Out-Null }
    catch { $collision = $_.Exception.Message -like '*not owned*' }
    Assert-Check $collision 'An unowned ProgID was overwritten.'
    Assert-Check ($null -eq (Get-Value $root 'Software\CupidGB\Capabilities' 'ApplicationName')) 'A collision caused partial registration.'

    $root.Flush()
    $actualAfter = @(Get-ActualDefaults)
    Assert-Check (($actualBefore -join "`n") -ceq ($actualAfter -join "`n")) 'Actual user defaults changed.'
    [pscustomobject]@{ Status = 'pass'; PrivateHive = $HivePath; FixtureDirectory = $fixtureDirectory;
        ActualUserDefaultsUnchanged = $true }
} finally {
    $root.Dispose()
    $handle.Dispose()
    if ($temporaryDirectory) {
        $target = [IO.Path]::GetFullPath($temporaryDirectory)
        $parent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
        if (-not $target.StartsWith($parent, [StringComparison]::OrdinalIgnoreCase) -or
            [IO.Path]::GetFileName($target) -notlike 'cupidgb-associations-*') {
            throw 'Temporary hive cleanup target escaped the test directory.'
        }
        Remove-Item -LiteralPath $target -Recurse -Force
    }
}
