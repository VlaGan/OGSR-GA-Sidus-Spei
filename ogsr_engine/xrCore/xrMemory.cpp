#include "stdafx.h"
#include <psapi.h>
#include "xrsharedmem.h"

#ifndef _DEBUG
#define USE_MIMALLOC
#endif

#ifdef USE_MIMALLOC
#include "..\mimalloc\include\mimalloc-override.h"
#ifdef XRCORE_STATIC
// xrSimpodin: перегрузка операторов new/delete будет действовать только внутри того модуля движка, в котором они перегружены.
// Если движок разбит на модули и операторы перегружены в xrCore.dll, то в других модулях будут использоваться стандартные операторы,
// и если создать объект через new в xrCore, а delete сделать в xrGame - будет ошибка, т.к. объект создали кастомным аллокатором, а удалить пытаемся системным.
// Здесь два варианта решения проблемы: или перегружать операторы в каждом модуле, что не очень рационально,
// или перегружать их только в случае, если движок собирается в один exe файл. Второй вариант мне кажется более рациональным.
#include "..\mimalloc\include\mimalloc-new-delete.h"
#endif
#pragma comment(lib, "mimalloc-static")
#endif

#ifdef USE_MEMORY_VALIDATOR
#include "xrMemoryDebug.h"
#endif

xrMemory Memory;

void xrMemory::_initialize()
{
    stat_calls = 0;

    g_pStringContainer = xr_new<str_container>();
    g_pSharedMemoryContainer = xr_new<smem_container>();
}

void xrMemory::_destroy()
{
    xr_delete(g_pSharedMemoryContainer);
    xr_delete(g_pStringContainer);
}

void xrMemory::mem_compact()
{
    _heapmin(); //-V530
    HeapCompact(GetProcessHeap(), 0);
    if (g_pStringContainer)
        g_pStringContainer->clean();
    if (g_pSharedMemoryContainer)
        g_pSharedMemoryContainer->clean();
}

void* xrMemory::mem_alloc(size_t size)
{
    stat_calls++;

    void* ptr = malloc(size);
#ifdef USE_MEMORY_VALIDATOR
    RegisterPointer(ptr);
#endif
    return ptr;
}

void xrMemory::mem_free(void* P)
{
    stat_calls++;

#ifdef USE_MEMORY_VALIDATOR
    UnregisterPointer(P);
#endif
    free(P);
}

void* xrMemory::mem_realloc(void* P, size_t size)
{
    stat_calls++;

#ifdef USE_MEMORY_VALIDATOR
    UnregisterPointer(P);
#endif
    void* ptr = realloc(P, size);
#ifdef USE_MEMORY_VALIDATOR
    RegisterPointer(ptr);
#endif
    return ptr;
}

void GetProcessMemInfo(SProcessMemInfo& minfo)
{
    std::memset(&minfo, 0, sizeof(SProcessMemInfo));

    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);

    minfo.TotalPhysicalMemory = mem.ullTotalPhys;
    minfo.FreePhysicalMemory = mem.ullAvailPhys;
    minfo.TotalVirtualMemory = mem.ullTotalVirtual;
    minfo.MemoryLoad = mem.dwMemoryLoad;

    PROCESS_MEMORY_COUNTERS pc;
    std::memset(&pc, 0, sizeof(PROCESS_MEMORY_COUNTERS));
    pc.cb = sizeof(pc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pc, sizeof(pc)))
    {
        minfo.PeakWorkingSetSize = pc.PeakWorkingSetSize;
        minfo.WorkingSetSize = pc.WorkingSetSize;
        minfo.PagefileUsage = pc.PagefileUsage;
        minfo.PeakPagefileUsage = pc.PeakPagefileUsage;
    }
}

size_t mem_usage_impl(u32* pBlocksUsed, u32* pBlocksFree)
{
    static bool no_memory_usage = !strstr(Core.Params, "-memory_usage");
    if (no_memory_usage)
        return 0;

    _HEAPINFO hinfo;
    int heapstatus;
    hinfo._pentry = nullptr;
    size_t total = 0;
    u32 blocks_free = 0;
    u32 blocks_used = 0;
    while ((heapstatus = _heapwalk(&hinfo)) == _HEAPOK)
    {
        if (hinfo._useflag == _USEDENTRY)
        {
            total += hinfo._size;
            blocks_used += 1;
        }
        else
        {
            blocks_free += 1;
        }
    }
    if (pBlocksFree)
        *pBlocksFree = 1024 * blocks_free;
    if (pBlocksUsed)
        *pBlocksUsed = 1024 * blocks_used;

    switch (heapstatus)
    {
    case _HEAPEMPTY: break;
    case _HEAPEND: break;
    case _HEAPBADPTR: Msg("!![%s] bad pointer to heap", __FUNCTION__); break;
    case _HEAPBADBEGIN: Msg("!![%s] bad start of heap", __FUNCTION__); break;
    case _HEAPBADNODE: Msg("!![%s] bad node in heap", __FUNCTION__); break;
    }
    return total;
}

u32 xrMemory::mem_usage(u32* pBlocksUsed, u32* pBlocksFree) { return u32(mem_usage_impl(pBlocksUsed, pBlocksFree)); }
