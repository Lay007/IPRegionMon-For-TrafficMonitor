# Concurrency and module lifetime tests

From the repository root:

    .\tests\concurrency\Run-Tests.ps1

Requires MSVC x64/x86 and a Windows SDK. All artifacts are written to a new
directory under TEMP. No installed plugin, user configuration or TrafficMonitor
process is used.

The module test uses the same ModuleTask.h helper as the plugin. It starts two
blocked workers, releases the host's LoadLibrary reference, checks that the
module remains loaded, then releases the workers one at a time. The last worker
throws; its stack and callable must still be destroyed before the DLL unloads.
101 load/unload cycles also check that the process handle count is unchanged.

## Module lifetime contract

ModuleTask acquires its own checked GetModuleHandleEx reference before creating a
native thread. The callable and its captures are destroyed before
FreeLibraryAndExitThread releases that reference and exits the worker. This uses
the dynamically linked CRT (/MD or /MDd), which performs per-thread cleanup on
native thread exit. There is no join, wait or I/O from a DLL destructor.

An ordinary FreeLibrary request therefore defers unloading until existing tasks
finish. It does not cancel an in-flight request or bound its total duration.
The host must stop issuing plugin calls before releasing its own reference.
Pointers returned by the plugin cannot survive actual module unloading.
Forced process termination is not a graceful task-shutdown mechanism.

The installed MSVC std::thread implementation also calls _beginthreadex, and the
inspected UCRT implementation already retains the procedure's module. The
explicit helper avoids depending on that implementation detail or using detach
without a locally checked lifetime guarantee.
