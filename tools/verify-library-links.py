#!/usr/bin/env python3
"""Developer-only, explicitly invoked audit of NEEPU's 21 library/reader resources.

This does not change ResourceDiscovery defaults. It reads public HTTPS HTML/text only;
it never submits login forms, requests full text, or downloads installers/binaries.
DNS preflight plus redirect checks are not complete DNS-rebinding protection: requests
performs its own connection-time resolution, so the checked address is not pinned.
"""

from __future__ import annotations

import argparse
import collections
import copy
import hashlib
import html
from html.parser import HTMLParser
import ipaddress
import json
from pathlib import Path
import re
import socket
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
from urllib.parse import parse_qsl, quote, urljoin, urlsplit, urlunsplit

import requests
from urllib3.exceptions import HTTPError as UrlLibHttpError


MAX_BYTES = 2 * 1024 * 1024
MAX_REDIRECTS = 3
URL_BUDGET = 28.0
RUN_BUDGET = 240.0
HOST_INTERVAL = 3.0
WORKERS = 3
SCHOOL_ROOT = "neepu.edu.cn"
LIBRARY_HOME = "https://lib.neepu.edu.cn/"
READER_URL = "https://lib.neepu.edu.cn/info/1141/1662.htm"
CARSI_SOURCE = "https://lib.neepu.edu.cn/info/1211/8193.htm"
FRIENDS = {
    "library.ncepu.edu.cn": "华北电力大学图书馆",
    "tsg.jlmu.cn": "吉林医药学院图书馆",
    "lib.ccut.edu.cn": "长春工业大学图书馆",
}
HOSTNAME = re.compile(
    r"(?=.{1,253}\Z)(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+"
    r"[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?\Z", re.I
)
BINARY_SUFFIX = re.compile(
    r"\.(?:pdf|docx?|xlsx?|pptx?|zip|rar|7z|exe|msi|dmg|pkg|apk|mp[34]|png|jpe?g|gif)\Z", re.I
)
SENSITIVE_QUERY = {
    "access_token", "id_token", "password", "passwd", "authorization", "api_key",
    "apikey", "token", "ticket", "samlrequest", "samlresponse", "code", "sessionid",
}
DNS_PROGRAM = (
    "import json,socket,sys; "
    "print(json.dumps(sorted({x[4][0] for x in "
    "socket.getaddrinfo(sys.argv[1],443,type=socket.SOCK_STREAM)})))"
)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical(value: str) -> str:
    """Selected ASCII URLs: remove fragments/default '/', preserving encoded query values."""
    parts = urlsplit(html.unescape(value).strip())
    path = quote(parts.path or "/", safe="/%:@!$&'()*+,;=-._~")
    query = quote(parts.query, safe="/%?:@!$&'()*+,;=-._~")
    return urlunsplit((parts.scheme.lower(), parts.netloc.lower(), path, query, ""))


def registered_domain(host: str) -> str:
    # Offline rule for the finite selected hosts, including China's public edu.cn suffix.
    labels = host.lower().split(".")
    count = 3 if host.endswith((".edu.cn", ".ac.cn", ".com.cn", ".org.cn", ".net.cn")) else 2
    return ".".join(labels[-count:])


def official(value: str) -> bool:
    host = urlsplit(value).hostname or ""
    return host == SCHOOL_ROOT or host.endswith("." + SCHOOL_ROOT)


class SafetyError(ValueError):
    pass


def safe_https(value: str) -> str:
    if any(ord(c) <= 32 or ord(c) == 127 or c == "\\" for c in value):
        raise SafetyError("URL contains whitespace/control/backslash")
    parts = urlsplit(value)
    host = parts.hostname or ""
    if (parts.scheme.lower() != "https" or parts.username is not None
            or parts.password is not None or "@" in parts.netloc or ":" in parts.netloc
            or not HOSTNAME.fullmatch(host)
            or host.endswith((".local", ".localhost", ".internal"))):
        raise SafetyError("Only HTTPS hostnames without userinfo, ports or IP literals are allowed")
    try:
        ipaddress.ip_address(host)
    except ValueError:
        pass
    else:
        raise SafetyError("IP literal is forbidden")
    if BINARY_SUFFIX.search(parts.path):
        raise SafetyError("Binary/download file URL is outside the public-page audit")
    if any(key.lower() in SENSITIVE_QUERY for key, _ in parse_qsl(parts.query)):
        raise SafetyError("Authentication/credential query is outside the public-page audit")
    return host.lower()


def resolve_public(host: str, remaining: float) -> list[str]:
    # A short-lived DNS helper makes the otherwise unbounded Windows resolver time-bounded.
    result = subprocess.run(
        [sys.executable, "-c", DNS_PROGRAM, host], capture_output=True, text=True,
        timeout=min(3.0, remaining), check=False, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    if result.returncode:
        raise SafetyError("DNS lookup failed")
    addresses = json.loads(result.stdout)
    if not addresses or any(not ipaddress.ip_address(addr).is_global for addr in addresses):
        raise SafetyError("DNS contains a non-public address: " + ", ".join(addresses))
    return addresses


class PublicPage(HTMLParser):
    def __init__(self, markup: str):
        super().__init__(convert_charrefs=True)
        self.text: list[str] = []
        self.title: list[str] = []
        self.headings: list[str] = []
        self.anchors: list[dict] = []
        self._anchor: dict | None = None
        self._hidden = 0
        self._title = False
        self._heading = False
        self.password_form = False
        self.feed(markup)
        self.visible = " ".join(" ".join(self.text).split())
        self.page_title = " ".join(" ".join(self.title).split())

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = dict(attrs)
        if tag in {"script", "style", "noscript", "head"}:
            self._hidden += 1
        if tag == "title":
            self._title = True
        if tag in {"h1", "h2", "h3"}:
            self._heading = True
        if tag == "input" and (values.get("type") or "").lower() == "password":
            self.password_form = True
        if tag == "a" and values.get("href"):
            self._anchor = {"href": values["href"], "label": []}

    def handle_endtag(self, tag: str) -> None:
        if tag in {"script", "style", "noscript", "head"}:
            self._hidden = max(0, self._hidden - 1)
        if tag == "title":
            self._title = False
        if tag in {"h1", "h2", "h3"}:
            self._heading = False
        if tag == "a" and self._anchor:
            self._anchor["label"] = " ".join(" ".join(self._anchor["label"]).split())
            self.anchors.append(self._anchor)
            self._anchor = None

    def handle_data(self, data: str) -> None:
        if self._title:
            self.title.append(data)
        if not self._hidden:
            self.text.append(data)
            if self._heading:
                self.headings.append(data.strip())
        if self._anchor and not self._hidden:
            self._anchor["label"].append(data)


def decode_html(raw: bytes, content_type: str = "") -> tuple[str, str]:
    candidates = []
    charset = re.search(r"charset\s*=\s*[\"']?([a-z0-9_-]+)", content_type, re.I)
    if charset:
        candidates.append(charset.group(1))
    meta = re.search(br"charset\s*=\s*[\"']?([a-z0-9_-]+)", raw[:8192], re.I)
    if meta:
        candidates.append(meta.group(1).decode("ascii"))
    candidates.extend(["utf-8-sig", "gb18030"])
    for encoding in dict.fromkeys(candidates):
        try:
            return raw.decode(encoding), encoding
        except (UnicodeDecodeError, LookupError):
            pass
    return raw.decode("utf-8", errors="replace"), "utf-8 with replacement"


def classify(page: PublicPage, markup: str, truncated: bool) -> tuple[str, str]:
    visible = page.visible
    if re.search(r"access denied|just a moment|verify.{0,30}(?:human|browser)|"
                 r"pardon our interruption|captcha|访问被拒绝", visible, re.I):
        return "unreachable", "bot/access restriction; this audit did not pass"
    if page.password_form or re.search(r"统一身份认证|账号登录|用户登录|^sign in$|^log in$",
                                      page.page_title, re.I):
        return "login_required", "public login form/title; no form was submitted"
    if truncated:
        return "discovered", "2 MiB text cap reached; complete page was not verified"
    js_prompt = re.search(
        r"(?:enable|requires?|turn on|disabled|activated).{0,70}javascript|"
        r"javascript.{0,70}(?:disabled|enable|required|download)|"
        r"(?:启用|开启|需要|支持).{0,30}javascript", visible, re.I,
    )
    if js_prompt:
        return "discovered", "JavaScript-only/download prompt; usable content was not verified"
    if len(visible) < 80:
        reason = "empty/too little readable body"
        if re.search(r"<script|<iframe|正在加载|loading", markup, re.I):
            reason = "JavaScript/iframe shell with too little readable body"
        return "discovered", reason + "; HTTP 200 does not establish usable access"
    return "verified", "readable public static page only; school entitlements/full text not tested"


class Auditor:
    def __init__(self, output: Path, allowed_hosts: set[str]):
        self.output = output
        self.allowed_hosts = allowed_hosts
        self.allowed_roots = {registered_domain(host) for host in allowed_hosts}
        self._guard = threading.Lock()
        self._locks: dict[str, threading.Lock] = {}
        self._next: dict[str, float] = {}
        self.deadline = time.monotonic() + RUN_BUDGET
        self.responses: dict[str, dict] = {}
        self.bodies: dict[str, bytes] = {}
        self.log = output / "run.log"
        self.log.write_text("Public HTTPS audit started " + utc_now() + "\n", encoding="utf-8")

    def wait_for_host(self, host: str, deadline: float) -> None:
        with self._guard:
            lock = self._locks.setdefault(host, threading.Lock())
        with lock:
            delay = max(0.0, self._next.get(host, 0.0) - time.monotonic())
            if time.monotonic() + delay >= deadline:
                raise TimeoutError("audit/URL time budget exhausted before host interval")
            if delay:
                time.sleep(delay)
            self._next[host] = time.monotonic() + HOST_INTERVAL

    def fetch(self, requested: str) -> tuple[dict, bytes]:
        started = time.monotonic()
        deadline = min(started + URL_BUDGET, self.deadline)
        current = requested
        meta = {
            "requested_url": requested, "final_url": requested, "checked_at": utc_now(),
            "http_status": None, "redirects": [], "access_status": "unreachable",
            "check_result": "not checked", "error": "", "raw_file": None,
            "raw_sha256": None, "body_bytes": 0, "truncated": False,
            "content_type": "", "model_calls": 0, "login_performed": False,
            "fulltext_requested": False, "binary_downloaded": False,
        }
        raw = b""
        session = requests.Session()
        session.trust_env = False  # No proxies, environment credentials or .netrc.
        session.headers.update({
            "User-Agent": "CampusPulse-PublicLinkAudit/1.0 (public HTML only)",
            "Accept": "text/html,application/xhtml+xml,text/plain;q=0.8",
        })
        try:
            initial_host = safe_https(requested)
            if initial_host not in self.allowed_hosts:
                raise SafetyError("Initial hostname is not in the finite selected-resource allowlist")
            for hop in range(MAX_REDIRECTS + 1):
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("audit/URL time budget exhausted")
                host = safe_https(current)
                if registered_domain(host) != registered_domain(initial_host):
                    raise SafetyError("Redirect leaves the initial registered domain")
                resolve_public(host, remaining)
                self.wait_for_host(host, deadline)
                remaining = deadline - time.monotonic()
                session.cookies.clear()
                with session.get(current, stream=True, allow_redirects=False,
                                 timeout=(min(5.0, remaining), min(8.0, remaining))) as response:
                    meta["http_status"] = response.status_code
                    meta["final_url"] = current
                    media = response.headers.get("Content-Type", "").split(";", 1)[0].lower().strip()
                    content_type = response.headers.get("Content-Type", "")
                    meta["content_type"] = media
                    if response.status_code in {301, 302, 303, 307, 308}:
                        destination = urljoin(current, response.headers.get("Location", ""))
                        authentication = bool(re.search(r"login|shibboleth|sso|oauth|saml|authenticate",
                                                        current + " " + destination, re.I))
                        parts = urlsplit(destination)
                        protected_query = any(key.lower() in SENSITIVE_QUERY
                                              for key, _ in parse_qsl(parts.query))
                        if protected_query:
                            # Never persist the transient SAML/auth query and never follow it.
                            redacted = urlunsplit((parts.scheme, parts.netloc, parts.path, "", ""))
                            safe_https(redacted)
                            meta["access_status"] = "login_required" if authentication else "unreachable"
                            meta["check_result"] = "authentication/credential redirect not followed; no login submitted"
                            meta["redirects"].append({
                                "from": current, "to": redacted, "http_status": response.status_code,
                                "followed": False, "query_redacted": True,
                                "same_registered_domain": registered_domain(parts.hostname or "")
                                == registered_domain(initial_host),
                            })
                            break
                        target_host = safe_https(destination)
                        same_root = registered_domain(target_host) == registered_domain(initial_host)
                        if not same_root:
                            meta["access_status"] = "login_required" if authentication else "unreachable"
                            meta["check_result"] = (
                                "authentication redirect not followed; no login submitted" if authentication
                                else "redirect outside initial registered domain was not followed"
                            )
                            meta["redirects"].append({
                                "from": current, "to": destination, "http_status": response.status_code,
                                "followed": False, "same_registered_domain": False,
                            })
                            break
                        meta["redirects"].append({
                            "from": current, "to": destination, "http_status": response.status_code,
                            "followed": hop < MAX_REDIRECTS, "same_registered_domain": True,
                        })
                        if hop == MAX_REDIRECTS:
                            raise SafetyError("Maximum of three redirects reached")
                        current = canonical(destination)
                        continue
                    if media not in {"text/html", "application/xhtml+xml", "text/plain"}:
                        meta["check_result"] = "non-HTML/text content type refused before reading body"
                        break
                    chunks = []
                    size = 0
                    while size <= MAX_BYTES:
                        if time.monotonic() >= deadline:
                            raise TimeoutError("overall URL body time budget exhausted")
                        # read1 returns after one socket read, so a trickling body cannot fill a large
                        # iter_content chunk indefinitely. Each underlying read still has the 8 s cap.
                        chunk = response.raw.read1(min(8192, MAX_BYTES + 1 - size), decode_content=True)
                        if not chunk:
                            break
                        chunks.append(chunk)
                        size += len(chunk)
                    raw = b"".join(chunks)
                    if len(raw) > MAX_BYTES:
                        raw = raw[:MAX_BYTES]
                        meta["truncated"] = True
                    markup, encoding = decode_html(raw, content_type)
                    page = PublicPage(markup)
                    meta["text_encoding"] = encoding
                    meta["page_title"] = page.page_title
                    meta["visible_text_chars"] = len(page.visible)
                    if response.status_code == 200:
                        meta["access_status"], meta["check_result"] = classify(page, markup, meta["truncated"])
                    elif response.status_code in {401, 407}:
                        meta["access_status"] = "login_required"
                        meta["check_result"] = "HTTP authentication required; no credentials supplied"
                    else:
                        meta["check_result"] = f"HTTP {response.status_code}; public-page audit did not pass"
                    break
        except (requests.RequestException, UrlLibHttpError, SafetyError, TimeoutError, subprocess.TimeoutExpired,
                socket.timeout, ValueError, OSError) as exc:
            meta["error"] = type(exc).__name__
            if isinstance(exc, SafetyError):
                meta["error_detail"] = str(exc)
            meta["check_result"] = (
                "timeout; this public-page audit did not pass" if isinstance(
                    exc, (requests.Timeout, TimeoutError, subprocess.TimeoutExpired, socket.timeout)
                ) else "public URL/DNS/TLS/redirect check failed; this audit did not pass"
            )
            meta["access_status"] = "unreachable"
        finally:
            session.cookies.clear()
            session.close()
        if raw:
            basename = digest(requested.encode("utf-8"))
            suffix = ".txt" if meta["content_type"] == "text/plain" else ".html"
            raw_path = self.output / "rawresponses" / (basename + suffix)
            raw_path.write_bytes(raw)
            meta["raw_file"] = str(raw_path.resolve())
            meta["raw_sha256"] = digest(raw)
            meta["body_bytes"] = len(raw)
        meta["elapsed_ms"] = round((time.monotonic() - started) * 1000)
        meta["note"] = "unreachable means this audit failed, not that a user's browser cannot use the link"
        meta_path = self.output / "rawresponses" / (digest(requested.encode("utf-8")) + ".json")
        meta_path.write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        return meta, raw

    def fetch_many(self, urls: list[str]) -> None:
        pending = list(dict.fromkeys(canonical(url) for url in urls if canonical(url) not in self.responses))
        with ThreadPoolExecutor(max_workers=WORKERS) as pool:
            futures = {pool.submit(self.fetch, url): url for url in pending}
            for future in as_completed(futures):
                url = futures[future]
                meta, raw = future.result()
                self.responses[url] = meta
                self.bodies[url] = raw
                line = f"{meta['checked_at']} {meta['access_status']} HTTP={meta['http_status']} {url}\n"
                with self.log.open("a", encoding="utf-8") as log:
                    log.write(line)
                print(line.rstrip(), flush=True)


def source_capture(auditor: Auditor, source_url: str, prior_dir: Path) -> tuple[PublicPage | None, dict]:
    key = canonical(source_url)
    raw = auditor.bodies.get(key, b"")
    meta = auditor.responses.get(key, {})
    if raw and meta.get("http_status") == 200 and not meta.get("truncated"):
        markup, _ = decode_html(raw)
        return PublicPage(markup), {
            "kind": "this_script_public_response", "raw_file": meta["raw_file"],
            "sha256": meta["raw_sha256"], "checked_at": meta["checked_at"],
        }
    for path in sorted(prior_dir.glob("*.json")):
        prior = json.loads(path.read_text(encoding="utf-8"))
        if canonical(prior.get("requested_url", "")) != key or prior.get("error"):
            continue
        filename = prior.get("file", "")
        if not re.fullmatch(r"[a-f0-9]{64}\.html", filename):
            continue
        file = prior_dir / filename
        if not file.is_file() or file.stat().st_size > MAX_BYTES:
            continue
        raw = file.read_bytes()
        if digest(raw) != prior.get("sha256"):
            continue
        markup, _ = decode_html(raw)
        return PublicPage(markup), {
            "kind": "prior_public_capture_not_this_script_success", "raw_file": str(file.resolve()),
            "sha256": digest(raw), "checked_at": prior.get("checked_at"),
            "metadata_file": str(path.resolve()),
        }
    return None, {"kind": "unavailable", "script_status": meta.get("access_status")}


def refreshed_reader_note(selected: list[dict]) -> str:
    labels = {"cajviewer.cnki.net": "CAJViewer", "get.adobe.com": "Adobe Reader",
              "www.sslibrary.com": "SSreader"}
    status_text = {"verified": "公开HTML页可读，未下载", "login_required": "显示需登录，本次未登录",
                   "discovered": "静态内容尚未验证", "unreachable": "本次检查未通过"}
    observations = [labels[urlsplit(row["url"]).hostname] + "：" + status_text[row["status"]]
                    for row in selected if urlsplit(row["url"]).hostname in labels]
    return ("学校2023-11-10阅读器说明列出CAJViewer、Adobe Reader和SSreader官方入口。"
            "本次脚本的公开入口检查：" + "；".join(observations) + "。"
            "HTTP链接只按同一主机、路径和参数升级HTTPS；未下载或安装软件。"
            "检查未通过不代表用户浏览器一定不可用，也不证明软件兼容性。")


def validate_importable_metadata(resources: list[dict]) -> None:
    identities = set()
    for row in resources:
        identity = (row["school_id"], row["id"])
        if identity in identities:
            raise ValueError("Duplicate school resource identity in merged report")
        identities.add(identity)
        note, evidence = row["access_note"], row["access_evidence"]
        if bool(note) != bool(evidence) or "\0" in note or "\0" in evidence:
            raise ValueError("Merged access note/evidence are not valid paired text")
        if len(note.encode("utf-8")) > 12000 or len(evidence.encode("utf-8")) > 8192:
            raise ValueError("Merged access note/evidence exceeds application byte limits")
        if evidence:
            safe_https(evidence)
            if not official(evidence):
                raise ValueError("Merged access evidence is not an official school HTTPS URL")


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--annotations", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    project = Path(__file__).resolve().parent.parent
    output = args.output.resolve()
    if not output.is_relative_to(project / "evidence" / "library-access"):
        parser.error("--output must stay within this project's evidence/library-access directory")
    report_bytes = args.report.read_bytes()
    annotation_bytes = args.annotations.read_bytes()
    report = json.loads(report_bytes)
    annotations = json.loads(annotation_bytes)
    resources = copy.deepcopy(report["resources"])
    if len(resources) != 90 or len(annotations) != 12:
        parser.error("This fixed audit expects the existing 90-resource report and 12 reviewed annotations")
    selected = [row for row in resources if row["category"] == "library" or canonical(row["url"]) == READER_URL]
    if len(selected) != 21 or any(row["school_id"] != "cn-neepu" for row in selected):
        parser.error("This fixed audit expects exactly 20 library resources plus the NEEPU reader page")
    for row in resources:
        row.setdefault("access_note", "")
        row.setdefault("access_evidence", "")
    for item in annotations:
        if not official(item["evidence_url"]):
            parser.error("Annotation evidence must be a school official HTTPS page")
        safe_https(item["evidence_url"])
        if (not item["note"].strip() or len(item["note"].encode("utf-8")) > 12000
                or len(item["evidence_url"].encode("utf-8")) > 8192):
            parser.error("Annotation lengths/paired fields are invalid")
    allowed_hosts = {safe_https(row["url"]) for row in selected}
    allowed_hosts.update(safe_https(item["evidence_url"]) for item in annotations)
    allowed_hosts.update({"cajviewer.cnki.net", "get.adobe.com", "www.sslibrary.com", "ds.carsi.edu.cn"})
    output.mkdir(parents=True, exist_ok=True)
    (output / "rawresponses").mkdir(exist_ok=True)
    auditor = Auditor(output, allowed_hosts)
    sources = [READER_URL, CARSI_SOURCE, LIBRARY_HOME] + [item["evidence_url"] for item in annotations]
    auditor.fetch_many(sources)
    prior_dir = project / "evidence" / "stage5-neepu-resources" / "cn-neepu"
    reader, reader_proof = source_capture(auditor, READER_URL, prior_dir)
    carsi, carsi_proof = source_capture(auditor, CARSI_SOURCE, prior_dir)
    home, home_proof = source_capture(auditor, LIBRARY_HOME, prior_dir)
    additions = []
    extraction = []
    warnings = []

    def add_link(title: str, source: str, anchor: dict, note: str, proof: dict,
                 annotation_match: str = "") -> None:
        original = urljoin(source, anchor["href"])
        parts = urlsplit(original)
        # The only upgrade is HTTP -> HTTPS on the same host/path/query, recorded explicitly.
        target = canonical(urlunsplit(("https", parts.netloc, parts.path, parts.query, "")))
        safe_https(target)
        if any(canonical(row["url"]) == target for row in resources):
            return
        row = {
            "id": digest(("cn-neepu|" + target).encode("utf-8")), "school_id": "cn-neepu",
            "title": title, "url": target, "description": note, "category": "library",
            "provider": urlsplit(target).hostname, "discovered_from": source,
            "last_checked_at": "", "status": "discovered", "link_kind": "official_recommended",
            "error": "", "audiences": [], "tags": [], "favorite": False,
            "access_note": note, "access_evidence": source,
        }
        resources.append(row)
        selected.append(row)
        additions.append(row)
        extraction.append({
            "resource_id": row["id"], "resource_url": target, "source_url": source,
            "original_ahref": anchor["href"], "resolved_original_url": original,
            "anchor_label": anchor["label"], "https_upgrade": parts.scheme.lower() == "http",
            "source_capture": proof, "annotation_match": annotation_match,
        })

    if reader:
        for host, title, keyword in [
            ("cajviewer.cnki.net", "CAJViewer 官方阅读器下载页", "CAJViewer"),
            ("get.adobe.com", "Adobe Reader 官方下载页", "Adobe Reader"),
            ("www.sslibrary.com", "SSreader（学校公布的超星阅读器链接）", "SSreader"),
        ]:
            matches = [anchor for anchor in reader.anchors
                       if urlsplit(urljoin(READER_URL, anchor["href"])).hostname == host]
            if matches:
                upgraded = urlsplit(urljoin(READER_URL, matches[0]["href"])).scheme.lower() == "http"
                scheme_note = ("原HTTP链接仅在同一主机、路径和参数下升级HTTPS；" if upgraded
                               else "学校原链接为HTTPS；")
                note = (f"学校2023-11-10常用阅读器说明页列出{keyword}及该厂商链接。"
                        + scheme_note + "这里只核实公开HTML入口，"
                        "未下载、安装或检查软件兼容性。")
                add_link(title, READER_URL, matches[0], note, reader_proof)
            else:
                warnings.append(f"No actual official reader ahref found for {host}; resource not added")
    else:
        warnings.append("Reader raw source unavailable; no guessed reader resource was added")
    carsi_page, carsi_source, proof = (carsi, CARSI_SOURCE, carsi_proof) if carsi else (home, LIBRARY_HOME, home_proof)
    if carsi_page:
        matches = [anchor for anchor in carsi_page.anchors
                   if urlsplit(urljoin(carsi_source, anchor["href"])).hostname == "ds.carsi.edu.cn"]
        if matches:
            note = ("学校公布的CARSI入口；该学校专用链接可能先转向统一身份认证，"
                    "这里只对公开入口GET，遇到跨域身份认证重定向即停止，未登录或测试电子资源权限。")
            add_link("CARSI 校外电子资源访问（学校入口）", carsi_source, matches[0], note, proof,
                     annotation_match="https://ds.carsi.edu.cn/")
        else:
            warnings.append("No actual school CARSI ahref found; resource not added")
    auditor.allowed_hosts.update(safe_https(row["url"]) for row in additions)
    auditor.fetch_many([row["url"] for row in selected])
    corrections = []
    for row in selected:
        meta = auditor.responses[canonical(row["url"])]
        row["status"] = meta["access_status"]
        row["last_checked_at"] = meta["checked_at"]
        row["error"] = "" if row["status"] == "verified" else (
            meta["check_result"] + "；本次公开入口检查结果不代表用户浏览器一定不可用。"
        )
        host = urlsplit(row["url"]).hostname
        if host in FRIENDS and home and any(
            canonical(urljoin(LIBRARY_HOME, anchor["href"])) == canonical(row["url"])
            for anchor in home.anchors
        ):
            row["category"] = "other"
            row["description"] = (f"东北电力大学图书馆公开页列出的友情链接：{FRIENDS[host]}。"
                                  "这是其他高校的图书馆入口，不代表东北电力大学购买或授权其电子资源。")
            corrections.append({"id": row["id"], "kind": "friend_link_category", "to": "other",
                                "source": LIBRARY_HOME, "source_capture": home_proof})
        if canonical(row["url"]) == "https://lib.neepu.edu.cn/dzzy/sjkdh/zwwsjk.htm":
            page, evidence = source_capture(auditor, row["url"], prior_dir)
            if page and ("中外文数据库" in page.page_title or "中外文数据库" in page.headings
                         or any(anchor["label"] == "中外文数据库" for anchor in page.anchors)):
                corrections.append({"id": row["id"], "kind": "official_title", "from": row["title"],
                                    "to": "中外文数据库", "source_capture": evidence})
                row["title"] = "中外文数据库"
            else:
                warnings.append("中外文数据库 title not confirmed from actual HTML; original retained")
    annotation_audit = []
    for item in annotations:
        matched = [row for row in selected if canonical(row["url"]) == canonical(item["url"])]
        if not matched and canonical(item["url"]) == "https://ds.carsi.edu.cn/":
            matches = {record["resource_id"] for record in extraction
                       if record["annotation_match"] == "https://ds.carsi.edu.cn/"}
            matched = [row for row in selected if row["id"] in matches]
        for row in matched:
            row["access_note"] = item["note"].strip()
            row["access_evidence"] = canonical(item["evidence_url"])
        source_result = auditor.responses[canonical(item["evidence_url"])]
        annotation_audit.append({
            "annotation_url": item["url"], "source_url": item["evidence_url"],
            "source_date": item.get("source_date"), "matched_resource_ids": [row["id"] for row in matched],
            "agent_independent_web_source_access_checked": item.get("source_access_checked", False),
            "source_claim_origin": "parent agent's independent web reading in this task; not script fetch evidence",
            "script_source_http_status": source_result["http_status"],
            "script_source_status": source_result["access_status"],
            "script_source_raw_file": source_result["raw_file"],
            "script_source_raw_sha256": source_result["raw_sha256"],
        })
        if not matched:
            warnings.append("Annotation had no actual corresponding resource: " + item["url"])
    if reader and len([row for row in additions if row["discovered_from"] == READER_URL]) == 3:
        reader_resource = next(row for row in selected if canonical(row["url"]) == READER_URL)
        reader_resource["access_note"] = refreshed_reader_note(selected)
        reader_resource["access_evidence"] = READER_URL
        for entry in annotation_audit:
            if canonical(entry["annotation_url"]) == READER_URL:
                entry["merged_note_adjustment"] = "Prior runtime observations refreshed from this script's checks; original annotations file unchanged"
    validate_importable_metadata(resources)
    result = copy.deepcopy(report)
    result.update({
        "resources": resources, "resource_count": len(resources),
        "categories": dict(sorted(collections.Counter(row["category"] for row in resources).items())),
        "statuses": dict(sorted(collections.Counter(row["status"] for row in resources).items())),
        "model_calls": 0, "import_only": True, "passed": True,
        "validation_scope": "reviewed_public_metadata_not_all_links_accessible",
        "database": "", "error": "", "phase_note": "审计合并，未导入生产；此文件是导入输入，不是运行时验收报告",
        "library_access_audit": str((output / "audit.json").resolve()),
    })
    (output / "report.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    audit = {
        "started_at": min(meta["checked_at"] for meta in auditor.responses.values()), "finished_at": utc_now(),
        "report_input": str(args.report.resolve()), "report_input_sha256": digest(report_bytes),
        "annotations_input": str(args.annotations.resolve()), "annotations_input_sha256": digest(annotation_bytes),
        "source_access_checked_annotation_fields_modified": False,
        "input_resource_count": 90, "existing_selected_count": 21, "added_count": len(additions),
        "selected_resource_count": len(selected), "merged_resource_count": len(resources),
        "other_resources_not_checked": 69, "public_url_check_count": len(auditor.responses),
        "selected_status_counts": dict(sorted(collections.Counter(row["status"] for row in selected).items())),
        "all_public_response_status_counts": dict(sorted(collections.Counter(
            meta["access_status"] for meta in auditor.responses.values()).items())),
        "raw_response_count": sum(meta["raw_file"] is not None for meta in auditor.responses.values()),
        "model_calls": 0, "login_performed": False, "fulltext_requested": False, "binary_downloaded": False,
        "production_database_imported": False, "third_party_collection_defaults_changed": False,
        "limits": {"workers": WORKERS, "per_hostname_start_interval_seconds": HOST_INTERVAL,
                   "redirects": MAX_REDIRECTS, "max_text_bytes": MAX_BYTES,
                   "connect_seconds": 5, "read_seconds": 8, "url_budget_seconds": URL_BUDGET,
                   "run_budget_seconds": RUN_BUDGET, "retries": 0, "dns_helper_seconds": 3},
        "allowlisted_initial_hosts": sorted(auditor.allowed_hosts),
        "dns_safety_limit": "public DNS preflight and same registered-domain redirects; not complete rebinding protection because connection DNS is not pinned",
        "registered_domain_rule": "finite audit-host rule, last 2 labels except known Chinese edu.cn/ac.cn/com.cn/org.cn/net.cn suffixes; not a general PSL implementation",
        "verification_meaning": "verified means readable public static HTML/text only, never current school subscription/fulltext/login permission",
        "unreachable_meaning": "this audit did not pass; does not prove that a user's browser cannot use the link",
        "new_resource_provenance": extraction, "corrections": corrections, "annotations": annotation_audit,
        "responses": [auditor.responses[url] for url in sorted(auditor.responses)], "warnings": warnings,
        "merged_report": str((output / "report.json").resolve()),
    }
    (output / "audit.json").write_text(json.dumps(audit, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if args.annotations.read_bytes() != annotation_bytes:
        raise RuntimeError("Concurrent annotations change detected; inspect outputs before importing")
    print(json.dumps({key: audit[key] for key in (
        "existing_selected_count", "added_count", "selected_resource_count", "merged_resource_count",
        "selected_status_counts", "public_url_check_count", "raw_response_count", "warnings"
    )}, ensure_ascii=False, indent=2), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
