#!/usr/bin/env python3
"""Install the native MQTT app into a fresh, pinned KVXputer checkout."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil

UPSTREAM_COMMIT = "c2ff56f154c35eb13fcfab845fb30dcecf372449"
ROOT = Path(__file__).resolve().parents[1]

def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text:
        return
    assert text.count(old) == 1, f"Unexpected upstream layout: {path} (anchor {old!r})"
    path.write_text(text.replace(old, new, 1), encoding="utf-8")

def install(kvx: Path) -> None:
    kvx = kvx.resolve()
    menu = kvx / "src/root/ui/main_menu.h"
    reg = kvx / "src/root/ui/menu_registry.cpp"
    assert menu.is_file() and reg.is_file(), "KVXputer source not found"

    # MQTT V0.2.3 (user version V5) is native, not a second setup()/loop().
    mqtt_source = ROOT / "overlay" / "src" / "menu" / "kvx_mqtt"
    mqtt_dest = kvx / "src" / "menu" / "kvx_mqtt"
    mqtt_dest.mkdir(parents=True, exist_ok=True)
    for source in mqtt_source.glob("*"):
        if source.suffix in {".h", ".cpp"}:
            shutil.copyfile(source, mqtt_dest / source.name)
    # Credentials are optional: /mqtt/config.ini on microSD is the normal
    # configuration source. Otherwise CI can inject them only at build time.
    mqtt_env = {
        "KVX_MQTT_HOST": os.environ.get("KVX_MQTT_HOST", ""),
        "KVX_MQTT_USER": os.environ.get("KVX_MQTT_USER", ""),
        "KVX_MQTT_PASSWORD": os.environ.get("KVX_MQTT_PASSWORD", ""),
        "KVX_MQTT_DEVICE": os.environ.get("KVX_MQTT_DEVICE", "a"),
        "KVX_MQTT_PEER": os.environ.get("KVX_MQTT_PEER", "b"),
    }
    mqtt_port = int(os.environ.get("KVX_MQTT_PORT", "8883"))
    assert 1 <= mqtt_port <= 65535, "Invalid MQTT TLS port"
    if any(mqtt_env[k] for k in ("KVX_MQTT_HOST", "KVX_MQTT_USER", "KVX_MQTT_PASSWORD")):
        assert all(mqtt_env[k] for k in ("KVX_MQTT_HOST", "KVX_MQTT_USER", "KVX_MQTT_PASSWORD")), "Incomplete MQTT build configuration"
    mqtt_secrets = (
        "#pragma once\n"
        "// Transient build configuration. Do not commit this generated file.\n"
        + "".join("#define " + k + " " + json.dumps(v) + "\n" for k, v in mqtt_env.items())
        + "#define KVX_MQTT_PORT " + str(mqtt_port) + "\n"
    )
    (mqtt_dest / "kvx_mqtt_private.h").write_text(mqtt_secrets, encoding="utf-8")
    # KVXputer retains its existing platformio.ini and all historical modules.
    # Add the single MQTT dependency without changing source filters or partitions.
    platformio_ini = kvx / "platformio.ini"
    pio_text = platformio_ini.read_text(encoding="utf-8")
    if "256dpi/MQTT@2.5.2" not in pio_text:
        dep_anchor = "\tmikalhart/TinyGPSPlus\n"
        assert pio_text.count(dep_anchor) == 1, "Unexpected upstream lib_deps"
        platformio_ini.write_text(pio_text.replace(dep_anchor,
             dep_anchor + "\t256dpi/MQTT@2.5.2\n", 1), encoding="utf-8")


    replace_once(menu,
        '#include "menu/discovery/discovery_menu.h"',
        '#include "menu/discovery/discovery_menu.h"\n#include "menu/kvx_mqtt/kvx_mqtt_menu.h"')
    replace_once(menu,
        '    DiscoveryMenu discoveryMenu;',
        '    DiscoveryMenu discoveryMenu;\n    KvxMqttMenu kvxMqttMenu;')

    text = reg.read_text(encoding="utf-8")
    anchor = '    {"config",'
    assert text.count(anchor) == 1, "Unexpected KVX menu layout"
    mqtt_entry = (
        '#if defined(HAS_KEYBOARD)\n'
        '    {"kvx-mqtt", "MQTT", &mainMenu.kvxMqttMenu, MENU_FLAG_NONE},\n'
        '#endif\n'
    )
    if mqtt_entry not in text:
        line_end = text.index("\n", text.index(anchor)) + 1
        reg.write_text(text[:line_end] + mqtt_entry + text[line_end:], encoding="utf-8")
    print("Installed: native MQTT V5")

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--kvx", type=Path, required=True)
    args = p.parse_args()
    install(args.kvx)
