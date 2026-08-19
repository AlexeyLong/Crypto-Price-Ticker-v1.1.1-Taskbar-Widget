#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <commctrl.h>
#include <cstdio>
#include <mutex>
#include <new>
#include <olectl.h>
#include <shlguid.h>
#include <shlobj.h>
#include <string>
#include <thread>
#include <vector>
#include <winhttp.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winhttp.lib")