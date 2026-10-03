# Third-party dependencies

CampusPulse original source is under MIT. Dependencies and university materials retain their own licenses and rights.

- Qt 6.8.3: Qt Core, Widgets, Network, SQL and test tooling. Project reference: https://www.qt.io/licensing/ and https://doc.qt.io/qt-6/licensing.html. The local prototype dynamically links Qt. The full distribution and applicable license/source obligations must be completed before publishing release binaries; this local package is not a completed public release.
- Lexbor 2.5.0: HTML parser and CSS selectors, statically linked, Apache License 2.0. Source: https://github.com/lexbor/lexbor/tree/v2.5.0. Its LICENSE is copied into the local executable package.
- SQLite is provided by the Qt SQLite driver; record the driver's bundled version when preparing a public release.
- aqtinstall/Python and their dependencies are development-only installation/configuration tools; they are not required to run CampusPulse.exe.

Captured official webpage fixtures record source URLs and hashes in tests/fixtures/neepu/capture.json. No student-list attachments were downloaded. Website content is not relicensed by the project's MIT license.

- libical 3.0.20: ICS construction and serialization, statically linked under the project's MPL-2.0 licensing option. Source: https://github.com/libical/libical/tree/v3.0.20. Unmodified library source, complete license texts and a source archive location are recorded in the distribution. Pinned archive SHA-256: `7b48f3f67e241e5efa620cabfe04392eb666917a93e8dfc825f046346f7fa6e7`. The library is linked only in the adapter layer; the original CampusPulse application remains MIT.
- CampusPulse calendar/pulse icon and navigation images are original vector artwork in assets/ and use this repository's MIT license.
