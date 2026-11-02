# NEXUS integration for KVXputer — phase 1

This is a source-only early integration, not a compiled KVXputer firmware.

Upstream: https://github.com/kvx17/kvxputer (AGPL-3.0). The upstream repository is read-only with the current connection.

Copy `src/menu/others/nexus/` into an upstream clone and apply `AJOUT_KVXPUTER.patch` at its root. This exposes "NEXUS Messages" inside Tools.

The initial module reads saved `nexusadv` NVS configuration in read-only mode, shows status and recent microSD messages, and uses KVXputer's existing Wi-Fi menu. It does not include a second Morse module or separate Wi-Fi scanner.

**Not yet implemented/verified:** sending new text, receiving new text via HTTPS, voice playback/recording integration, authentication flow on KVXputer, server interoperability, Cardputer hardware tests, full upstream compilation. These remain necessary before an integrated release can be offered.

Do not flash a KVXputer image without a successful PlatformIO build, pinout and partition checks, and real-device testing. Preserve NVS, microSD data and the existing KVXputer firmware.