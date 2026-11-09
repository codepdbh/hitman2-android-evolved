# Import surface

Every function the game's modules import, grouped by provider. Functions from
system DLLs are the Win32/DirectX surface the Android platform layer must answer;
functions from local modules are engine-internal links resolved by recompiling both sides.

| Provider | Local? | Distinct functions | Used by |
|---|---|--:|---|
| `advapi32.dll` | no | 8 | `config.exe`, `eax.dll`, `hitman2.exe`, `unins000.exe` |
| `comctl32.dll` | no | 36 | `unins000.exe` |
| `comdlg32.dll` | no | 2 | `unins000.exe` |
| `d3d8.dll` | no | 1 | `RenderD3D.dll` |
| `dinput8.dll` | no | 1 | `hitman2.exe` |
| `dsound.dll` | no | 1 | `hitman2.exe` |
| `eax.dll` | yes | 1 | `hitman2.exe` |
| `gdi32.dll` | no | 87 | `RenderD3D.dll`, `RenderOpenGL.dll`, `hitman2.exe`, `unins000.exe` |
| `hid.dll` | no | 14 | `p5dll.dll` |
| `hitman2.exe` | yes | 28 | `RenderD3D.dll`, `RenderOpenGL.dll` |
| `kernel32.dll` | no | 176 | `Msvcp60.dll`, `RenderD3D.dll`, `RenderOpenGL.dll`, `config.exe`, `eax.dll`, `hitman2.exe`, `jpegdll.dll`, `msvcrt.dll`, `p5dll.dll`, `unins000.exe`, `xmlparse.dll` |
| `mpr.dll` | no | 5 | `unins000.exe` |
| `msimg32.dll` | no | 1 | `unins000.exe` |
| `msvcrt.dll` | yes | 76 | `Msvcp60.dll` |
| `ole32.dll` | no | 6 | `eax.dll`, `unins000.exe` |
| `oleaut32.dll` | no | 1 | `unins000.exe` |
| `opengl32.dll` | no | 64 | `RenderOpenGL.dll` |
| `p5dll.dll` | yes | 4 | `hitman2.exe` |
| `setupapi.dll` | no | 4 | `p5dll.dll` |
| `shell32.dll` | no | 4 | `unins000.exe` |
| `user32.dll` | no | 215 | `RenderD3D.dll`, `RenderOpenGL.dll`, `config.exe`, `eax.dll`, `hitman2.exe`, `p5dll.dll`, `unins000.exe` |
| `version.dll` | no | 3 | `unins000.exe` |
| `winspool.drv` | no | 1 | `unins000.exe` |

## `advapi32.dll` (8)

| Function | Imported by |
|---|---|
| `AdjustTokenPrivileges` | unins000.exe |
| `RegCloseKey` | config.exe, eax.dll, hitman2.exe |
| `RegCreateKeyExA` | eax.dll, hitman2.exe |
| `RegDeleteKeyA` | eax.dll |
| `RegEnumKeyExA` | eax.dll |
| `RegOpenKeyExA` | config.exe, eax.dll, hitman2.exe |
| `RegQueryValueExA` | config.exe, hitman2.exe |
| `RegSetValueExA` | eax.dll |

## `comctl32.dll` (36)

| Function | Imported by |
|---|---|
| `_TrackMouseEvent` | unins000.exe |
| `FlatSB_GetScrollInfo` | unins000.exe |
| `FlatSB_GetScrollPos` | unins000.exe |
| `FlatSB_SetScrollInfo` | unins000.exe |
| `FlatSB_SetScrollPos` | unins000.exe |
| `FlatSB_SetScrollProp` | unins000.exe |
| `ImageList_Add` | unins000.exe |
| `ImageList_BeginDrag` | unins000.exe |
| `ImageList_Copy` | unins000.exe |
| `ImageList_Create` | unins000.exe |
| `ImageList_Destroy` | unins000.exe |
| `ImageList_DragEnter` | unins000.exe |
| `ImageList_DragLeave` | unins000.exe |
| `ImageList_DragMove` | unins000.exe |
| `ImageList_DragShowNolock` | unins000.exe |
| `ImageList_Draw` | unins000.exe |
| `ImageList_DrawEx` | unins000.exe |
| `ImageList_EndDrag` | unins000.exe |
| `ImageList_GetBkColor` | unins000.exe |
| `ImageList_GetDragImage` | unins000.exe |
| `ImageList_GetIcon` | unins000.exe |
| `ImageList_GetIconSize` | unins000.exe |
| `ImageList_GetImageCount` | unins000.exe |
| `ImageList_GetImageInfo` | unins000.exe |
| `ImageList_LoadImageW` | unins000.exe |
| `ImageList_Read` | unins000.exe |
| `ImageList_Remove` | unins000.exe |
| `ImageList_Replace` | unins000.exe |
| `ImageList_ReplaceIcon` | unins000.exe |
| `ImageList_SetBkColor` | unins000.exe |
| `ImageList_SetIconSize` | unins000.exe |
| `ImageList_SetImageCount` | unins000.exe |
| `ImageList_SetOverlayImage` | unins000.exe |
| `ImageList_Write` | unins000.exe |
| `InitCommonControls` | unins000.exe |
| `InitializeFlatSB` | unins000.exe |

## `comdlg32.dll` (2)

| Function | Imported by |
|---|---|
| `GetOpenFileNameW` | unins000.exe |
| `GetSaveFileNameW` | unins000.exe |

## `d3d8.dll` (1)

| Function | Imported by |
|---|---|
| `Direct3DCreate8` | RenderD3D.dll |

## `dinput8.dll` (1)

| Function | Imported by |
|---|---|
| `DirectInput8Create` | hitman2.exe |

## `dsound.dll` (1)

| Function | Imported by |
|---|---|
| `ordinal#2` | hitman2.exe |

## `eax.dll` (1)

| Function | Imported by |
|---|---|
| `ordinal#6` | hitman2.exe |

## `gdi32.dll` (87)

| Function | Imported by |
|---|---|
| `AddFontResourceW` | unins000.exe |
| `Arc` | unins000.exe |
| `BitBlt` | unins000.exe |
| `Chord` | unins000.exe |
| `CreateBitmap` | unins000.exe |
| `CreateBrushIndirect` | unins000.exe |
| `CreateCompatibleBitmap` | unins000.exe |
| `CreateCompatibleDC` | unins000.exe |
| `CreateDCW` | unins000.exe |
| `CreateDIBitmap` | unins000.exe |
| `CreateDIBSection` | unins000.exe |
| `CreateFontIndirectW` | unins000.exe |
| `CreateHalftonePalette` | unins000.exe |
| `CreateICW` | unins000.exe |
| `CreatePalette` | unins000.exe |
| `CreatePenIndirect` | unins000.exe |
| `CreateRectRgn` | unins000.exe |
| `CreateSolidBrush` | unins000.exe |
| `DeleteDC` | unins000.exe |
| `DeleteObject` | unins000.exe |
| `DescribePixelFormat` | RenderD3D.dll, RenderOpenGL.dll |
| `Ellipse` | unins000.exe |
| `EndDoc` | unins000.exe |
| `EndPage` | unins000.exe |
| `EnumFontFamiliesExW` | unins000.exe |
| `EnumFontsW` | unins000.exe |
| `ExcludeClipRect` | unins000.exe |
| `ExtFloodFill` | unins000.exe |
| `ExtTextOutW` | unins000.exe |
| `FrameRgn` | unins000.exe |
| `GetBitmapBits` | unins000.exe |
| `GetBrushOrgEx` | unins000.exe |
| `GetClipBox` | unins000.exe |
| `GetCurrentPositionEx` | unins000.exe |
| `GetDCOrgEx` | unins000.exe |
| `GetDeviceCaps` | RenderD3D.dll, RenderOpenGL.dll, unins000.exe |
| `GetDIBColorTable` | unins000.exe |
| `GetDIBits` | unins000.exe |
| `GetObjectW` | unins000.exe |
| `GetPaletteEntries` | unins000.exe |
| `GetPixel` | unins000.exe |
| `GetPixelFormat` | RenderD3D.dll, RenderOpenGL.dll |
| `GetRgnBox` | unins000.exe |
| `GetStockObject` | RenderD3D.dll, RenderOpenGL.dll, hitman2.exe, unins000.exe |
| `GetSystemPaletteEntries` | unins000.exe |
| `GetTextExtentPoint32W` | unins000.exe |
| `GetTextExtentPointW` | unins000.exe |
| `GetTextMetricsW` | unins000.exe |
| `GetWindowOrgEx` | unins000.exe |
| `IntersectClipRect` | unins000.exe |
| `LineDDA` | unins000.exe |
| `LineTo` | unins000.exe |
| `MaskBlt` | unins000.exe |
| `MoveToEx` | unins000.exe |
| `PatBlt` | unins000.exe |
| `Pie` | unins000.exe |
| `Polyline` | unins000.exe |
| `RealizePalette` | unins000.exe |
| `Rectangle` | unins000.exe |
| `RectVisible` | unins000.exe |
| `RemoveFontResourceW` | unins000.exe |
| `RestoreDC` | unins000.exe |
| `RoundRect` | unins000.exe |
| `SaveDC` | unins000.exe |
| `SelectClipRgn` | unins000.exe |
| `SelectObject` | unins000.exe |
| `SelectPalette` | unins000.exe |
| `SetAbortProc` | unins000.exe |
| `SetBkColor` | unins000.exe |
| `SetBkMode` | unins000.exe |
| `SetBrushOrgEx` | unins000.exe |
| `SetDeviceGammaRamp` | RenderOpenGL.dll |
| `SetDIBColorTable` | unins000.exe |
| `SetPixel` | unins000.exe |
| `SetPixelFormat` | RenderOpenGL.dll |
| `SetROP2` | unins000.exe |
| `SetStretchBltMode` | unins000.exe |
| `SetTextAlign` | unins000.exe |
| `SetTextColor` | unins000.exe |
| `SetViewportOrgEx` | unins000.exe |
| `SetWindowOrgEx` | unins000.exe |
| `StartDocW` | unins000.exe |
| `StartPage` | unins000.exe |
| `StretchBlt` | unins000.exe |
| `StretchDIBits` | unins000.exe |
| `SwapBuffers` | RenderOpenGL.dll |
| `UnrealizeObject` | unins000.exe |

## `hid.dll` (14)

| Function | Imported by |
|---|---|
| `HidD_FlushQueue` | p5dll.dll |
| `HidD_FreePreparsedData` | p5dll.dll |
| `HidD_GetAttributes` | p5dll.dll |
| `HidD_GetFeature` | p5dll.dll |
| `HidD_GetHidGuid` | p5dll.dll |
| `HidD_GetManufacturerString` | p5dll.dll |
| `HidD_GetPreparsedData` | p5dll.dll |
| `HidD_GetProductString` | p5dll.dll |
| `HidD_GetSerialNumberString` | p5dll.dll |
| `HidD_SetFeature` | p5dll.dll |
| `HidP_GetCaps` | p5dll.dll |
| `HidP_GetSpecificButtonCaps` | p5dll.dll |
| `HidP_GetSpecificValueCaps` | p5dll.dll |
| `HidP_MaxUsageListLength` | p5dll.dll |

## `hitman2.exe` (28)

| Function | Imported by |
|---|---|
| `??0ConstrainedParticleSystem@@QAE@XZ` | RenderD3D.dll, RenderOpenGL.dll |
| `??1ConstrainedParticleSystem@@UAE@XZ` | RenderD3D.dll, RenderOpenGL.dll |
| `??5ZLoadGame@@QAEAAV0@AA_N@Z` | RenderD3D.dll, RenderOpenGL.dll |
| `??5ZLoadGame@@QAEAAV0@AAH@Z` | RenderD3D.dll, RenderOpenGL.dll |
| `??5ZLoadGame@@QAEAAV0@AAI@Z` | RenderD3D.dll, RenderOpenGL.dll |
| `??5ZLoadGame@@QAEAAV0@AAVLINKSORTREFTAB@@@Z` | RenderD3D.dll, RenderOpenGL.dll |
| `??5ZLoadGame@@QAEAAV0@AAY02M@Z` | RenderD3D.dll, RenderOpenGL.dll |
| `??6ZSaveGame@@QAEAAV0@_N@Z` | RenderD3D.dll, RenderOpenGL.dll |
| `??6ZSaveGame@@QAEAAV0@AAVLINKSORTREFTAB@@@Z` | RenderD3D.dll, RenderOpenGL.dll |
| `??6ZSaveGame@@QAEAAV0@AAY02M@Z` | RenderD3D.dll, RenderOpenGL.dll |
| `??6ZSaveGame@@QAEAAV0@H@Z` | RenderD3D.dll, RenderOpenGL.dll |
| `??6ZSaveGame@@QAEAAV0@I@Z` | RenderD3D.dll, RenderOpenGL.dll |
| `?g_pInt3Handler@@3P6IXXZA` | RenderD3D.dll, RenderOpenGL.dll |
| `?g_pSysCom@@3PAVZSysCom@@A` | RenderD3D.dll, RenderOpenGL.dll |
| `?g_pSysFile@@3PAVZSysFile@@A` | RenderD3D.dll, RenderOpenGL.dll |
| `?g_pSysInterface@@3PAVZSysInterface@@A` | RenderD3D.dll, RenderOpenGL.dll |
| `?g_pSysMem@@3PAVZSysMem@@A` | RenderD3D.dll, RenderOpenGL.dll |
| `?GeomPtrToRef@ZEngineDataBase@@QAEIPAVZBaseGeom@@@Z` | RenderD3D.dll, RenderOpenGL.dll |
| `?GeomPtrToRef@ZEngineDataBase@@QAEIPAVZGEOM@@@Z` | RenderD3D.dll, RenderOpenGL.dll |
| `?GeomRefToBasePtr@ZEngineDataBase@@QAEPAVZBaseGeom@@I@Z` | RenderD3D.dll, RenderOpenGL.dll |
| `?GeomRefToPtr@ZEngineDataBase@@QAEPAVZGEOM@@I@Z` | RenderD3D.dll, RenderOpenGL.dll |
| `?GetAnimBuffer@ZEngineDataBase@@QAEPADXZ` | RenderD3D.dll, RenderOpenGL.dll |
| `?Init@ConstrainedParticleSystem@@UAEXXZ` | RenderD3D.dll, RenderOpenGL.dll |
| `?InitOkX@ConstrainedParticleSystem@@UAEXXZ` | RenderD3D.dll, RenderOpenGL.dll |
| `?Load@ConstrainedParticleSystem@@QAE_NPAVZLoadGame@@@Z` | RenderD3D.dll, RenderOpenGL.dll |
| `?RunTime@ZEngineDataBase@@QAE_NXZ` | RenderD3D.dll, RenderOpenGL.dll |
| `?s_fGravity@ConstrainedParticleSystem@@2MA` | RenderD3D.dll, RenderOpenGL.dll |
| `?Save@ConstrainedParticleSystem@@QAE_NPAVZSaveGame@@@Z` | RenderD3D.dll, RenderOpenGL.dll |

## `kernel32.dll` (176)

| Function | Imported by |
|---|---|
| `Beep` | msvcrt.dll |
| `CloseHandle` | RenderD3D.dll, RenderOpenGL.dll, config.exe, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll |
| `CompareStringA` | hitman2.exe, jpegdll.dll, msvcrt.dll |
| `CompareStringW` | hitman2.exe, jpegdll.dll, msvcrt.dll |
| `CopyFileA` | hitman2.exe |
| `CreateDirectoryA` | hitman2.exe, msvcrt.dll |
| `CreateDirectoryW` | msvcrt.dll |
| `CreateEventA` | hitman2.exe |
| `CreateFileA` | config.exe, hitman2.exe, msvcrt.dll, p5dll.dll |
| `CreateFileW` | msvcrt.dll |
| `CreateMutexA` | hitman2.exe |
| `CreatePipe` | msvcrt.dll |
| `CreateProcessA` | config.exe, msvcrt.dll |
| `CreateProcessW` | msvcrt.dll |
| `CreateThread` | hitman2.exe, msvcrt.dll, p5dll.dll |
| `DeleteCriticalSection` | Msvcp60.dll, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll |
| `DeleteFileA` | hitman2.exe, msvcrt.dll |
| `DeleteFileW` | msvcrt.dll |
| `DisableThreadLibraryCalls` | Msvcp60.dll, RenderD3D.dll, RenderOpenGL.dll |
| `DosDateTimeToFileTime` | hitman2.exe |
| `DuplicateHandle` | msvcrt.dll |
| `EnterCriticalSection` | Msvcp60.dll, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll |
| `EnumSystemLocalesA` | msvcrt.dll |
| `ExitProcess` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `ExitThread` | msvcrt.dll, p5dll.dll |
| `FileTimeToDosDateTime` | hitman2.exe |
| `FileTimeToLocalFileTime` | hitman2.exe, msvcrt.dll |
| `FileTimeToSystemTime` | hitman2.exe, msvcrt.dll |
| `FindClose` | hitman2.exe, msvcrt.dll |
| `FindFirstFileA` | hitman2.exe, msvcrt.dll |
| `FindFirstFileW` | msvcrt.dll |
| `FindNextFileA` | hitman2.exe, msvcrt.dll |
| `FindNextFileW` | msvcrt.dll |
| `FindResourceA` | config.exe |
| `FlushFileBuffers` | RenderD3D.dll, RenderOpenGL.dll, config.exe, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll |
| `FormatMessageA` | RenderOpenGL.dll, hitman2.exe |
| `FreeEnvironmentStringsA` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `FreeEnvironmentStringsW` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `FreeLibrary` | hitman2.exe, msvcrt.dll |
| `FreeResource` | config.exe |
| `GetACP` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `GetCommandLineA` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `GetCommandLineW` | msvcrt.dll |
| `GetConsoleMode` | msvcrt.dll |
| `GetCPInfo` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `GetCurrentDirectoryA` | hitman2.exe, msvcrt.dll |
| `GetCurrentDirectoryW` | msvcrt.dll |
| `GetCurrentProcess` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `GetCurrentProcessId` | msvcrt.dll |
| `GetCurrentThread` | hitman2.exe, msvcrt.dll |
| `GetCurrentThreadId` | eax.dll, jpegdll.dll, msvcrt.dll, p5dll.dll |
| `GetDiskFreeSpaceA` | msvcrt.dll |
| `GetDiskFreeSpaceExA` | hitman2.exe |
| `GetDriveTypeA` | hitman2.exe, msvcrt.dll |
| `GetDriveTypeW` | msvcrt.dll |
| `GetEnvironmentStrings` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `GetEnvironmentStringsW` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `GetEnvironmentVariableA` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `GetExitCodeProcess` | msvcrt.dll |
| `GetFileAttributesA` | msvcrt.dll |
| `GetFileAttributesW` | msvcrt.dll |
| `GetFileInformationByHandle` | msvcrt.dll |
| `GetFileSize` | hitman2.exe |
| `GetFileTime` | hitman2.exe |
| `GetFileType` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `GetFullPathNameA` | msvcrt.dll |
| `GetFullPathNameW` | msvcrt.dll |
| `GetLastError` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll |
| `GetLocaleInfoA` | msvcrt.dll |
| `GetLocaleInfoW` | msvcrt.dll |
| `GetLocalTime` | hitman2.exe, msvcrt.dll |
| `GetLogicalDrives` | msvcrt.dll |
| `GetLogicalDriveStringsA` | hitman2.exe |
| `GetModuleFileNameA` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `GetModuleFileNameW` | msvcrt.dll |
| `GetModuleHandleA` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `GetNumberOfConsoleInputEvents` | msvcrt.dll |
| `GetOEMCP` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `GetProcAddress` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `GetProcessHeap` | hitman2.exe |
| `GetStartupInfoA` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `GetStdHandle` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `GetStringTypeA` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `GetStringTypeW` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `GetSystemTime` | hitman2.exe |
| `GetSystemTimeAsFileTime` | hitman2.exe, msvcrt.dll |
| `GetTimeZoneInformation` | hitman2.exe, msvcrt.dll |
| `GetUserDefaultLCID` | msvcrt.dll |
| `GetVersion` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, p5dll.dll, xmlparse.dll |
| `GetVersionExA` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `GlobalAddAtomA` | hitman2.exe |
| `GlobalDeleteAtom` | hitman2.exe |
| `GlobalGetAtomNameA` | hitman2.exe |
| `HeapAlloc` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `HeapCompact` | msvcrt.dll |
| `HeapCreate` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `HeapDestroy` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `HeapFree` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `HeapReAlloc` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `HeapSize` | RenderD3D.dll, RenderOpenGL.dll, hitman2.exe, msvcrt.dll, p5dll.dll |
| `HeapValidate` | msvcrt.dll |
| `HeapWalk` | msvcrt.dll |
| `InitializeCriticalSection` | Msvcp60.dll, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll |
| `InterlockedDecrement` | Msvcp60.dll, eax.dll, jpegdll.dll, p5dll.dll |
| `InterlockedExchange` | Msvcp60.dll |
| `InterlockedIncrement` | Msvcp60.dll, eax.dll, jpegdll.dll, p5dll.dll |
| `IsBadCodePtr` | RenderD3D.dll, RenderOpenGL.dll, eax.dll, hitman2.exe, msvcrt.dll |
| `IsBadReadPtr` | RenderD3D.dll, RenderOpenGL.dll, eax.dll, hitman2.exe, msvcrt.dll |
| `IsBadWritePtr` | RenderD3D.dll, RenderOpenGL.dll, eax.dll, hitman2.exe, msvcrt.dll |
| `IsValidCodePage` | msvcrt.dll |
| `IsValidLocale` | msvcrt.dll |
| `LCMapStringA` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `LCMapStringW` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `LeaveCriticalSection` | Msvcp60.dll, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll |
| `LoadLibraryA` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `LoadResource` | config.exe |
| `LocalAlloc` | p5dll.dll |
| `LocalFileTimeToFileTime` | msvcrt.dll |
| `LocalFree` | RenderOpenGL.dll, hitman2.exe, p5dll.dll |
| `LockFile` | msvcrt.dll |
| `LockResource` | config.exe |
| `MoveFileA` | hitman2.exe, msvcrt.dll |
| `MoveFileW` | msvcrt.dll |
| `MultiByteToWideChar` | Msvcp60.dll, RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `OutputDebugStringA` | hitman2.exe |
| `PeekConsoleInputA` | msvcrt.dll |
| `PeekNamedPipe` | msvcrt.dll |
| `QueryPerformanceCounter` | hitman2.exe |
| `QueryPerformanceFrequency` | hitman2.exe |
| `RaiseException` | RenderD3D.dll, RenderOpenGL.dll, eax.dll, hitman2.exe, msvcrt.dll, p5dll.dll |
| `ReadConsoleA` | msvcrt.dll |
| `ReadConsoleInputA` | msvcrt.dll |
| `ReadFile` | config.exe, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll |
| `ReadProcessMemory` | hitman2.exe |
| `RemoveDirectoryA` | hitman2.exe, msvcrt.dll |
| `RemoveDirectoryW` | msvcrt.dll |
| `ResetEvent` | hitman2.exe |
| `ResumeThread` | msvcrt.dll, p5dll.dll |
| `RtlUnwind` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `SetConsoleCtrlHandler` | hitman2.exe, msvcrt.dll |
| `SetConsoleMode` | msvcrt.dll |
| `SetCurrentDirectoryA` | hitman2.exe, msvcrt.dll |
| `SetCurrentDirectoryW` | msvcrt.dll |
| `SetEndOfFile` | hitman2.exe, msvcrt.dll |
| `SetEnvironmentVariableA` | hitman2.exe, jpegdll.dll, msvcrt.dll |
| `SetEnvironmentVariableW` | msvcrt.dll |
| `SetErrorMode` | hitman2.exe, msvcrt.dll |
| `SetEvent` | hitman2.exe |
| `SetFileAttributesA` | msvcrt.dll |
| `SetFileAttributesW` | msvcrt.dll |
| `SetFilePointer` | RenderD3D.dll, RenderOpenGL.dll, config.exe, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll |
| `SetFileTime` | msvcrt.dll |
| `SetHandleCount` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `SetLastError` | eax.dll, jpegdll.dll, msvcrt.dll, p5dll.dll |
| `SetLocalTime` | msvcrt.dll |
| `SetStdHandle` | RenderD3D.dll, RenderOpenGL.dll, config.exe, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll |
| `SetThreadPriority` | hitman2.exe, p5dll.dll |
| `SetThreadPriorityBoost` | hitman2.exe |
| `SetUnhandledExceptionFilter` | RenderD3D.dll, RenderOpenGL.dll, eax.dll, hitman2.exe, msvcrt.dll |
| `Sleep` | Msvcp60.dll, hitman2.exe, msvcrt.dll, p5dll.dll, unins000.exe |
| `SystemTimeToFileTime` | msvcrt.dll |
| `TerminateProcess` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `TerminateThread` | p5dll.dll |
| `TlsAlloc` | eax.dll, jpegdll.dll, msvcrt.dll, p5dll.dll |
| `TlsFree` | eax.dll, jpegdll.dll, msvcrt.dll, p5dll.dll |
| `TlsGetValue` | eax.dll, jpegdll.dll, msvcrt.dll, p5dll.dll |
| `TlsSetValue` | eax.dll, jpegdll.dll, msvcrt.dll, p5dll.dll |
| `UnhandledExceptionFilter` | config.exe, hitman2.exe, msvcrt.dll, p5dll.dll |
| `UnlockFile` | msvcrt.dll |
| `VirtualAlloc` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `VirtualFree` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `VirtualQuery` | hitman2.exe |
| `WaitForSingleObject` | hitman2.exe, msvcrt.dll, p5dll.dll |
| `WideCharToMultiByte` | Msvcp60.dll, RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |
| `WriteConsoleA` | msvcrt.dll |
| `WriteFile` | RenderD3D.dll, RenderOpenGL.dll, config.exe, eax.dll, hitman2.exe, jpegdll.dll, msvcrt.dll, p5dll.dll, xmlparse.dll |

## `mpr.dll` (5)

| Function | Imported by |
|---|---|
| `WNetCloseEnum` | unins000.exe |
| `WNetEnumResourceW` | unins000.exe |
| `WNetGetConnectionW` | unins000.exe |
| `WNetGetUniversalNameW` | unins000.exe |
| `WNetOpenEnumW` | unins000.exe |

## `msimg32.dll` (1)

| Function | Imported by |
|---|---|
| `AlphaBlend` | unins000.exe |

## `msvcrt.dll` (76)

| Function | Imported by |
|---|---|
| `??0exception@@QAE@ABQBD@Z` | Msvcp60.dll |
| `??0exception@@QAE@ABV0@@Z` | Msvcp60.dll |
| `??1exception@@UAE@XZ` | Msvcp60.dll |
| `??1type_info@@UAE@XZ` | Msvcp60.dll |
| `??2@YAPAXI@Z` | Msvcp60.dll |
| `??4exception@@QAEAAV0@ABV0@@Z` | Msvcp60.dll |
| `?terminate@@YAXXZ` | Msvcp60.dll |
| `?what@exception@@UBEPBDXZ` | Msvcp60.dll |
| `__crtCompareStringA` | Msvcp60.dll |
| `__crtLCMapStringA` | Msvcp60.dll |
| `__CxxFrameHandler` | Msvcp60.dll |
| `__dllonexit` | Msvcp60.dll |
| `__lc_codepage` | Msvcp60.dll |
| `__lc_collate_cp` | Msvcp60.dll |
| `__lc_handle` | Msvcp60.dll |
| `__mb_cur_max` | Msvcp60.dll |
| `__setlc_active` | Msvcp60.dll |
| `__unguarded_readlc_active` | Msvcp60.dll |
| `_adjust_fdiv` | Msvcp60.dll |
| `_CxxThrowException` | Msvcp60.dll |
| `_errno` | Msvcp60.dll |
| `_except_handler3` | Msvcp60.dll |
| `_ftol` | Msvcp60.dll |
| `_Getdays` | Msvcp60.dll |
| `_Getmonths` | Msvcp60.dll |
| `_Gettnames` | Msvcp60.dll |
| `_initterm` | Msvcp60.dll |
| `_iob` | Msvcp60.dll |
| `_isctype` | Msvcp60.dll |
| `_lock` | Msvcp60.dll |
| `_onexit` | Msvcp60.dll |
| `_pctype` | Msvcp60.dll |
| `_Strftime` | Msvcp60.dll |
| `_unlock` | Msvcp60.dll |
| `atan2` | Msvcp60.dll |
| `cos` | Msvcp60.dll |
| `exp` | Msvcp60.dll |
| `fclose` | Msvcp60.dll |
| `fflush` | Msvcp60.dll |
| `fgetc` | Msvcp60.dll |
| `fgetpos` | Msvcp60.dll |
| `fgetwc` | Msvcp60.dll |
| `fopen` | Msvcp60.dll |
| `fputc` | Msvcp60.dll |
| `fputwc` | Msvcp60.dll |
| `free` | Msvcp60.dll |
| `fseek` | Msvcp60.dll |
| `fsetpos` | Msvcp60.dll |
| `fwrite` | Msvcp60.dll |
| `ldexp` | Msvcp60.dll |
| `localeconv` | Msvcp60.dll |
| `log` | Msvcp60.dll |
| `malloc` | Msvcp60.dll |
| `memchr` | Msvcp60.dll |
| `memcmp` | Msvcp60.dll |
| `memcpy` | Msvcp60.dll |
| `memmove` | Msvcp60.dll |
| `memset` | Msvcp60.dll |
| `pow` | Msvcp60.dll |
| `realloc` | Msvcp60.dll |
| `setlocale` | Msvcp60.dll |
| `setvbuf` | Msvcp60.dll |
| `sin` | Msvcp60.dll |
| `sprintf` | Msvcp60.dll |
| `sqrt` | Msvcp60.dll |
| `strcmp` | Msvcp60.dll |
| `strcspn` | Msvcp60.dll |
| `strlen` | Msvcp60.dll |
| `strtod` | Msvcp60.dll |
| `strtol` | Msvcp60.dll |
| `strtoul` | Msvcp60.dll |
| `towlower` | Msvcp60.dll |
| `towupper` | Msvcp60.dll |
| `ungetc` | Msvcp60.dll |
| `ungetwc` | Msvcp60.dll |
| `wcslen` | Msvcp60.dll |

## `ole32.dll` (6)

| Function | Imported by |
|---|---|
| `CoCreateInstance` | eax.dll |
| `CoDisconnectObject` | unins000.exe |
| `CoInitialize` | eax.dll |
| `CoTaskMemFree` | eax.dll |
| `CoUninitialize` | eax.dll |
| `StringFromCLSID` | eax.dll |

## `oleaut32.dll` (1)

| Function | Imported by |
|---|---|
| `SysFreeString` | unins000.exe |

## `opengl32.dll` (64)

| Function | Imported by |
|---|---|
| `glAlphaFunc` | RenderOpenGL.dll |
| `glBegin` | RenderOpenGL.dll |
| `glBindTexture` | RenderOpenGL.dll |
| `glBlendFunc` | RenderOpenGL.dll |
| `glClear` | RenderOpenGL.dll |
| `glClearColor` | RenderOpenGL.dll |
| `glColor4f` | RenderOpenGL.dll |
| `glColor4ub` | RenderOpenGL.dll |
| `glColorMaterial` | RenderOpenGL.dll |
| `glColorPointer` | RenderOpenGL.dll |
| `glCullFace` | RenderOpenGL.dll |
| `glDeleteTextures` | RenderOpenGL.dll |
| `glDepthFunc` | RenderOpenGL.dll |
| `glDepthMask` | RenderOpenGL.dll |
| `glDisable` | RenderOpenGL.dll |
| `glDisableClientState` | RenderOpenGL.dll |
| `glDrawBuffer` | RenderOpenGL.dll |
| `glDrawElements` | RenderOpenGL.dll |
| `glDrawPixels` | RenderOpenGL.dll |
| `glEnable` | RenderOpenGL.dll |
| `glEnableClientState` | RenderOpenGL.dll |
| `glEnd` | RenderOpenGL.dll |
| `glFinish` | RenderOpenGL.dll |
| `glFlush` | RenderOpenGL.dll |
| `glFogf` | RenderOpenGL.dll |
| `glFogfv` | RenderOpenGL.dll |
| `glFogi` | RenderOpenGL.dll |
| `glFrustum` | RenderOpenGL.dll |
| `glGenTextures` | RenderOpenGL.dll |
| `glGetIntegerv` | RenderOpenGL.dll |
| `glGetString` | RenderOpenGL.dll |
| `glLightf` | RenderOpenGL.dll |
| `glLightfv` | RenderOpenGL.dll |
| `glLightModelfv` | RenderOpenGL.dll |
| `glLoadIdentity` | RenderOpenGL.dll |
| `glLoadMatrixf` | RenderOpenGL.dll |
| `glMaterialfv` | RenderOpenGL.dll |
| `glMatrixMode` | RenderOpenGL.dll |
| `glNormalPointer` | RenderOpenGL.dll |
| `glOrtho` | RenderOpenGL.dll |
| `glPixelStorei` | RenderOpenGL.dll |
| `glPolygonMode` | RenderOpenGL.dll |
| `glRasterPos2i` | RenderOpenGL.dll |
| `glReadBuffer` | RenderOpenGL.dll |
| `glReadPixels` | RenderOpenGL.dll |
| `glScissor` | RenderOpenGL.dll |
| `glShadeModel` | RenderOpenGL.dll |
| `glTexCoord2f` | RenderOpenGL.dll |
| `glTexCoordPointer` | RenderOpenGL.dll |
| `glTexEnvf` | RenderOpenGL.dll |
| `glTexEnvi` | RenderOpenGL.dll |
| `glTexGenf` | RenderOpenGL.dll |
| `glTexImage2D` | RenderOpenGL.dll |
| `glTexParameteri` | RenderOpenGL.dll |
| `glTexSubImage2D` | RenderOpenGL.dll |
| `glVertex2f` | RenderOpenGL.dll |
| `glVertex3f` | RenderOpenGL.dll |
| `glVertex3fv` | RenderOpenGL.dll |
| `glVertexPointer` | RenderOpenGL.dll |
| `glViewport` | RenderOpenGL.dll |
| `wglCreateContext` | RenderOpenGL.dll |
| `wglDeleteContext` | RenderOpenGL.dll |
| `wglGetProcAddress` | RenderOpenGL.dll |
| `wglMakeCurrent` | RenderOpenGL.dll |

## `p5dll.dll` (4)

| Function | Imported by |
|---|---|
| `??0CP5DLL@@QAE@XZ` | hitman2.exe |
| `??1CP5DLL@@QAE@XZ` | hitman2.exe |
| `?P5_Init@CP5DLL@@QAEIXZ` | hitman2.exe |
| `?P5_SetMouseState@CP5DLL@@QAEXHI@Z` | hitman2.exe |

## `setupapi.dll` (4)

| Function | Imported by |
|---|---|
| `SetupDiDestroyDeviceInfoList` | p5dll.dll |
| `SetupDiEnumDeviceInterfaces` | p5dll.dll |
| `SetupDiGetClassDevsA` | p5dll.dll |
| `SetupDiGetDeviceInterfaceDetailA` | p5dll.dll |

## `shell32.dll` (4)

| Function | Imported by |
|---|---|
| `SHBrowseForFolderW` | unins000.exe |
| `SHChangeNotify` | unins000.exe |
| `SHGetMalloc` | unins000.exe |
| `SHGetPathFromIDListW` | unins000.exe |

## `user32.dll` (215)

| Function | Imported by |
|---|---|
| `ActivateKeyboardLayout` | unins000.exe |
| `AdjustWindowRectEx` | RenderD3D.dll, RenderOpenGL.dll, unins000.exe |
| `AppendMenuW` | unins000.exe |
| `BeginPaint` | unins000.exe |
| `BringWindowToTop` | unins000.exe |
| `CallNextHookEx` | unins000.exe |
| `CallWindowProcW` | unins000.exe |
| `ChangeDisplaySettingsA` | RenderOpenGL.dll |
| `CharLowerBuffW` | unins000.exe |
| `CharLowerW` | unins000.exe |
| `CharNextW` | unins000.exe |
| `CharToOemBuffA` | unins000.exe |
| `CharUpperBuffW` | unins000.exe |
| `CheckMenuItem` | unins000.exe |
| `ChildWindowFromPoint` | unins000.exe |
| `ClientToScreen` | RenderD3D.dll, RenderOpenGL.dll, unins000.exe |
| `ClipCursor` | RenderD3D.dll, RenderOpenGL.dll |
| `CopyIcon` | unins000.exe |
| `CreateAcceleratorTableW` | unins000.exe |
| `CreateIcon` | unins000.exe |
| `CreateMenu` | unins000.exe |
| `CreatePopupMenu` | unins000.exe |
| `CreateWindowExA` | RenderD3D.dll, RenderOpenGL.dll, hitman2.exe |
| `CreateWindowExW` | unins000.exe |
| `DefFrameProcW` | unins000.exe |
| `DefMDIChildProcW` | unins000.exe |
| `DefWindowProcA` | RenderD3D.dll, RenderOpenGL.dll, hitman2.exe |
| `DefWindowProcW` | unins000.exe |
| `DeleteMenu` | unins000.exe |
| `DestroyCursor` | unins000.exe |
| `DestroyIcon` | unins000.exe |
| `DestroyMenu` | unins000.exe |
| `DestroyWindow` | hitman2.exe, unins000.exe |
| `DialogBoxParamA` | config.exe |
| `DispatchMessageA` | config.exe, hitman2.exe, unins000.exe |
| `DispatchMessageW` | unins000.exe |
| `DrawEdge` | unins000.exe |
| `DrawFocusRect` | unins000.exe |
| `DrawFrameControl` | unins000.exe |
| `DrawIcon` | unins000.exe |
| `DrawIconEx` | unins000.exe |
| `DrawMenuBar` | unins000.exe |
| `DrawTextExW` | unins000.exe |
| `DrawTextW` | unins000.exe |
| `EnableMenuItem` | unins000.exe |
| `EnableScrollBar` | unins000.exe |
| `EnableWindow` | config.exe, unins000.exe |
| `EndDialog` | config.exe |
| `EndPaint` | unins000.exe |
| `EnumChildWindows` | unins000.exe |
| `EnumDisplaySettingsA` | RenderD3D.dll, RenderOpenGL.dll, config.exe |
| `EnumThreadWindows` | unins000.exe |
| `EnumWindows` | unins000.exe |
| `ExitWindowsEx` | unins000.exe |
| `FillRect` | unins000.exe |
| `FindWindowA` | hitman2.exe |
| `FindWindowExW` | unins000.exe |
| `FindWindowW` | unins000.exe |
| `FrameRect` | unins000.exe |
| `GetActiveWindow` | unins000.exe |
| `GetAsyncKeyState` | RenderD3D.dll, RenderOpenGL.dll, hitman2.exe |
| `GetCapture` | unins000.exe |
| `GetClassInfoExA` | RenderD3D.dll, RenderOpenGL.dll, hitman2.exe |
| `GetClassInfoW` | unins000.exe |
| `GetClassLongW` | unins000.exe |
| `GetClassNameW` | unins000.exe |
| `GetClientRect` | RenderD3D.dll, RenderOpenGL.dll, unins000.exe |
| `GetCursor` | RenderD3D.dll, RenderOpenGL.dll, unins000.exe |
| `GetCursorPos` | hitman2.exe, unins000.exe |
| `GetDC` | RenderD3D.dll, RenderOpenGL.dll, unins000.exe |
| `GetDCEx` | unins000.exe |
| `GetDesktopWindow` | RenderD3D.dll, RenderOpenGL.dll, eax.dll, unins000.exe |
| `GetDlgCtrlID` | unins000.exe |
| `GetDlgItem` | config.exe, unins000.exe |
| `GetFocus` | unins000.exe |
| `GetForegroundWindow` | unins000.exe |
| `GetIconInfo` | unins000.exe |
| `GetKeyboardLayout` | unins000.exe |
| `GetKeyboardLayoutList` | unins000.exe |
| `GetKeyboardLayoutNameW` | unins000.exe |
| `GetKeyboardState` | hitman2.exe, unins000.exe |
| `GetKeyNameTextA` | hitman2.exe |
| `GetKeyNameTextW` | unins000.exe |
| `GetKeyState` | unins000.exe |
| `GetLastActivePopup` | unins000.exe |
| `GetMenu` | unins000.exe |
| `GetMenuItemCount` | unins000.exe |
| `GetMenuItemID` | unins000.exe |
| `GetMenuItemInfoW` | unins000.exe |
| `GetMenuState` | unins000.exe |
| `GetMenuStringW` | unins000.exe |
| `GetMessageA` | config.exe, hitman2.exe |
| `GetMessagePos` | unins000.exe |
| `GetMessageW` | unins000.exe |
| `GetParent` | RenderD3D.dll, RenderOpenGL.dll, unins000.exe |
| `GetPropW` | unins000.exe |
| `GetScrollInfo` | unins000.exe |
| `GetScrollPos` | unins000.exe |
| `GetScrollRange` | unins000.exe |
| `GetSubMenu` | unins000.exe |
| `GetSysColor` | unins000.exe |
| `GetSysColorBrush` | unins000.exe |
| `GetSystemMenu` | unins000.exe |
| `GetSystemMetrics` | RenderD3D.dll, RenderOpenGL.dll, hitman2.exe, unins000.exe |
| `GetTopWindow` | unins000.exe |
| `GetWindow` | unins000.exe |
| `GetWindowDC` | RenderOpenGL.dll, unins000.exe |
| `GetWindowLongA` | RenderD3D.dll, RenderOpenGL.dll |
| `GetWindowLongW` | unins000.exe |
| `GetWindowPlacement` | unins000.exe |
| `GetWindowRect` | RenderD3D.dll, RenderOpenGL.dll, unins000.exe |
| `GetWindowTextW` | unins000.exe |
| `GetWindowThreadProcessId` | unins000.exe |
| `InflateRect` | unins000.exe |
| `InsertMenuItemW` | unins000.exe |
| `InsertMenuW` | unins000.exe |
| `IntersectRect` | unins000.exe |
| `InvalidateRect` | unins000.exe |
| `IsChild` | unins000.exe |
| `IsDialogMessageA` | unins000.exe |
| `IsDialogMessageW` | unins000.exe |
| `IsIconic` | unins000.exe |
| `IsRectEmpty` | unins000.exe |
| `IsWindow` | unins000.exe |
| `IsWindowEnabled` | unins000.exe |
| `IsWindowUnicode` | unins000.exe |
| `IsWindowVisible` | unins000.exe |
| `IsZoomed` | unins000.exe |
| `KillTimer` | unins000.exe |
| `LoadBitmapW` | unins000.exe |
| `LoadCursorA` | RenderD3D.dll, RenderOpenGL.dll, hitman2.exe |
| `LoadCursorW` | unins000.exe |
| `LoadIconA` | RenderD3D.dll, RenderOpenGL.dll |
| `LoadIconW` | unins000.exe |
| `LoadKeyboardLayoutW` | unins000.exe |
| `LoadStringW` | unins000.exe |
| `MapVirtualKeyA` | hitman2.exe |
| `MapVirtualKeyW` | unins000.exe |
| `MapWindowPoints` | unins000.exe |
| `MessageBeep` | unins000.exe |
| `MessageBoxA` | hitman2.exe, p5dll.dll |
| `MessageBoxW` | unins000.exe |
| `MoveWindow` | RenderD3D.dll, RenderOpenGL.dll, unins000.exe |
| `MsgWaitForMultipleObjects` | unins000.exe |
| `MsgWaitForMultipleObjectsEx` | unins000.exe |
| `OemToCharBuffA` | unins000.exe |
| `OffsetRect` | unins000.exe |
| `PeekMessageA` | hitman2.exe, unins000.exe |
| `PeekMessageW` | unins000.exe |
| `PostMessageA` | hitman2.exe |
| `PostMessageW` | unins000.exe |
| `PostQuitMessage` | config.exe, hitman2.exe, unins000.exe |
| `PtInRect` | unins000.exe |
| `RedrawWindow` | unins000.exe |
| `RegisterClassExA` | RenderD3D.dll, RenderOpenGL.dll, hitman2.exe |
| `RegisterClassW` | unins000.exe |
| `RegisterClipboardFormatW` | unins000.exe |
| `RegisterWindowMessageA` | hitman2.exe |
| `RegisterWindowMessageW` | unins000.exe |
| `ReleaseCapture` | RenderD3D.dll, RenderOpenGL.dll, unins000.exe |
| `ReleaseDC` | RenderD3D.dll, RenderOpenGL.dll, unins000.exe |
| `RemoveMenu` | unins000.exe |
| `RemovePropW` | unins000.exe |
| `ReplyMessage` | unins000.exe |
| `ScreenToClient` | hitman2.exe, unins000.exe |
| `ScrollWindow` | unins000.exe |
| `ScrollWindowEx` | unins000.exe |
| `SendDlgItemMessageA` | config.exe |
| `SendMessageA` | RenderD3D.dll, RenderOpenGL.dll, hitman2.exe, unins000.exe |
| `SendMessageTimeoutW` | unins000.exe |
| `SendMessageW` | unins000.exe |
| `SendNotifyMessageW` | unins000.exe |
| `SetActiveWindow` | unins000.exe |
| `SetCapture` | RenderD3D.dll, RenderOpenGL.dll, unins000.exe |
| `SetClassLongW` | unins000.exe |
| `SetCursor` | RenderD3D.dll, RenderOpenGL.dll, unins000.exe |
| `SetCursorPos` | RenderD3D.dll, RenderOpenGL.dll |
| `SetFocus` | RenderD3D.dll, RenderOpenGL.dll, unins000.exe |
| `SetForegroundWindow` | RenderD3D.dll, RenderOpenGL.dll, unins000.exe |
| `SetMenu` | unins000.exe |
| `SetMenuItemInfoW` | unins000.exe |
| `SetParent` | unins000.exe |
| `SetPropW` | unins000.exe |
| `SetRect` | unins000.exe |
| `SetRectEmpty` | unins000.exe |
| `SetScrollInfo` | unins000.exe |
| `SetScrollPos` | unins000.exe |
| `SetScrollRange` | unins000.exe |
| `SetTimer` | unins000.exe |
| `SetWindowLongA` | RenderD3D.dll, RenderOpenGL.dll, hitman2.exe |
| `SetWindowLongW` | unins000.exe |
| `SetWindowPlacement` | unins000.exe |
| `SetWindowPos` | hitman2.exe, unins000.exe |
| `SetWindowsHookExW` | unins000.exe |
| `SetWindowTextA` | RenderD3D.dll, RenderOpenGL.dll, hitman2.exe |
| `SetWindowTextW` | unins000.exe |
| `ShowCursor` | RenderOpenGL.dll |
| `ShowOwnedPopups` | unins000.exe |
| `ShowScrollBar` | unins000.exe |
| `ShowWindow` | RenderD3D.dll, RenderOpenGL.dll, hitman2.exe, unins000.exe |
| `SystemParametersInfoA` | hitman2.exe |
| `SystemParametersInfoW` | unins000.exe |
| `ToAscii` | hitman2.exe |
| `TrackPopupMenu` | unins000.exe |
| `TranslateMDISysAccel` | unins000.exe |
| `TranslateMessage` | config.exe, hitman2.exe, unins000.exe |
| `UnhookWindowsHookEx` | unins000.exe |
| `UnregisterClassA` | hitman2.exe |
| `UnregisterClassW` | unins000.exe |
| `UpdateWindow` | unins000.exe |
| `WaitForInputIdle` | unins000.exe |
| `WaitMessage` | hitman2.exe, unins000.exe |
| `WindowFromPoint` | unins000.exe |
| `wsprintfA` | hitman2.exe |
| `wvsprintfA` | hitman2.exe |

## `version.dll` (3)

| Function | Imported by |
|---|---|
| `GetFileVersionInfoSizeW` | unins000.exe |
| `GetFileVersionInfoW` | unins000.exe |
| `VerQueryValueW` | unins000.exe |

## `winspool.drv` (1)

| Function | Imported by |
|---|---|
| `GetDefaultPrinterW` | unins000.exe |
