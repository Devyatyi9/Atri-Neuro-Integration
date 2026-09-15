#pragma once

#include <windows.h>

struct iTVPFunctionExporter;

extern "C" __declspec(dllexport) HRESULT _stdcall V2Link(
    iTVPFunctionExporter *exporter);
extern "C" __declspec(dllexport) HRESULT _stdcall V2Unlink();

void ShutdownPluginLifecycle();
void ShutdownPluginWorkers();

// The engine's function exporter handed to V2Link, or null before that. Lets a
// function be looked up without calling it: the stub wrappers throw when a
// function is missing, the exporter only answers false.
iTVPFunctionExporter *PluginFunctionExporter();
