#!/usr/bin/env python3
"""Offline CampusPulse AI resource draft validator. Never calls an API or opens URLs."""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import ipaddress
import json
from pathlib import Path
import re
import sys
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
CLAIM_KINDS = ("purpose", "school_status", "scope", "campus_access", "off_campus_access", "login_condition")
UNKNOWN = "unknown"
MAX_INPUT_BYTES = 256 * 1024
MAX_OUTPUT_BYTES = 64 * 1024


class ContractError(ValueError):
    pass


def fail(path: str, detail: str) -> None:
    raise ContractError(f"{path}: {detail}")


def _no_duplicates(pairs):
    obj = {}
    for key, value in pairs:
        if key in obj:
            fail("json", f"duplicate key {key!r}")
        obj[key] = value
    return obj


def parse_json(text: str, label: str, maximum: int):
    if not isinstance(text, str) or not text.strip():
        fail(label, "empty JSON content")
    if len(text.encode("utf-8")) > maximum:
        fail(label, "JSON byte limit exceeded")
    try:
        return json.loads(text, object_pairs_hook=_no_duplicates,
                          parse_constant=lambda _: fail(label, "non-finite JSON number"))
    except (json.JSONDecodeError, UnicodeError, RecursionError) as exc:
        fail(label, f"invalid or truncated JSON: {exc}")


def load_json(path: Path, maximum: int):
    if path.stat().st_size > maximum:
        fail(str(path), "file byte limit exceeded")
    try:
        return parse_json(path.read_text(encoding="utf-8"), str(path), maximum)
    except UnicodeError as exc:
        fail(str(path), f"not UTF-8: {exc}")


def parse_date(value: str, path: str) -> dt.date:
    if not isinstance(value, str) or not re.fullmatch(r"\d{4}-\d{2}-\d{2}", value):
        fail(path, "expected YYYY-MM-DD")
    try:
        return dt.date.fromisoformat(value)
    except ValueError:
        fail(path, "invalid calendar date")


def parse_datetime(value: str, path: str) -> dt.datetime:
    if not isinstance(value, str) or not re.fullmatch(
            r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:Z|[+-]\d{2}:\d{2})", value):
        fail(path, "expected timezone-aware RFC3339 timestamp, seconds precision")
    try:
        parsed = dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
        if parsed.utcoffset() is None:
            fail(path, "timezone required")
        return parsed
    except ValueError:
        fail(path, "invalid timestamp")


def dns_host(value: str, path: str) -> str:
    if not isinstance(value, str) or len(value) > 253 or value != value.lower():
        fail(path, "expected lowercase ASCII DNS hostname")
    if not re.fullmatch(r"(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z][a-z0-9-]{1,62}", value):
        fail(path, "invalid DNS hostname")
    try:
        ipaddress.ip_address(value)
    except ValueError:
        pass
    else:
        fail(path, "IP literal is forbidden")
    if value.endswith((".localhost", ".local", ".internal")):
        fail(path, "local hostname is forbidden")
    return value


def https_url(value: str, path: str) -> str:
    if not isinstance(value, str) or any(ord(char) <= 32 for char in value) or "\\" in value:
        fail(path, "URL contains whitespace, controls or backslash")
    if re.search(r"%(?![0-9a-fA-F]{2})", value):
        fail(path, "URL contains malformed percent escape")
    if re.search(r"%(?:0[0-9a-f]|1[0-9a-f]|7f|5c)", value, flags=re.I):
        fail(path, "URL contains encoded control or backslash")
    try:
        parts = urlsplit(value)
        if (parts.scheme != "https" or not parts.netloc or parts.username is not None
                or parts.password is not None or parts.port is not None or parts.fragment):
            fail(path, "expected HTTPS URL without credentials, explicit port or fragment")
        host = dns_host(parts.hostname or "", path)
        if parts.netloc != host:
            fail(path, "URL host must be lowercase and canonical")
        return host
    except ValueError as exc:
        if isinstance(exc, ContractError):
            raise
        fail(path, f"invalid URL: {exc}")


def _resolve(schema: dict, root: dict) -> dict:
    if "$ref" not in schema:
        return schema
    ref = schema["$ref"]
    if not ref.startswith("#/"):
        raise RuntimeError("only local schema references are supported")
    node = root
    for part in ref[2:].split("/"):
        node = node[part.replace("~1", "/").replace("~0", "~")]
    return node


def check_schema(value, schema: dict, path: str, root: dict | None = None) -> None:
    """Validate the exact subset used by the bundled schemas; not a generic JSON Schema engine."""
    root = root or schema
    schema = _resolve(schema, root)
    typ = schema.get("type")
    expected = typ if isinstance(typ, list) else [typ]
    matches = {"null": value is None, "object": isinstance(value, dict),
               "array": isinstance(value, list), "string": isinstance(value, str),
               "boolean": isinstance(value, bool),
               "integer": isinstance(value, int) and not isinstance(value, bool)}
    if typ is not None and not any(matches.get(item, False) for item in expected):
        fail(path, f"expected type {typ}")
    if "const" in schema and value != schema["const"]:
        fail(path, "incorrect contract version or fixed value")
    if "enum" in schema and value not in schema["enum"]:
        fail(path, "value outside enum")
    if value is None:
        return
    if isinstance(value, dict):
        properties = schema.get("properties", {})
        if schema.get("additionalProperties") is False and set(value) - set(properties):
            fail(path, "unknown fields: " + ", ".join(sorted(set(value) - set(properties))))
        missing = set(schema.get("required", [])) - set(value)
        if missing:
            fail(path, "missing fields: " + ", ".join(sorted(missing)))
        for key, item in value.items():
            if key in properties:
                check_schema(item, properties[key], path + "." + key, root)
    elif isinstance(value, list):
        if len(value) < schema.get("minItems", 0) or len(value) > schema.get("maxItems", sys.maxsize):
            fail(path, "array item limit violated")
        if schema.get("uniqueItems") and len({json.dumps(item, sort_keys=True) for item in value}) != len(value):
            fail(path, "duplicate array items")
        for index, item in enumerate(value):
            check_schema(item, schema.get("items", {}), f"{path}[{index}]", root)
    elif isinstance(value, str):
        if len(value) < schema.get("minLength", 0) or len(value) > schema.get("maxLength", sys.maxsize):
            fail(path, "string length limit violated")
        if "pattern" in schema and re.search(schema["pattern"], value) is None:
            fail(path, "string pattern mismatch")
        if schema.get("format") == "date":
            parse_date(value, path)
        elif schema.get("format") == "date-time":
            parse_datetime(value, path)
        elif schema.get("format") == "uri":
            https_url(value, path)


def validate(request: dict, response: dict) -> dict:
    input_schema = json.loads((ROOT / "schemas/ai-resource-input.schema.json").read_text(encoding="utf-8"))
    output_schema = json.loads((ROOT / "schemas/ai-resource-output.schema.json").read_text(encoding="utf-8"))
    check_schema(request, input_schema, "input")
    check_schema(response, output_schema, "output")
    for document, maximum, label in ((request, MAX_INPUT_BYTES, "input"),
                                     (response, MAX_OUTPUT_BYTES, "output")):
        if len(json.dumps(document, ensure_ascii=False).encode("utf-8")) > maximum:
            fail(label, "JSON byte limit exceeded")
    school = request["school"]
    root_host = dns_host(school["root_host"], "input.school.root_host")
    hosts = school["trusted_source_hosts"]
    for host in hosts:
        dns_host(host, "input.school.trusted_source_hosts")
        if host != root_host and not host.endswith("." + root_host):
            fail("input.school.trusted_source_hosts", "source host is outside registered school root")
    as_of = parse_date(request["as_of"], "input.as_of")
    https_url(request["resource"]["url"], "input.resource.url")
    matches = {"request_id": request["request_id"], "school_id": school["school_id"],
               "resource_id": request["resource"]["resource_id"], "resource_url": request["resource"]["url"]}
    for key, wanted in matches.items():
        if response[key] != wanted:
            fail("output." + key, "request identity mismatch")
    sources = {}
    source_urls = set()
    total_chars = 0
    for source in request["sources"]:
        sid = source["source_id"]
        if sid in sources:
            fail("input.sources", "duplicate source_id")
        if source["url"] in source_urls:
            fail("input.sources", "duplicate source URL")
        source_urls.add(source["url"])
        if https_url(source["url"], "input.sources.url") not in hosts:
            fail("input.sources.url", "source URL host is not in trusted_source_hosts")
        retrieved = parse_datetime(source["retrieved_at"], "input.sources.retrieved_at")
        if retrieved.date() > as_of:
            fail("input.sources.retrieved_at", "retrieval is after as_of date")
        if source["published_on"] is not None:
            published = parse_date(source["published_on"], "input.sources.published_on")
            if published > retrieved.date():
                fail("input.sources.published_on", "publication is after retrieval date")
        if not source["text"].strip():
            fail("input.sources.text", "empty source text")
        digest = hashlib.sha256(source["text"].encode("utf-8")).hexdigest()
        if source["sha256"] != digest:
            fail("input.sources.sha256", "hash must match exact UTF-8 excerpt text")
        total_chars += len(source["text"])
        sources[sid] = source
    if total_chars > 24000:
        fail("input.sources", "combined excerpt limit is 24000 characters")

    def evidence(items: list, path: str) -> None:
        for item in items:
            source = sources.get(item["source_id"])
            if source is None:
                fail(path, "evidence references unknown source_id")
            if item["source_url"] != source["url"]:
                fail(path, "evidence URL does not exactly match fetched source URL")
            if not item["quote"].strip() or item["quote"] not in source["text"]:
                fail(path, "quote is not an exact substring of supplied source text")

    claims = {}
    flags = []
    for claim in response["claims"]:
        kind = claim["kind"]
        if kind in claims:
            fail("output.claims", "duplicate claim kind")
        value = claim["value"]
        if kind == "school_status":
            if value not in ("unknown", "purchased", "trial", "open"):
                fail("output.claims.school_status", "invalid school_status value")
            is_unknown = value == UNKNOWN
        else:
            is_unknown = value is None
            if not is_unknown and (not value.strip() or value.strip().lower() == UNKNOWN):
                fail("output.claims." + kind, "unknown must be null, not empty text or literal unknown")
        if is_unknown and claim["evidence"]:
            fail("output.claims." + kind, "unknown claim must have empty evidence")
        if not is_unknown and not claim["evidence"]:
            fail("output.claims." + kind, "assertion requires at least one exact source quote")
        evidence(claim["evidence"], "output.claims." + kind)
        if not is_unknown:
            flags.append({"code": "semantic_review_required", "field": "claims." + kind,
                          "reason": "Quote presence does not establish meaning, current entitlement or user authorization."})
        claims[kind] = claim
    if set(claims) != set(CLAIM_KINDS):
        fail("output.claims", "exactly one claim for every fixed claim kind is required")
    if any(item["value"] in (None, UNKNOWN) for item in claims.values()) and not response["unknowns"]:
        fail("output.unknowns", "missing facts require an explicit unknowns entry")
    if any(not item.strip() for item in response["unknowns"]):
        fail("output.unknowns", "unknowns may not contain empty strings")

    trial = response["trial"]
    evidence(trial["evidence"], "output.trial")
    start = parse_date(trial["starts_on"], "output.trial.starts_on") if trial["starts_on"] else None
    end = parse_date(trial["ends_on"], "output.trial.ends_on") if trial["ends_on"] else None
    if start or end:
        if claims["school_status"]["value"] != "trial":
            fail("output.trial", "trial dates require school_status=trial")
        if not trial["evidence"]:
            fail("output.trial", "trial date assertion requires evidence")
        for value in (trial["starts_on"], trial["ends_on"]):
            if value and not any(value in item["quote"] for item in trial["evidence"]):
                fail("output.trial", "v1 accepts only literal YYYY-MM-DD dates in quoted evidence")
    elif trial["evidence"]:
        fail("output.trial", "unknown trial dates require empty evidence")
    if start and end and start > end:
        fail("output.trial", "trial starts after it ends")
    if claims["school_status"]["value"] == "trial":
        flags.append({"code": "trial_scope_review_required", "field": "trial",
                      "reason": "Calculated date position does not establish active service or account permission."})
    if not (start and end):
        trial_position = "unknown"
    elif as_of < start:
        trial_position = "before_start"
    elif as_of > end:
        trial_position = "after_end"
    else:
        trial_position = "within_dates"
    for candidate in response["candidate_links"]:
        host = https_url(candidate["url"], "output.candidate_links.url")
        if host != root_host and not host.endswith("." + root_host):
            fail("output.candidate_links.url", "v1 accepts only school-root candidate URLs")
        evidence(candidate["evidence"], "output.candidate_links")
        if not candidate["evidence"] or not any(candidate["url"] in item["quote"] for item in candidate["evidence"]):
            fail("output.candidate_links", "candidate URL must literally occur in evidence quote")
    return {"mechanical_valid": True, "status": "validated_draft", "request_id": request["request_id"],
            "manual_review_required": True, "business_write_allowed": False,
            "trial_date_position": trial_position, "as_of": request["as_of"], "flags": flags,
            "checks": ["local_schema", "request_identity", "source_url_allowlist", "excerpt_sha256",
                       "exact_evidence_quote", "unknown_semantics", "trial_date_order", "literal_candidate_url"],
            "limitations": ["No API request or HTTP request was made.",
                            "Input trust roots must come from an already validated university package.",
                            "Exact quotes do not mechanically verify claim semantics or current school licensing.",
                            "No user authentication or full-text access was verified."]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="raw model JSON content, not an API envelope")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        result = validate(load_json(args.input, MAX_INPUT_BYTES), load_json(args.output, MAX_OUTPUT_BYTES))
        code = 0
    except (ContractError, OSError) as exc:
        result = {"mechanical_valid": False, "status": "rejected", "business_write_allowed": False,
                  "error": str(exc)}
        code = 1
    content = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(content, encoding="utf-8")
    print(content, end="")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
