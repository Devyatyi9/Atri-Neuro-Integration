#pragma once

#include <windows.h>

struct iTVPFunctionExporter;

extern "C" __declspec(dllexport) HRESULT _stdcall V2Link(
    iTVPFunctionExporter *exporter);
extern "C" __declspec(dllexport) HRESULT _stdcall V2Unlink();

void ShutdownPluginLifecycle();
void ShutdownPluginWorkers();
