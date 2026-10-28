# Network deadlines and lifecycle regression tests

Run from the repository root with MSVC x64, MFC and the Windows SDK installed:

```powershell
.\tests\network\Run-Tests.ps1
```

The script discovers Visual Studio with vswhere, builds a standalone loopback
host and the real Release|x64 plugin with test-only exports, then runs a
90-second watchdog. Artifacts and logs go to a new ignored Bin/network-* folder.
No installed TrafficMonitor, user INI, certificate store or system network
configuration is changed. Deployment post-build events are explicitly disabled.

## Reproduction and expectations

The server advertises a large body and sends one byte every 50 ms for 6.5 seconds.
Before the fix, the actual GET returned success after 6547 ms with an incomplete
116-byte body. The initial failing run is preserved locally in
Bin/network-7d0d6f6b2deb452790bfe2200d7a8d7d/test.log. `-ReproOnly` runs just
this case, using the same assertions; it should pass on the corrected code.

GET has one 4000 ms budget; HEAD has one 1200 ms budget. Tests require failures
near those deadlines for a slow body, slow headers and a silent server, and
verify that the server sees the connection close. They also check ordinary
GET/HEAD success, refused loopback connections and rejection of an incomplete
Content-Length body with no partial output. Timing tolerances allow scheduling
and cleanup overhead, not a second full network timeout.

Five cycles release the host's DLL reference during an active slow GET, check
that the module stays loaded, then require cancellation and eventual unloading.
A test-only EOF callback gate proves that the worker and DLL cannot finish
before the final callback returns. Three separate EXE children return normally
while an actual plugin request is active; the parent checks exit code 0 and
socket closure. These children are isolated hosts, not the TrafficMonitor GUI.
They do not demonstrate graceful worker completion during ExitProcess.

## Transport contract

BoundedHttp.h is shared by the real GET and HEAD paths. It uses asynchronous
WinHTTP internally, while the existing ModuleTask worker serially waits for
completion notifications. It starts no application-owned timer or helper thread.
All operations consume the same monotonic deadline, including DNS/connect,
headers, redirects and body transfer. Deadline expiry closes the request only
after the initiating WinHTTP call has returned. Callback code only publishes
completion; it never issues another request operation.

Cleanup closes request, connection and session, then waits for the session's
WINHTTP_OPTION_UNLOAD_NOTIFY_EVENT. Unlike HANDLE_CLOSING, this signals after
the last callback has returned, so ModuleTask may safely release its DLL reference.
Buffers/events stay alive throughout cancellation. If that option cannot be
installed, the request fails before registering a callback. No cleanup is added
to DllMain or static destructors, and there is no detach or forced worker kill.

The deadline bounds network work; OS scheduling and callback cancellation/drain
can add latency. Drain deliberately waits rather than freeing live callback
state on a second timeout. This is not a hard real-time guarantee against an OS
or WinHTTP teardown failure. Each request has its own budget: a whole update may
take longer because fallback providers and configured latency targets are
processed sequentially. FreeLibrary defers unloading rather than signalling
cancellation of the entire update. A fixed 8 KiB buffer and 1 MiB body ceiling
bound allocations for an individual API response. HTTP status errors, read errors,
truncation and timeouts produce no usable partial body.

References:

- [WinHTTP concurrency and cancellation](https://learn.microsoft.com/en-us/windows/win32/winhttp/concurrency-in-winhttp)
- [Closing async request handles](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle)
- [Session unload notification](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/WinHttp/option-flags.md#winhttp_option_unload_notify_event)
