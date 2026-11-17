# SimpleArchive

A small native Win32 ZIP utility for Windows 10 version 1607 (build 14393) and later. The interface and application are C++; archive operations use the ZIP/Compressed Folders support built into Windows through the Shell COM API. It does not use .NET, NuGet, or third-party runtime libraries.

## Features

- Select a `.zip` file and extract its contents to a chosen folder.
- Create a `.zip` from the contents of a chosen folder.
- Uses Windows' built-in ZIP handler; this is ZIP-only, not a RAR/7z implementation.

Shell `CopyHere` operations run asynchronously and Windows does not provide a completion callback for this interface. The app reports that the operation was started; check the destination folder before closing Windows Explorer or shutting down.

## Build

Build with Visual Studio 2022 (C++ workload) and CMake, using a Windows 10 SDK. In a Visual Studio Developer Command Prompt:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

The executable is `build\Release\SimpleArchive.exe`. The MSVC runtime is statically linked (`/MT`), so the Visual C++ Redistributable is not required. Windows system components, including the Shell ZIP handler, are still required. Build separately with `-A Win32` for 32-bit Windows.

## Compatibility

The project targets Windows 10 APIs and is intended for Windows 10 version 1607 (build 14393) and newer. It is not a self-contained ZIP codec: extraction and compression depend on the built-in Windows Shell ZIP namespace.
