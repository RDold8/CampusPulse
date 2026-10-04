# Third-party dependencies

CampusPulse original source is under MIT. Dependencies and university materials retain their own licenses and rights.

- Qt Base 6.8.3: dynamically linked Qt Core, GUI, Widgets, Network, SQL and plugins, under LGPL-3.0 with their component/third-party terms. Licensing: https://doc.qt.io/qt-6.8/licensing.html. Windows release packages include complete license texts, third-party attribution, Qt SPDX and dependency source locations in `licenses/`. Qt Test is development-only. Users may replace compatible Qt DLLs and debug/reverse-engineer the application to modify the library; no activation or signature enforcement prevents replacement.
- Lexbor 2.5.0: HTML parser and CSS selectors, statically linked, Apache License 2.0. Source: https://github.com/lexbor/lexbor/tree/v2.5.0. Both LICENSE and NOTICE are included in `licenses/lexbor`.
- SQLite 3.49.1: provided by Qt's QSQLITE plugin, public domain. Version established from the matching Qt SDK SPDX and release driver. Component attribution is included with Qt notices; upstream: https://www.sqlite.org/copyright.html.
- aqtinstall/Python and their dependencies are development-only installation/configuration tools; they are not required to run CampusPulse.exe.

Captured official webpage fixtures record source URLs and hashes in tests/fixtures/neepu/capture.json. No student-list attachments were downloaded. Website content is not relicensed by the project's MIT license.

- libical 3.0.20: ICS construction and serialization, statically linked under the project's MPL-2.0 licensing option. Source: https://github.com/libical/libical/tree/v3.0.20. Unmodified library source, complete license texts and a source archive location are recorded in the distribution. Pinned archive SHA-256: `7b48f3f67e241e5efa620cabfe04392eb666917a93e8dfc825f046346f7fa6e7`. The library is linked only in the adapter layer; the original CampusPulse application remains MIT.
- CampusPulse calendar/pulse icon and navigation images are original vector artwork in assets/ and use this repository's MIT license.

- Microsoft Visual C++ Runtime 14.44.35211.0: unmodified x64 runtime DLLs deployed app-local from the licensed Visual Studio 2022 `VC/Redist` CRT directory (directory version 14.44.35112). Microsoft runtime terms apply only to those DLLs. The original DOCX and full text are included under `licenses/Microsoft`; official terms: https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/. Distribution list: https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution.
- Inno Setup 7.1.0: build-time installer compiler, not an application runtime. Project: https://jrsoftware.org/ and source: https://github.com/jrsoftware/issrc. Its own embedded installer notices remain intact.

The `v0.1.0` Windows preview Release provides the unmodified corresponding Qt Base 6.8.3, Lexbor 2.5.0 and libical 3.0.20 source archives alongside the installer and portable package: https://github.com/RDold8/CampusPulse/releases/tag/v0.1.0. `licenses/dependency-sources.json` records SHA-256 values and project-controlled download URLs. Qt Base source commit: `c07c2d5a527a644d36e7853d55132ae38921682f`, matching the shipped Qt SDK SBOM. Build and library-replacement information is in `docs/windows-release.md` and package `licenses/SOURCES.txt`.
