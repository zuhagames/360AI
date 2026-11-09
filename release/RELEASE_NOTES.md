# 360AI 2.1 - Updates on your Xbox

Get future app updates directly from GitHub without moving your drive back to a PC.

- **Updates screen:** press START on Home, or type `open updates` in Assistant.
- **Automatic checks:** a background worker checks the latest stable release on launch. Y saves an auto-check preference. A checks or downloads; X cancels or discards a staged update.
- **Small downloads:** app-only update ZIPs contain just the XEX. Your models, notes, settings, rules and exports stay in place.
- **Verified installation:** TLS 1.2 certificate/hostname/time checks, GitHub SHA-256, ZIP/CRC checks and a staged XEX. Press A after verification to restart into the separate installer.
- **Recovery:** the installer retains `360AI-previous.xex`, checks the staged files again, attempts rollback on failure and can resume an interrupted installation. No unattended restart; session timers end at restart.
- **Network handling:** updater and model API share a reference-counted XNet/Winsock lifetime. The updater runs at below-normal priority and does not block the UI on socket reads.

**Coming from 2.0 or earlier?** Download **360AI-2.1-USB.zip**, copy the whole `360AI` folder onto your drive, and keep `360AI-updater.xex` beside `360AI.xex`. All four models remain included. Preserve your personal settings/notes if upgrading an existing folder. This first manual install adds the updater for future compatible releases.

**360AI-2.1-Update.zip** is the small app-only package used by the updater; use the USB ZIP for a first installation. Keep **32 MB free**, a correct console clock and Internet/DNS access. GitHub rate limits or DashLaunch/router blocking can prevent a check. See UPDATES.txt for troubleshooting and recovery.

Xbox SDK app/installer builds, native updater/assistant/controller tests, malformed and tampered ZIP cases, rollback/interrupted-install recovery, personal-file preservation, protocol/tokenizer/package tests, live GitHub HTTPS and release ZIP verification are checked on the host. Physical Xbox HTTPS, file replacement, power-loss behavior and relaunch still require hardware testing; no model-inference speedup is claimed.

Made by **ZuhaGames & Canyons**, expanded from [Canyons' gemma.xex](https://gitlab.com/canyons0/gemma.xex). AI tools assisted development, debugging and documentation.
