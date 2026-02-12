#pragma once

void RenderImGuiWindows();

// TJS functions for Object Inspector (defined in dllmain.cpp)
extern "C" {
    const char* neuroImGuiGetKeys(const char* objectName, int offset, int count);
    int neuroImGuiGetKeysCount();
    const char* neuroImGuiGetPropertyType(const char* objectName, const char* propName);
    const char* neuroImGuiGetPropertyValue(const char* objectName, const char* propName);
    const char* neuroImGuiGetFlags(const char* flagType);
}
