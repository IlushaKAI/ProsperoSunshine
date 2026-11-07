# CLAUDE.md

Notes for coding agents working in this repository. User-facing docs: `README.md`; plan: `ROADMAP.md`;
verified state with evidence: `STATUS.md` (both German).

## Layout

- `src/sunshine/` – PS5 replacements for Sunshine parts (video, audio, input, process, main).
- `src/platform/ps5/` – Avcap2 capture (`capture.c`), `SharedCapture`, backend glue.
- `src/capture/`, `src/video/` – portable logic with unit tests in `tests/`.
- `src/web/` – the web page (HTTPS on base port + 1, off without a login): `web_auth.hpp`, `web_lines.hpp`, `web_plain.hpp` (plain HTTP gets a redirect) portable with
  tests in `tests/`; `web_login.cpp`, `web_server.cpp` host-only; `page/` HTML/JS/CSS embedded into the ELF by `CMakeLists.txt`.
- `patches/<dep>/` – patches applied on top of pinned sources; `deps.lock` pins every source by SHA256.
- `app/` – the PS5 settings app `PPSA47989` (ps5-homebrew-ui, ShadowMountPlus title folder).
- `tools/moonlight/` – automated Moonlight test client (pairing, smoke, reconnect, loss, A/V sync).
- `docs/` – reverse-engineering notes (German), ADRs.
- `assets/app/` – sources for the app icon (`icon0.png` 512²) and home-screen background (`pic0.png` 1920×1080).

## Build and test

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1   # host + probes -> out/
```

```sh
docker run --rm --mount type=bind,source="$PWD",target=/repo prosperosunshine-sdk:v0.45 bash scripts/test.sh
```

The app and its tests (the app bundles `out/prospero_sunshine.elf`, so build the host first):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File app/build.ps1   # -> build/app/PPSA47989 and .zip
```

```sh
docker run --rm --mount type=bind,source="$PWD",target=/repo prosperosunshine-app:2 bash app/tests/run.sh
```

Stream test against the console, from Git Bash: build the local client once with `bash tools/moonlight/build_windows.sh`, then
`STREAM_ARGS="--no-script" [VIDEO_FORMAT=hevc] bash tools/moonlight/run_smoke.sh <console> <seconds>`. `--no-script`
presses nothing; without it the client presses buttons on the console. Test locally, not in Docker: Docker's network drops
large bursts and falsifies results. Only `loss_test.sh` needs the container client (`tools/moonlight/build.sh`). The home
screen is too quiet to load the encoder; the Mario 64 demo moves without input.

Release archive from an existing build: `bash scripts/package.sh [version]` (on the host) -> `dist/`.

Toolchain: PS5 Payload SDK v0.45 in the Docker image `prosperosunshine-sdk:v0.45` (clang 18, target
`x86_64-sie-ps5`). Dependencies build once into `out/deps` via `scripts/deps.sh`.

## Rules

- **The repo will be public.** No console IP, local paths, e-mail addresses or names in tracked files.
  Test tools take the console address from `PS5_HOST`. Grep the diff before every commit.
- Firmware 13.00 and 10.20 are verified. Capture refuses other versions unless the ResourceArbitrator calls
  resolve by NID, and then logs them as unverified; see `docs/firmware-porting.md` before porting.
- New or changed third-party sources: pin them in `deps.lock` (SHA256), patch via `patches/<dep>/`. Archives from servers
  outside GitHub (today ffmpeg and Opus) are mirrored in the `deps-mirror` pre-release, which `scripts/deps.sh` falls back
  to; upload the new archive there under its original file name when bumping one (needs the owner's OK, it publishes).
- `STATUS.md` records only what was verified live, with how it was measured. Plans go in `ROADMAP.md`.
- Commit only when asked; never push without being asked.
- Do not test rest mode on the console without asking first (risk to the homebrew environment). One test
  ended in a hung console that had to be switched off at the power button. Close all games first.
- Publishing (push, GitHub release, store catalog PR, issue comments) needs the owner's OK each time. No
  attribution lines for agents in commits or PRs.
- EULAs and terms of games and apps on the PS5 may be accepted for the owner; accounts, purchases and anything off the
  console still need asking. Don't save games.

## Releasing

0. Before starting a feature and again before a release: `gh pr list` and `gh issue list` (open ones, and what changed since the
   last release). A contributor may already be working on the same thing; PR 16 built surround in parallel with 0.3.1.
1. Set the version in `CMakeLists.txt` (`PROJECT_VERSION`, `PROJECT_VERSION_PATCH`), `contentVersion` in
   `app/sce_sys/param.json` (must rise, format `01.000.004`), the status line in `README.md` and `CHANGELOG.md`.
2. Build host and app, run both test suites, `scripts/package.sh <version>`. Check `SHA256SUMS` and grep the ELF
   for private strings (`10.0.0.0/8` in there is Sunshine's own LAN range).
3. Install the build on the console, re-register the app (below), restart the host, stream H.264 and HEVC.
4. Push, wait for CI, then `gh release create v<version> build/app/PPSA47989.zip dist/prosperosunshine-<version>.tar.gz`
   with `--target <full SHA>` (a short SHA fails with HTTP 422).
5. Catalog: a PR to `darkxex/ps5-homebrew-catalog` updating `apps/PPSA47989.json` (`version`, `artifact_url`, `sha256`,
   `icon_url`) from a fork branch. The store API at `/api/v1/apps/PPSA47989.json` shows when it is live.

## Working on the console

- The host is an ELF payload (ELF loader on port 9021); state lives in `/data/prosperosunshine/`.
  Control socket on TCP 47995, 127.0.0.1 only (the empty file `control_lan` there opens it to the LAN):
  `STATUS`, `PAIRINGS`, `PIN [<id>] <pin> [name]`, `DEVICES`, `STREAMS`, `ENDSTREAM <uuid>`, `RENAME <uuid> <name>`, `UNPAIR <uuid>`, `REPORT`, `AUTOLOAD [ON|OFF]`, `RELISTEN`, `PROBE`, `FILE`, `INSTALL`, `CANCELPIN <id>`, `WEBLOGIN [SET <user> <password>|ON|OFF|FORGET]`, `QUIT` (one client at a time). `STREAMS` lists the running streams, one `OK uuid=... address=... codec=... hdr=... mode=... kbps=... secs=... pads=... role=... channel=... name=...` line each, then `END` (format in `src/control/stream_list.hpp`); `ENDSTREAM` ends one device's stream while the game and the others go on. `REPORT` makes the
  host answer on TCP 47996 for some seconds with `diagnostics.txt` (a title may not listen on the network).
  `RELISTEN` starts the host's services anew as after rest mode took its ports; `out/relisten_probe.elf` sends it.
  `PROBE` sends `/data/prosperosunshine/firmware_probe.elf` to the ELF loader unless a stream runs (`docs/firmware-probe.md`).
- Before touching the console, check `http://<console>:47989/serverinfo`: go on only at `SUNSHINE_SERVER_FREE`, someone may
  be streaming. Don't take screenshots during a stream.
- Logs: `sunshine.log` (older ones `.1`, `.2`, ...; `min_log_level = debug` in `sunshine.conf` also logs motors, light bar
  and trigger blocks), and `host_diag.log`, which the app's diagnostic report reads. The host reads `sunshine.conf` at
  start; to restart it, use "Restart host" in the app.
- Key presses from a tool add a virtual controller next to the host's pad, and it stays. Remove it afterwards, or the
  owner's input in a game may not work. After waking from rest mode the console waits on "Press the PS button" and the
  user selection.
- Rest mode closes the host's listening sockets and stops its log files; `wait_or_ports_lost` in `main_ps5.cpp` notices
  refused ports and starts the services anew in the same process.
- Whether a game sends trigger effects: only some do (Teardown yes, GTA III not in the sessions measured), and among the official clients only
  Moonlight for PC passes them to a DualSense (a contributor reports R3VALATiON's Artemis fork on Android too, issue 30). See `docs/re/adaptive-triggers.md`.
- Autoload through the Payload Manager: `/data/pldmgr/autoload.txt`, slot `local/prospero-sunshine`.
- A title may have no `/data` (9.60): then the app reaches the files listed in `src/control/file_bridge.cpp` through `FILE` and `INSTALL` on
  the control socket, while the host runs, and the host closes it through `/app0/app.quit`.
- The app is installed at `/data/homebrew/PPSA47989`. A ShadowMountPlus scan does **not** pick up changes to
  `sce_sys` (param.json, icon, background) of an already registered title. Re-register it:
  `POST http://<console>:10101/api/v1/games/uninstall {"title_id":"PPSA47989"}` (keeps the source folder),
  then `POST /api/v1/scan {}`.
- The app sits in the Media tab like Prospero Themes: `applicationCategoryType` 65536, while keeping
  `contentBadgeType` 1 and the `launchActivity` intent. A Media-kind title must not carry the `amm` block
  (launch fails with 0x80020016 / CE-100022-5).
- The ProsperoStore catalog is cached on the console under `/data/prosperostore/cache/`; check a title ID
  against it before a release.
- Close the app (write `/data/prosperosunshine/app.quit`) before uploading to it or starting another title. Once a title
  started while the app was still alive, and the app was started again later: from then on every title failed with `0x80940033`,
  ShadowMountPlus logging `launch mount unavailable ... Device busy`, until the console restarted (`STATUS.md`).
- Don't open the real Moonlight on the PC without asking, its window takes the focus and the owner may be playing there.
  `stream_test.exe` has no window.
- `ps5_launch` and `ps5_open_url` sometimes fail with `0x80960003` while nothing is wrong; try again. A browser card must be
  closed first (hold Options, Close Card). For encoder load without a game, open a noise page in the browser card.
