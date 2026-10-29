# Localization and native dialog checks

From the repository root:

```powershell
.\tests\localization\Run-Tests.ps1
```

Requires MSVC x64, MFC and a Windows SDK. The runner builds the real Release|x64
plugin with test-only exports and a standalone host. Artifacts go to a new
ignored Bin/localization-* directory. A 30-second process watchdog catches a
modal dialog or deadlock. Deployment events are disabled. No installed
TrafficMonitor, user INI, clipboard or public network endpoint is used.

The fake ITrafficMonitor supplies EN, Simplified Chinese, Traditional Chinese and
Russian language IDs. Unsupported languages fall back to English; Chinese
variants reuse the shipped Simplified Chinese resources. The test thread uses
the opposite UI language to ensure that the host language selects this DLL's
resources explicitly. The real MFC options container and all three child pages
are created as hidden windows. Native captions and OK/Cancel texts are checked,
along with metadata, immutable resource-pointer lifetime and unchanged thread
UI language. OnInitialize without EI_CONFIG_DIR must not start network work.

Verify-Resources.ps1 checks that both string tables contain the same nonempty
IDs and printf arguments, and that English resources and C++ UI text have no
untranslated Chinese. Deliberate data literals are listed explicitly: persisted
default target names and existing provider/ISP normalization. They are not
translated based on interface language. The width sample is checked separately.

A loopback server returns Chinese geography to the real background worker. The
tooltip must use the selected UI language while preserving that geography.
The native-dialog check covers child captions, custom navigation/card titles,
list columns and round-tripping user-defined Chinese target names and URLs.
It does not change a user's clipboard, run external network requests or edit
the installed host configuration.

The production plugin uses FindResourceEx for its existing dialog and string
resources, not SetThreadUILanguage or a new UI framework. String cache entries
are keyed by language and resource ID and never replaced. Normal hosts select
the language once during startup; changing the host language requires the host's
normal restart. v7+ initialization binds language before the first configured
worker formats its tooltip. Older hosts without OnInitialize use English and
their normal DataRequired update path.
