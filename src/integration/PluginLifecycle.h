#pragma once

#include <Windows.h>

struct iTVPFunctionExporter;

extern "C" __declspec(dllexport) bool __cdecl InitPlugin();
extern "C" __declspec(dllexport) HRESULT __stdcall V2Link(
    iTVPFunctionExporter *exporter);
extern "C" __declspec(dllexport) HRESULT __stdcall V2Unlink();
