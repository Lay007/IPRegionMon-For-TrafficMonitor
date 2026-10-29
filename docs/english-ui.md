# English UI support (Issue #2)

The plugin reuses its existing EN/ZH resources and MFC/GDI+ controls. OnInitialize
reads TrafficMonitor's GetLanguageId: Chinese language variants select the
existing Simplified Chinese resources; other languages select English. The
host thread's UI language is not changed. Hosts without OnInitialize use English.

UiLanguage.cpp explicitly loads this DLL's string tables and dialog templates.
DataManager caches resource strings by language and ID so returned pointers
remain stable. Runtime labels, option messages, status text and tooltips use
those resources. UI language is bound before the configured startup worker runs.
Changing the host language takes effect after the host's normal restart.

API language remains independent. Geography, provider data and user-defined
target names are displayed as supplied. Persisted default target names and ISP
normalization strings remain unchanged. The INI schema, item IDs, request
protocols, latency calculation and worker ownership are unchanged.

## Taskbar layout

GetItemValueSampleText returns the actual bounded display value instead of a
fixed example. TrafficMonitor measures that string using its own font and DPI.
The plugin retains the measured value per rendering thread until the next
measurement when drawing in the taskbar. This prevents a newly published longer
result from overflowing an earlier measurement. The main window reads current
data. TrafficMonitor 1.86's taskbar recalculates layout every second and measures
and paints on its UI thread; other host scheduling models need separate testing.

The region/latency format limits the region to 20 display units before appending
latency. Other item text has a 48-unit limit. Wide characters count as two units;
these limits control compactness, not pixel width. Long values use an ellipsis,
preserving UTF-16 pairs and avoiding a cut inside a combining accent. Raw results,
tooltips and copy actions keep full data. User-customized TrafficMonitor labels
are not overwritten and can still contain text saved in an earlier language.

## Validation

Run tests/localization/Run-Tests.ps1 for resource parity, native hidden EN/ZH
dialogs, language/data separation, startup order and taskbar GDI checks. See its
README for coverage and manual GUI checks. Run tests/concurrency/Run-Tests.ps1
and tests/abi/Run-AbiTest.ps1 separately. Test-only exports are excluded from
ordinary builds.

Build Release|x64 with the project's v145 toolset, MFC/ATL and Windows SDK.
Use /p:PostBuildEventUseInBuild=false when building a test artifact: the original
post-build event deploys to a TrafficMonitor plugins directory. Set separate
OutDir and IntDir for each test run. No working installation is required.

## Pull request scope

The localization branch starts at fix/development-baseline, commit b06453b.
Review its changes against that baseline. A PR directly against the current
upstream main would also include six earlier baseline fixes.

Localization uses the official interface from db54256, synchronized settings
and stable getter strings from 19812bd, and the options task lifetime from
b6e0fd0 (which uses module retention from 9c0507e). Those should be accepted in
the baseline first. Request deadlines (682b4df) and HTTPS hardening (b06453b)
are independent security fixes; they do not belong in the localization diff.
Prefer a separate baseline PR followed by this localization PR, or a stacked
PR targeting the reviewed baseline. No history rewrite or publication is
required to prepare these local changes.
