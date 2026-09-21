#!/usr/bin/env python3
"""Developer CLI for DEOS declarative manifests.

The prototype intentionally uses JSON as its zero-dependency wire format.
If PyYAML is installed, .yaml/.yml inputs are accepted too. The on-device
format is not frozen; a compact typed binary IR is planned before v0.1.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import sys
import urllib.error
import urllib.request
from typing import Any


def load_document(path: pathlib.Path) -> dict[str, Any]:
    text = path.read_text(encoding="utf-8")
    if path.suffix.lower() in {".yaml", ".yml"}:
        try:
            import yaml  # type: ignore
        except ImportError as exc:
            raise SystemExit("YAML input requires PyYAML; use JSON or install PyYAML") from exc
        data = yaml.safe_load(text)
    else:
        data = json.loads(text)
    if not isinstance(data, dict):
        raise SystemExit("manifest root must be an object")
    return data


def validate(doc: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if doc.get("apiVersion") != "deos/v1alpha1":
        errors.append("apiVersion must be deos/v1alpha1")
    resources = doc.get("resources")
    if not isinstance(resources, list):
        errors.append("resources must be an array")
        return errors

    seen: set[tuple[str, str]] = set()
    resource_by_key: dict[str, dict[str, Any]] = {}
    for index, resource in enumerate(resources):
        prefix = f"resources[{index}]"
        if not isinstance(resource, dict):
            errors.append(f"{prefix} must be an object")
            continue
        kind = resource.get("kind")
        metadata = resource.get("metadata")
        if not isinstance(kind, str) or not kind:
            errors.append(f"{prefix}.kind must be a non-empty string")
        if not isinstance(metadata, dict) or not isinstance(metadata.get("name"), str) or not metadata.get("name"):
            errors.append(f"{prefix}.metadata.name must be a non-empty string")
            continue
        if isinstance(kind, str) and kind:
            key = (kind, metadata["name"])
            string_key = f"{kind}/{metadata['name']}"
            if key in seen:
                errors.append(f"duplicate resource {string_key}")
            seen.add(key)
            resource_by_key[string_key] = resource
        spec = resource.get("spec", {})
        if not isinstance(spec, dict):
            errors.append(f"{prefix}.spec must be an object")
        depends_on = resource.get("dependsOn", [])
        if not isinstance(depends_on, list) or not all(isinstance(item, str) and "/" in item for item in depends_on):
            errors.append(f"{prefix}.dependsOn must be an array of Kind/name strings")

    for key, resource in resource_by_key.items():
        for dependency in resource.get("dependsOn", []):
            if isinstance(dependency, str) and dependency not in resource_by_key:
                errors.append(f"{key} depends on missing resource {dependency}")

    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(key: str, path: list[str]) -> None:
        if key in visited or key not in resource_by_key:
            return
        if key in visiting:
            try:
                start = path.index(key)
            except ValueError:
                start = 0
            cycle = path[start:] + [key]
            errors.append("dependency cycle: " + " -> ".join(cycle))
            return
        visiting.add(key)
        path.append(key)
        for dependency in resource_by_key[key].get("dependsOn", []):
            if isinstance(dependency, str):
                visit(dependency, path)
        path.pop()
        visiting.remove(key)
        visited.add(key)

    for key in sorted(resource_by_key):
        visit(key, [])

    return errors


def index_resources(doc: dict[str, Any]) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for resource in doc.get("resources", []):
        key = f"{resource['kind']}/{resource['metadata']['name']}"
        result[key] = resource
    return result


def plan(desired: dict[str, Any], actual: dict[str, Any]) -> list[tuple[str, str]]:
    wanted = index_resources(desired)
    current = index_resources(actual)
    result: list[tuple[str, str]] = []

    for key in sorted(wanted):
        if key not in current:
            result.append(("CREATE", key))
        elif {
            "spec": current[key].get("spec", {}),
            "dependsOn": current[key].get("dependsOn", []),
        } != {
            "spec": wanted[key].get("spec", {}),
            "dependsOn": wanted[key].get("dependsOn", []),
        }:
            result.append(("UPDATE", key))

    for key in sorted(current):
        if key not in wanted:
            result.append(("DELETE", key))
    return result


def normalize_device(value: str) -> str:
    value = value.strip().rstrip("/")
    if not value:
        raise SystemExit("device address must not be empty")
    if "://" not in value:
        value = "http://" + value
    if not value.startswith(("http://", "https://")):
        raise SystemExit("device address must use http:// or https://")
    return value


def resolve_token(explicit: str | None) -> str:
    token = explicit or os.environ.get("DEOS_TOKEN", "")
    if not token:
        raise SystemExit("DEOS API token required: pass --token or set DEOS_TOKEN")
    return token


def remote_request(
    device: str,
    path: str,
    *,
    method: str = "GET",
    token: str | None = None,
    body: bytes | None = None,
    content_type: str | None = None,
) -> bytes:
    url = normalize_device(device) + path
    headers = {"User-Agent": "deosctl/0.1"}
    if token:
        headers["X-DEOS-Token"] = token
    if content_type:
        headers["Content-Type"] = content_type

    request = urllib.request.Request(
        url,
        data=body,
        headers=headers,
        method=method,
    )
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            return response.read()
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise SystemExit(f"device returned HTTP {exc.code}: {detail}") from exc
    except urllib.error.URLError as exc:
        raise SystemExit(f"cannot reach {url}: {exc.reason}") from exc


def print_json_response(payload: bytes) -> None:
    text = payload.decode("utf-8", errors="replace")
    try:
        parsed = json.loads(text)
    except json.JSONDecodeError:
        print(text)
        return
    print(json.dumps(parsed, ensure_ascii=False, indent=2, sort_keys=True))


def parse_action_arg(value: str) -> tuple[str, Any]:
    if "=" not in value:
        raise SystemExit(f"action argument must be key=value: {value}")
    key, raw = value.split("=", 1)
    key = key.strip()
    if not key:
        raise SystemExit("action argument key must not be empty")
    raw = raw.strip()
    if not raw:
        return key, ""
    try:
        parsed = json.loads(raw)
    except json.JSONDecodeError:
        parsed = raw
    if not isinstance(parsed, (bool, int, float, str)):
        raise SystemExit(f"action argument must be a scalar JSON value: {key}")
    return key, parsed


def command_entities(args: argparse.Namespace) -> int:
    payload = remote_request(
        args.device,
        "/api/v1/entities",
        token=resolve_token(args.token),
    )
    print_json_response(payload)
    return 0


def command_actions(args: argparse.Namespace) -> int:
    payload = remote_request(
        args.device,
        "/api/v1/actions",
        token=resolve_token(args.token),
    )
    print_json_response(payload)
    return 0


def command_invoke(args: argparse.Namespace) -> int:
    values: dict[str, Any] = {}
    for item in args.arg:
        key, value = parse_action_arg(item)
        if key in values:
            raise SystemExit(f"duplicate action argument: {key}")
        values[key] = value

    body = json.dumps(
        {"id": args.action_id, "args": values},
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode("utf-8")

    payload = remote_request(
        args.device,
        "/api/v1/action",
        method="POST",
        token=resolve_token(args.token),
        body=body,
        content_type="application/json",
    )
    print_json_response(payload)
    return 0


def command_status(args: argparse.Namespace) -> int:
    payload = remote_request(args.device, "/api/v1/status")
    print_json_response(payload)
    return 0


def command_reboot(args: argparse.Namespace) -> int:
    payload = remote_request(
        args.device,
        "/api/v1/reboot",
        method="POST",
        token=resolve_token(args.token),
        body=b"",
        content_type="application/octet-stream",
    )
    print_json_response(payload)
    return 0


def command_ota(args: argparse.Namespace) -> int:
    firmware = pathlib.Path(args.firmware)
    if not firmware.is_file():
        raise SystemExit(f"firmware not found: {firmware}")

    image = firmware.read_bytes()
    if not image:
        raise SystemExit("firmware image is empty")

    print(
        f"Uploading {firmware.name} ({len(image)} bytes) "
        f"to {normalize_device(args.device)} ..."
    )
    payload = remote_request(
        args.device,
        "/api/v1/ota",
        method="POST",
        token=resolve_token(args.token),
        body=image,
        content_type="application/octet-stream",
    )
    print_json_response(payload)
    return 0


def command_validate(args: argparse.Namespace) -> int:
    doc = load_document(pathlib.Path(args.manifest))
    errors = validate(doc)
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    print(f"valid: {len(doc['resources'])} resources")
    return 0


def command_plan(args: argparse.Namespace) -> int:
    desired = load_document(pathlib.Path(args.manifest))
    actual = load_document(pathlib.Path(args.actual)) if args.actual else {"apiVersion": "deos/v1alpha1", "resources": []}
    for label, doc in (("desired", desired), ("actual", actual)):
        errors = validate(doc)
        if errors:
            for error in errors:
                print(f"ERROR ({label}): {error}")
            return 1

    changes = plan(desired, actual)
    if not changes:
        print("No changes. System already matches desired state.")
        return 0
    print("DEOS Plan")
    for op, key in changes:
        print(f"{op:6} {key}")
    print(f"\n{len(changes)} change(s)")
    return 0


def command_normalize(args: argparse.Namespace) -> int:
    doc = load_document(pathlib.Path(args.manifest))
    errors = validate(doc)
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    json.dump(doc, sys.stdout, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    print()
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="deosctl")
    sub = parser.add_subparsers(dest="command", required=True)

    p_validate = sub.add_parser("validate", help="validate a desired-state manifest")
    p_validate.add_argument("manifest")
    p_validate.set_defaults(func=command_validate)

    p_plan = sub.add_parser("plan", help="compare desired and actual manifests")
    p_plan.add_argument("manifest")
    p_plan.add_argument("--actual", help="actual-state snapshot; defaults to empty")
    p_plan.set_defaults(func=command_plan)

    p_normalize = sub.add_parser("normalize", help="emit canonical JSON")
    p_normalize.add_argument("manifest")
    p_normalize.set_defaults(func=command_normalize)

    p_status = sub.add_parser("status", help="read live device status over LAN")
    p_status.add_argument("--device", default="http://deos.local")
    p_status.set_defaults(func=command_status)

    p_entities = sub.add_parser("entities", help="list typed device state entities")
    p_entities.add_argument("--device", default="http://deos.local")
    p_entities.add_argument("--token", help="API token; defaults to DEOS_TOKEN")
    p_entities.set_defaults(func=command_entities)

    p_actions = sub.add_parser("actions", help="list remotely visible actions")
    p_actions.add_argument("--device", default="http://deos.local")
    p_actions.add_argument("--token", help="API token; defaults to DEOS_TOKEN")
    p_actions.set_defaults(func=command_actions)

    p_invoke = sub.add_parser("invoke", help="invoke a capability-gated device action")
    p_invoke.add_argument("action_id")
    p_invoke.add_argument(
        "--arg",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="scalar action argument; VALUE accepts JSON booleans/numbers/strings",
    )
    p_invoke.add_argument("--device", default="http://deos.local")
    p_invoke.add_argument("--token", help="API token; defaults to DEOS_TOKEN")
    p_invoke.set_defaults(func=command_invoke)

    p_reboot = sub.add_parser("reboot", help="reboot a DEOS device over LAN")
    p_reboot.add_argument("--device", default="http://deos.local")
    p_reboot.add_argument("--token", help="API token; defaults to DEOS_TOKEN")
    p_reboot.set_defaults(func=command_reboot)

    p_ota = sub.add_parser("ota", help="upload an application binary to the inactive A/B slot")
    p_ota.add_argument("firmware")
    p_ota.add_argument("--device", default="http://deos.local")
    p_ota.add_argument("--token", help="API token; defaults to DEOS_TOKEN")
    p_ota.set_defaults(func=command_ota)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
