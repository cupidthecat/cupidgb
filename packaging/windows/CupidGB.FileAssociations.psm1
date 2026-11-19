# @file    CupidGB.FileAssociations.psm1
# @brief   Manage per-user cartridge and music associations without changing existing defaults.
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

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-CupidGBExecutable {
    param([Parameter(Mandatory)][string] $ExecutablePath)

    $path = (Resolve-Path -LiteralPath $ExecutablePath -ErrorAction Stop).ProviderPath
    if (-not [IO.File]::Exists($path) -or [IO.Path]::GetFileName($path) -ine 'cupidgb.exe') {
        throw 'ExecutablePath must name an existing cupidgb.exe file.'
    }
    return [IO.Path]::GetFullPath($path)
}

function Get-CupidGBRegistrationPlan {
    param([Parameter(Mandatory)][string] $ExecutablePath)

    $command = '"{0}" "%1"' -f $ExecutablePath
    $icon = '"{0}",0' -f $ExecutablePath
    $capabilities = 'Software\CupidGB\Capabilities'
    $entries = [Collections.Generic.List[object]]::new()
    foreach ($type in @(
        @{ Extension = '.gb'; ProgId = 'CupidGB.GameBoy'; Description = 'CupidGB Game Boy cartridge' },
        @{ Extension = '.gbc'; ProgId = 'CupidGB.GameBoyColor'; Description = 'CupidGB Game Boy Color cartridge' },
        @{ Extension = '.gbs'; ProgId = 'CupidGB.Music'; Description = 'CupidGB Game Boy music' }
    )) {
        $key = 'Software\Classes\' + $type.ProgId
        $entries.Add(@($key, '', $type.Description, 'String'))
        $entries.Add(@($key, 'CupidGBRegistrationVersion', 1, 'DWord'))
        $entries.Add(@($key, 'CupidGBExecutable', $ExecutablePath, 'String'))
        $entries.Add(@(($key + '\DefaultIcon'), '', $icon, 'String'))
        $entries.Add(@(($key + '\shell\open\command'), '', $command, 'String'))
        $entries.Add(@(('Software\Classes\' + $type.Extension + '\OpenWithProgids'),
                $type.ProgId, [byte[]]@(), 'None'))
        $entries.Add(@(($capabilities + '\FileAssociations'), $type.Extension, $type.ProgId, 'String'))
    }
    $application = 'Software\Classes\Applications\cupidgb.exe'
    $entries.Add(@($application, 'FriendlyAppName', 'CupidGB', 'String'))
    $entries.Add(@($application, 'CupidGBRegistrationVersion', 1, 'DWord'))
    $entries.Add(@($application, 'CupidGBExecutable', $ExecutablePath, 'String'))
    $entries.Add(@(($application + '\DefaultIcon'), '', $icon, 'String'))
    $entries.Add(@(($application + '\shell\open\command'), '', $command, 'String'))
    $entries.Add(@(($application + '\SupportedTypes'), '.gb', '', 'String'))
    $entries.Add(@(($application + '\SupportedTypes'), '.gbc', '', 'String'))
    $entries.Add(@(($application + '\SupportedTypes'), '.gbs', '', 'String'))
    $entries.Add(@($capabilities, 'ApplicationName', 'CupidGB', 'String'))
    $entries.Add(@($capabilities, 'ApplicationDescription', 'Game Boy and Game Boy Color emulator', 'String'))
    $entries.Add(@($capabilities, 'ApplicationIcon', $icon, 'String'))
    $entries.Add(@($capabilities, 'CupidGBRegistrationVersion', 1, 'DWord'))
    $entries.Add(@($capabilities, 'CupidGBExecutable', $ExecutablePath, 'String'))
    $entries.Add(@('Software\RegisteredApplications', 'CupidGB', $capabilities, 'String'))
    return ,$entries
}

function Test-CupidGBRegistryValue {
    param([Microsoft.Win32.RegistryKey] $Key, [object[]] $Entry)

    if (-not $Key -or $Key.GetValueNames() -notcontains $Entry[1]) {
        return $false
    }
    if ($Key.GetValueKind($Entry[1]).ToString() -ne $Entry[3]) {
        return $false
    }
    $value = $Key.GetValue($Entry[1], $null,
        [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    if ($Entry[3] -eq 'None') {
        return $value -is [byte[]] -and $value.Length -eq 0
    }
    return $value -ceq $Entry[2]
}

function Assert-CupidGBOwnedRegistration {
    param([Microsoft.Win32.RegistryKey] $UserRoot)

    foreach ($path in @('Software\Classes\CupidGB.GameBoy', 'Software\Classes\CupidGB.GameBoyColor',
            'Software\Classes\CupidGB.Music',
            'Software\Classes\Applications\cupidgb.exe', 'Software\CupidGB\Capabilities')) {
        $key = $UserRoot.OpenSubKey($path)
        try {
            if ($key -and (-not (Test-CupidGBRegistryValue $key @($path,
                            'CupidGBRegistrationVersion', 1, 'DWord')) -or
                    -not ($key.GetValue('CupidGBExecutable', '') -is [string]) -or
                    -not $key.GetValue('CupidGBExecutable', '') -or
                    $key.GetValueKind('CupidGBExecutable') -ne [Microsoft.Win32.RegistryValueKind]::String)) {
                throw "Existing registration is not owned by these scripts: $path"
            }
        } finally {
            if ($key) { $key.Dispose() }
        }
    }
    $registered = $UserRoot.OpenSubKey('Software\RegisteredApplications')
    try {
        if ($registered -and $registered.GetValueNames() -contains 'CupidGB' -and
            $registered.GetValue('CupidGB', '') -cne 'Software\CupidGB\Capabilities') {
            throw 'Another registration already uses the CupidGB application name.'
        }
    } finally {
        if ($registered) { $registered.Dispose() }
    }
}

function Register-CupidGBFileAssociations {
    [CmdletBinding(SupportsShouldProcess)]
    param(
        [Parameter(Mandatory)][string] $ExecutablePath,
        [Microsoft.Win32.RegistryKey] $UserRoot = [Microsoft.Win32.Registry]::CurrentUser
    )

    $path = Get-CupidGBExecutable $ExecutablePath
    Assert-CupidGBOwnedRegistration $UserRoot
    if (-not $PSCmdlet.ShouldProcess($path, 'Add CupidGB to per-user Open With choices')) {
        return [pscustomobject]@{ Changed = $false; ExecutablePath = $path }
    }
    $changed = $false
    foreach ($entry in (Get-CupidGBRegistrationPlan $path)) {
        $key = $UserRoot.CreateSubKey($entry[0])
        try {
            if (-not (Test-CupidGBRegistryValue $key $entry)) {
                $key.SetValue($entry[1], $entry[2], [Microsoft.Win32.RegistryValueKind]$entry[3])
                $changed = $true
            }
        } finally { $key.Dispose() }
    }
    return [pscustomobject]@{ Changed = $changed; ExecutablePath = $path }
}

function Unregister-CupidGBFileAssociations {
    [CmdletBinding(SupportsShouldProcess)]
    param(
        [Parameter(Mandatory)][string] $ExecutablePath,
        [Microsoft.Win32.RegistryKey] $UserRoot = [Microsoft.Win32.Registry]::CurrentUser
    )

    # Unregistration also works after the executable has been removed.
    $path = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ExecutablePath)
    $owner = $UserRoot.OpenSubKey('Software\CupidGB\Capabilities')
    try {
        $registeredPath = if ($owner) { [string]$owner.GetValue('CupidGBExecutable', '') } else { '' }
        $matches = $owner -and $owner.GetValue('CupidGBRegistrationVersion', 0) -eq 1 -and
            [string]::Equals($registeredPath, $path,
                [StringComparison]::OrdinalIgnoreCase)
    } finally { if ($owner) { $owner.Dispose() } }
    if (-not $matches) {
        return [pscustomobject]@{ Changed = $false; ExecutablePath = $path }
    }
    $path = $registeredPath
    Assert-CupidGBOwnedRegistration $UserRoot
    if (-not $PSCmdlet.ShouldProcess($path, 'Remove this folder from per-user Open With choices')) {
        return [pscustomobject]@{ Changed = $false; ExecutablePath = $path }
    }

    $changed = $false
    $plan = Get-CupidGBRegistrationPlan $path
    foreach ($entry in $plan) {
        $key = $UserRoot.OpenSubKey($entry[0], $true)
        try {
            # Leave later edits and entries from other applications intact.
            if (Test-CupidGBRegistryValue $key $entry) {
                $key.DeleteValue($entry[1], $false)
                $changed = $true
            }
        } finally { if ($key) { $key.Dispose() } }
    }
    $paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $plan) {
        if ($entry[0] -eq 'Software\RegisteredApplications') { continue }
        $current = [string]$entry[0]
        while ($current -notin @('Software\Classes', 'Software\Classes\Applications',
                'Software\CupidGB', 'Software')) {
            [void]$paths.Add($current)
            if ($current -in @('Software\Classes\.gb\OpenWithProgids',
                    'Software\Classes\.gbc\OpenWithProgids',
                    'Software\Classes\.gbs\OpenWithProgids')) { break }
            $current = $current.Substring(0, $current.LastIndexOf('\'))
        }
    }
    foreach ($current in ($paths | Sort-Object Length -Descending)) {
        $key = $UserRoot.OpenSubKey($current)
        try { $empty = $key -and $key.SubKeyCount -eq 0 -and $key.ValueCount -eq 0 }
        finally { if ($key) { $key.Dispose() } }
        if ($empty) { $UserRoot.DeleteSubKey($current, $false) }
    }
    return [pscustomobject]@{ Changed = $changed; ExecutablePath = $path }
}

function Send-CupidGBShellAssociationChange {
    if (-not ('CupidGB.ShellNotifications' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace CupidGB {
    public static class ShellNotifications {
        [DllImport("shell32.dll")]
        public static extern void SHChangeNotify(uint change, uint flags, IntPtr first, IntPtr second);
    }
}
'@
    }
    [CupidGB.ShellNotifications]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
}

Export-ModuleMember -Function Register-CupidGBFileAssociations, Unregister-CupidGBFileAssociations,
    Send-CupidGBShellAssociationChange
