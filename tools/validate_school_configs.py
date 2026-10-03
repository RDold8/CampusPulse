"""Validate configuration contracts only; no network access or adapter execution."""

import argparse
import json
import sys
from pathlib import Path
from urllib.parse import urlsplit
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

from jsonschema import Draft202012Validator, FormatChecker

ROOT = Path(__file__).resolve().parents[1]


def validate_config(config, validator):
    errors = [
        f"{'/'.join(str(part) for part in error.absolute_path) or '<root>'}: {error.message}"
        for error in validator.iter_errors(config)
    ]
    if errors:
        return errors
    try:
        ZoneInfo(config['school']['timezone'])
    except (ZoneInfoNotFoundError, ValueError):
        errors.append('school/timezone: unknown timezone or missing tzdata')
    homepage = urlsplit(config['school']['official_homepage'])
    official_root = (homepage.hostname or '').lower().removeprefix('www.')
    for entry in config.get('resource_discovery', {}).get('department_urls', []):
        try:
            url = urlsplit(entry)
            host = (url.hostname or '').lower()
            safe = (url.scheme == 'https' and host and official_root
                    and (host == official_root or host.endswith('.' + official_root))
                    and '@' not in url.netloc and ':' not in url.netloc
                    and not any(ch.isspace() or ord(ch) < 32 or ch == '\\' for ch in entry))
        except ValueError:
            safe = False
        if not safe:
            errors.append('resource_discovery/department_urls: requires a safe official HTTPS URL')
    seen = set()
    for source in config['sources']:
        prefix = f"sources/{source['key']}"
        if source['key'] in seen:
            errors.append(f'{prefix}: duplicate source key')
        seen.add(source['key'])
        if not set(source['category_hints']).issubset(config['categories']):
            errors.append(f'{prefix}: category hints absent from school categories')
        for field in ('discovery_url', 'entry_url'):
            if source[field] is None:
                continue
            url = urlsplit(source[field])
            if url.hostname not in source['allowed_hosts']:
                errors.append(f'{prefix}/{field}: host absent from allowed_hosts')
            if url.username is not None or url.password is not None:
                errors.append(f'{prefix}/{field}: embedded credentials are forbidden')
        if source['enabled'] and any(host.endswith('.invalid') for host in source['allowed_hosts']):
            errors.append(f'{prefix}: placeholder hosts cannot be enabled')
        access = source.get('access', {})
        if 'login_url' in access:
            value = access['login_url']
            try:
                login = urlsplit(value)
                host = (login.hostname or '').lower()
                safe = (login.scheme == 'https' and host and official_root
                        and (host == official_root or host.endswith('.' + official_root))
                        and host in source['allowed_hosts']
                        and login.username is None and login.password is None
                        and '@' not in login.netloc and ':' not in login.netloc
                        and not any(ch.isspace() or ord(ch) < 32 or ch == '\\' for ch in value))
            except ValueError:
                safe = False
            if not safe:
                errors.append(f'{prefix}/access/login_url: requires a trusted official HTTPS host without credentials or port')
    if not homepage.hostname or homepage.username is not None or homepage.password is not None:
        errors.append('school/official_homepage: invalid host or embedded credentials')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paths', nargs='*', type=Path)
    args = parser.parse_args()
    paths = args.paths or sorted((ROOT / 'configs/schools').glob('*.json')) + sorted((ROOT / 'templates').glob('university*.json'))
    if not paths:
        parser.error('no school configuration files found')
    schema = json.loads((ROOT / 'schemas/school.schema.json').read_text(encoding='utf-8'))
    Draft202012Validator.check_schema(schema)
    validator = Draft202012Validator(schema, format_checker=FormatChecker())
    failures = 0
    school_keys = {}
    for path in paths:
        try:
            config = json.loads(path.read_text(encoding='utf-8'))
            errors = validate_config(config, validator)
            if not errors:
                key = config['school']['key']
                if key in school_keys:
                    errors.append(f'duplicate school key already used in {school_keys[key]}')
                school_keys[key] = path
        except (OSError, ValueError) as exc:
            errors = [str(exc)]
        if errors:
            failures += 1
            print(f'FAIL {path}')
            for error in errors:
                print(f'  {error}')
        else:
            print(f'PASS {path}')
    print(f'Checked {len(paths)} file(s); failures={failures}. Scope: configuration contracts only.')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
