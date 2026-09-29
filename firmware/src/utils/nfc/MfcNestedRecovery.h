#pragma once
#include <cstdint>
namespace MfcNestedRecovery {
struct Result { bool started=false; bool success=false; uint8_t recovered=0; bool foundA[40]={}; bool foundB[40]={}; uint8_t keysA[40][6]={}; uint8_t keysB[40][6]={}; };
enum class LogLevel : uint8_t { Info, Success, Warning, Error, Debug };
using LogFn = void(*)(const char*, LogLevel, void*);
using ProgressFn = void(*)(const char*, int, void*);
Result run(uint8_t sectors, const uint8_t uid[7], uint8_t uidLen,
           const bool foundA[40], const bool foundB[40],
           const uint8_t keysA[40][6], const uint8_t keysB[40][6],
           LogFn log=nullptr, ProgressFn progress=nullptr, void* ctx=nullptr);
}
