# Windows integration

The desktop executable uses the Windows GUI subsystem and includes a CupidGB icon and version information. The icon comes from the project's SVG at `packaging/linux/cupidgb.svg`. A matching PNG is embedded in the Qt application for its windows and taskbar entry. The separate ROM runner remains a console tool.

## Open With registration

The installed folder contains `Register-CupidGB.ps1`, `Unregister-CupidGB.ps1`, and `CupidGB.FileAssociations.psm1`. Registration adds CupidGB as a per-user opener for `.gb` and `.gbc` cartridges and `.gbs` music. It preserves extension defaults, Windows `UserChoice` entries, and other applications' Open With entries. Administrator access is not required.

From PowerShell in that folder:

```powershell
./Register-CupidGB.ps1 -WhatIf
./Register-CupidGB.ps1
```

Choose a default opener through Windows Open With or Default Apps. To remove the registration:

```powershell
./Unregister-CupidGB.ps1
```

The scripts also accept `-ExecutablePath` when run from a source checkout or another folder. Use the full path to `cupidgb.exe`. After moving the application, run registration from its new folder. Unregistration from an older folder preserves a registration that now points elsewhere. Unregistration can also remove its entries after the executable has been deleted.

Registration checks its application names before writing. It refuses to replace entries that lack its ownership markers. Unregistration removes matching values created by these scripts and leaves later edits and unrelated values in place. Both operations support `-WhatIf` and can be repeated.

These controls follow the Windows [application registration](https://learn.microsoft.com/en-us/windows/win32/shell/app-registration), [file type](https://learn.microsoft.com/en-us/windows/win32/shell/fa-file-types), and [Default Programs](https://learn.microsoft.com/en-us/windows/win32/shell/default-programs) interfaces. They do not select a user's default application.

## Isolated registration checks

Run the script check without registering CupidGB in the current user's shell:

```powershell
./packaging/windows/test_file_associations.ps1
```

The check uses `RegLoadAppKey` to create a private file-backed registry hive outside HKCU and HKLM. It exercises registration, repeated operations, quoted Unicode paths, folder moves, removal after file deletion, collisions, and preservation of existing associations. It also verifies that the current user's extension defaults and `UserChoice` values remain unchanged. The automatic temporary hive is removed after its handles close.

For an inspectable hive file, pass a new path with `-HivePath`. That mode keeps the hive and creates a unique fixture directory beside it. Existing sibling folders and executables are preserved. Reusing an existing hive path is rejected before files or registry data change. The [Windows application hive API](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regloadappkeyw) does not require a registry mount or administrator privileges.

`test_file_association_preservation.ps1` checks two custom-hive runs beside existing executable fixtures, then verifies that rejecting an existing hive leaves all files and directories unchanged. CTest runs both scripts on Windows.

## Updating the icon

Normal builds use the checked-in ICO and PNG files. Regeneration needs Qt Gui and Svg development modules:

```powershell
cmake -S packaging/windows/icon_tool -B build-icon -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build-icon --config Release
./build-icon/Release/cupidgb_generate_icon.exe packaging/linux/cupidgb.svg `
    packaging/windows/cupidgb.ico packaging/windows/cupidgb.png
```

Use the actual Qt installation path, with its runtime available to the tool. The ICO contains 16, 24, 32, 48, 64, 128, and 256 pixel images with transparency. Qt Svg is needed only for regeneration; the application displays the embedded PNG through Qt Gui.

[Build and deployment instructions](../../docs/building.md) describe staging the executable, Qt libraries, SDL2 runtime, and required notices. The registry scripts do not install runtimes, copy boot ROMs, or change game saves.
