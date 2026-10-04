"""Prepare verified dependency notices and source assets for the Windows release."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tarfile
import xml.etree.ElementTree as ET
import zipfile


SOURCES = {
    "qtbase-everywhere-src-6.8.3.tar.xz": "56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80",
    "lexbor-2.5.0.zip": "460c6ee1396568078dc45b96dd55c9e92909deaff27cd6421c25dc8dfc8608b5",
    "libical-3.0.20.zip": "7b48f3f67e241e5efa620cabfe04392eb666917a93e8dfc825f046346f7fa6e7",
}

WEBVIEW2_SDK = {
    "version": "1.0.3537.50",
    "url": "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.3537.50/"
           "microsoft.web.webview2.1.0.3537.50.nupkg",
    "sha256": "5ea526bbd728adda0da4d31219267e96460494a427e4894c4e09d9f320f4b9aa",
    "license": "BSD-3-Clause",
    "linkage": "WebView2LoaderStatic.lib; browser runtime is installed separately",
}
# Notice bytes from the pinned NuGet archive, not from a machine's browser runtime.
WEBVIEW2_NOTICES = {
    "LICENSE.txt": "0af8f1b807512aae39c2ac1aa4d0cae65cabecb6fd554b8439a5162a0d6eca55",
    "NOTICE.txt": "106423785c5b7eba0a8e61d1837f2132e9c828e20ad530f565d981c1df60dd90",
}


def copy_webview2_notices(root: Path, licenses: Path, sdk_dir: Path | None) -> None:
    candidates = [sdk_dir] if sdk_dir else [root / "build" / "_deps" / "webview2-src", root / ".deps" / "webview2"]
    selected = next((path for path in candidates if path and (path / "LICENSE.txt").is_file()), None)
    if selected is None:
        raise FileNotFoundError("Pinned WebView2 SDK notices not found; build first or use --webview2-sdk-dir")
    notices = {}
    for name, expected in WEBVIEW2_NOTICES.items():
        content = (selected / name).read_bytes()
        if hashlib.sha256(content).hexdigest() != expected:
            raise ValueError(f"WebView2 SDK notice SHA-256 mismatch: {name}")
        notices[name] = content
    target = licenses / "WebView2-SDK"
    target.mkdir(parents=True, exist_ok=True)
    for name, content in notices.items():
        (target / name).write_bytes(content)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-dir", required=True, type=Path)
    parser.add_argument("--qt-root", required=True, type=Path)
    parser.add_argument("--source-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--version", default="0.1.2")
    parser.add_argument("--source-release-version", help="Published release containing unchanged dependency sources")
    parser.add_argument("--webview2-sdk-dir", type=Path, help="Extracted pinned SDK, for a custom build directory")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    licenses = args.package_dir / "licenses"
    qt_licenses = licenses / "QtBase"
    qt_licenses.mkdir(parents=True, exist_ok=True)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    assets = []
    for name, expected in SOURCES.items():
        source = args.source_dir / name
        actual = hashlib.sha256(source.read_bytes()).hexdigest()
        if actual != expected:
            raise ValueError(f"Dependency source SHA-256 mismatch: {name}")
        shutil.copy2(source, args.output_dir / name)
        assets.append({"name": name, "sha256": actual})

    # Extract notice files explicitly; never extract archive paths onto the filesystem.
    with tarfile.open(args.source_dir / "qtbase-everywhere-src-6.8.3.tar.xz") as archive:
        for entry in archive.getmembers():
            if not entry.isfile():
                continue
            relative = Path(*Path(entry.name).parts[1:])
            if relative.parts[0] == "LICENSES" or (
                "3rdparty" in relative.parts
                and (relative.name.upper().startswith(("LICENSE", "COPYING", "NOTICE", "COPYRIGHT"))
                     or relative.suffix == ".json")
            ):
                if any(part in {"..", "."} for part in relative.parts):
                    raise ValueError("Unsafe Qt archive path")
                destination = qt_licenses / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                stream = archive.extractfile(entry)
                assert stream is not None
                destination.write_bytes(stream.read())

    sbom_dir = licenses / "sbom"
    sbom_dir.mkdir(exist_ok=True)
    for path in (args.qt_root / "sbom").glob("qtbase-6.8.3.*"):
        shutil.copy2(path, sbom_dir / path.name)
    sbom = json.loads((sbom_dir / "qtbase-6.8.3.spdx.json").read_text(encoding="utf-8"))
    extracted = sbom.get("hasExtractedLicensingInfos", [])
    (qt_licenses / "EXTRACTED-LICENSES.txt").write_text(
        "\n\n".join(f"{item['licenseId']}\n{item['extractedText']}" for item in extracted), encoding="utf-8"
    )
    for library in ("lexbor", "libical"):
        target = licenses / library
        target.mkdir(exist_ok=True)
        for path in (root / "build" / "_deps" / f"{library}-src").iterdir():
            if path.is_file() and path.name.upper().startswith(("LICENSE", "COPYING", "NOTICE")):
                shutil.copy2(path, target / path.name)

    copy_webview2_notices(root, licenses, args.webview2_sdk_dir)

    microsoft = licenses / "Microsoft"
    microsoft.mkdir(exist_ok=True)
    runtime_doc = args.source_dir / "Visual-C-Runtime-2015-2022-License-1.docx"
    shutil.copy2(runtime_doc, microsoft / runtime_doc.name)
    with zipfile.ZipFile(runtime_doc) as archive:
        xml = ET.fromstring(archive.read("word/document.xml"))
    ns = {"w": "http://schemas.openxmlformats.org/wordprocessingml/2006/main"}
    runtime_terms = "\n".join("".join(p.itertext()) for p in xml.findall(".//w:p", ns))
    (microsoft / "VC-RUNTIME-LICENSE.txt").write_text(runtime_terms, encoding="utf-8")
    (licenses / "INSTALLATION-LICENSES.txt").write_text(
        f"CampusPulse {args.version} - Component licenses\n\n"
        "CampusPulse original work is MIT licensed. Dependency terms apply only to their respective components.\n"
        "Qt Base is dynamically linked under LGPL-3.0; its notices and GPL/LGPL texts are in licenses/QtBase.\n"
        "You may replace Qt DLLs with compatible modified builds and debug/reverse-engineer the application\n"
        "for that purpose. There is no signature or activation check preventing this.\n"
        "Lexbor: Apache-2.0. libical: MPL-2.0. SQLite: public domain.\n"
        "WebView2 SDK 1.0.3537.50: BSD-3-Clause; loader and third-party notices are in licenses/WebView2-SDK.\n"
        "The Microsoft Edge WebView2 Evergreen Runtime is installed separately and is not included in this package.\n"
        "Microsoft's terms below apply only to the Microsoft Visual C++ Runtime DLLs, not CampusPulse or Qt.\n\n"
        + (root / "LICENSE").read_text(encoding="utf-8") + "\n\n" + runtime_terms,
        encoding="utf-8",
    )
    source_release = args.source_release_version or args.version
    url = f"https://github.com/RDold8/CampusPulse/releases/download/v{source_release}/"
    manifest = {
        "version": args.version,
        "source_release_version": source_release,
        "qt_version": "6.8.3",
        "qt_source_commit": "c07c2d5a527a644d36e7853d55132ae38921682f",
        "sqlite_version": "3.49.1",
        "sources": [{**asset, "url": url + asset["name"]} for asset in assets],
        "webview2_sdk": WEBVIEW2_SDK,
        "webview2_runtime": {
            "deployment": "Microsoft Edge WebView2 Evergreen Runtime; installed separately, not bundled",
            "distribution_documentation": "https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution",
            "download_url": "https://developer.microsoft.com/en-us/microsoft-edge/webview2/",
        },
        "source_changes": "No Qt, Lexbor or libical modifications",
    }
    (licenses / "dependency-sources.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (licenses / "SOURCES.txt").write_text(
        "Corresponding dependency sources are available at no charge from the same CampusPulse release.\n"
        f"https://github.com/RDold8/CampusPulse/releases/tag/v{source_release}\n\n"
        + "\n".join(f"{asset['name']}\nSHA256: {asset['sha256']}\n{url}{asset['name']}\n" for asset in assets)
        + f"\nWebView2 SDK {WEBVIEW2_SDK['version']} (unmodified static loader, BSD-3-Clause)\n"
        f"NuGet SHA256: {WEBVIEW2_SDK['sha256']}\n{WEBVIEW2_SDK['url']}\n"
        "The separately installed WebView2 Evergreen Runtime is not redistributed by this release.\n"
        + "\nQt builds with CMake and the x64 MSVC toolchain. Keep the Qt 6.8 ABI when replacing DLLs.\n"
        "Rebuild CampusPulse using the public CMake source when changing ABI/toolchains.\n"
        "Build instructions: https://github.com/RDold8/CampusPulse/blob/main/docs/windows-release.md\n",
        encoding="utf-8",
    )
    print(json.dumps({"dependency_sources": len(assets), "notice_files": len(list(licenses.rglob('*')))}, indent=2))


if __name__ == "__main__":
    main()
