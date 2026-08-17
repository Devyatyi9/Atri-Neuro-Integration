#pragma once

#include <Windows.h>

struct iTVPFunctionExporter;

iTVPFunctionExporter *WaitForExporterInit(DWORD timeoutMs);
