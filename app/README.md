# ProsperoSunshine app

A PS5 homebrew title (`PPSA47989`, "ProsperoSunshine") that runs the ProsperoSunshine host from the
console itself, with a controller:

- **Host**: shows whether the host runs or streams, and starts, stops or restarts it.
- **Pairing**: lists Moonlight's pending pairing requests and takes the PIN Moonlight shows, on an
  on-screen keyboard. This replaces Sunshine's web UI for pairing.
- **Settings**: edits `/data/prosperosunshine/sunshine.conf`, and turns the host's web page on or off.
- **Start at boot**: puts the host into the Payload Manager's autoload list, or takes it out.
- **Diagnostics**: a report for a bug report, to read on the console, from a phone or PC, or as a file.

It is drawn with [ps5-homebrew-ui](https://github.com/Phononenom/ps5-homebrew-ui) and built, packaged
and installed the same way as [PS5SD](https://github.com/Darkatek7/PS5SD): a title folder for
ShadowMountPlus.

## Requirements

- The host's requirements (`../README.md`): an ELF loader on port 9021; FW 13.00 and 10.20 are the tested versions.
- ShadowMountPlus, to register the title.
- For **Start at boot**: the [Payload Manager](https://github.com/itsPLK/ps5-payload-manager)
  (`PLDM00001`, tested with v0.6.3), with its own autoload switched on.

## Installing

Copy the folder `PPSA47989` (from `build/app/`, or unpacked from `PPSA47989.zip`) to
`/data/homebrew/` on the console, for example with ftpsrv on port 2121, and let ShadowMountPlus scan.
"ProsperoSunshine" then shows on the home screen, under Media.

ShadowMountPlus copies the title's `sce_sys` (icon, `param.json`, backgrounds) to
`/user/appmeta/PPSA47989/` when it registers the title, and leaves that copy alone for a title it
knows. After a change to those files, copy them there too (or remove the title and let it register
again). In the Media row the home screen shows the system theme's wallpaper, not `pic0.dds`.

The title carries the host (`payloads/prospero_sunshine.elf`). At every start the app copies it to
`/data/prosperosunshine/prospero_sunshine.elf` when that file is missing or another build; start,
restart and autoload all use that copy. So a new host reaches the console with a new version of the
app.

## Using it

Everything works with the controller. L1 and R1 switch between the three pages in the tab bar; the
D-pad moves the focus, which glides between the parts; Cross acts; Triangle reads the files again
(for example after editing them over FTP). The bar above the button hints shows what is being done
and how the last request went.

**Home** shows the host:

| Part | What it does |
| --- | --- |
| Hero | Host stopped, ready to stream, streaming (which app, and which client when the host reports it), or session paused (the host still has "PS5 Screen" open for a client that left; only hosts that report `client=` tell this apart), polled every two seconds through the host's control socket. The sun turns while the host runs and sends out rings while it streams. The chips under it: the port, LAN encryption, whether the host starts at boot, how many devices are paired. The console's IP address is in the top bar. |
| Start host / Stop host | Start sends the host ELF to the ELF loader (`127.0.0.1:9021`) and waits until the host answers. Stop sends `QUIT` and waits until it is gone. |
| Restart host | Stop, then start: the way new settings take effect. |
| Start at boot | The switch for the Payload Manager's autoload (below). A line under it warns when the Payload Manager's own autoload is off (with a running host, which can read `pldmgr_config.txt`; a title may not). |
| Notice | One line above the pairing part when there is something to know about this console: the firmware is untested, the last capture attempt failed, and why (HDR on, capture in use, firmware not supported, the step that failed), or an Experimental switch failed or started and the Diagnostics report is wanted. It comes from the firmware and the host's `host_diag.log`. |
| Pairing | A card per Moonlight client waiting to pair, with its name and address; a new request takes the focus and chimes. Cross opens the PIN keyboard: type the four digits Moonlight shows and press Done (R2 or Options). Square on a card denies the request (`CANCELPIN`), and Moonlight hears that the operator cancelled it. The app waits until Moonlight has finished pairing and says whether it worked. Beside it, the paired devices (from `sunshine_state.json`). |
| Streaming now | While someone streams, a card with the first two devices (from the host's `STREAMS`): name, codec and HDR, resolution and frame rate, bitrate, controllers and how long it has been streaming; "joined" marks a device that gets another device's picture. It takes the second pairing card's place, or the empty pairing card's. Cross opens the list of all streams with their addresses; Cross on one asks "End the stream of ...?", and a second Cross ends that device's stream (`ENDSTREAM`). The game and the other streams go on, and the device can connect again. |

While a stream runs, Stop, Restart and Save and restart need a second Cross within four seconds: the
player streaming the console may be the one pressing. Input is taken only after the app has been up
for half a second.

Known limit: while the host has a session open, also a paused one, its virtual DualSense is
connected for the console's user, and the app (which reads that user's pad, as the kit does) gets
its input from it. On the test console the remote-play test controller then did not reach the app
until the session ended (Quit in Moonlight, or a host restart). A button that is down when the app
starts is ignored until it is let go, so it cannot act on the focused Stop host.

**Settings** shows one tile per option. Cross opens its editor in a sheet over the frosted page: a
list for choices, a stepper for numbers (left and right one step, up and down ten, L1 and R1 a
hundred; Square the default), the keyboard for the host name (empty: the default). Square on a tile
puts it back to the host's default (the key is then removed from the file). A tile says DEFAULT or
UNSAVED; the bar under the tiles explains the focused one. **Save** writes the file; the host reads
it only when it starts, so use **Save and restart host** (or Restart host later) to apply it.
**Revert changes** goes back to what the file holds.

| Setting | `sunshine.conf` key | Values | Without the key |
| --- | --- | --- | --- |
| Host name | `sunshine_name` | text, up to 63 characters, no `#` or `[` | the console's network name |
| Log level | `min_log_level` | verbose, debug, info, warning, error, fatal, none | info |
| Ping timeout (ms) | `ping_timeout` | 500 to 60000 | 2000 (the host's own default) |
| Encryption on the LAN | `lan_encryption_mode` | 0 off, 1 when the client supports it, 2 required | 0 |
| Encryption over the internet | `wan_encryption_mode` | as above | 1 |
| Base port | `port` | 1025 to 65514 | 47989 |
| HEVC (8 bit) | `experimental_hevc` | 1 on | off |
| HDR (10 bit) | `experimental_hdr` | 1 on | off |
| 1440p and 4K | `experimental_large_modes` | 1 on | off |
| 150 Mbit/s | `experimental_high_bitrate` | 1 on | off (cap 50 Mbit/s; 150 on firmware 13.00 anyway) |
| Below 1080p | `experimental_small_modes` | 1 on | off (1080p for smaller requests; on firmware 13.00 and 10.20 those modes anyway) |
| Surround | `surround_sound` | 1 on | off (stereo; with it, 5.1 and 7.1 for clients that ask, speaker order unverified) |

The names, ranges and defaults are Sunshine's (`src/config.cpp` at a679b059), except for the `experimental_` keys, which the host
reads itself. Every other line of the file, comments included, is kept as it is. Sunshine uses the first line of a key; when the app
sets a key that appears more than once, it keeps the first line's place and removes the others.

**Web page** sets the user name and password for the host's [web page](../README.md#web-page) (`WEBLOGIN SET`), which turns
it on. **Turn off** (`WEBLOGIN OFF`) stops the page and keeps the login for **Turn on** (`WEBLOGIN ON`); **Forget login**
(`WEBLOGIN FORGET`) removes it. It isn't a `sunshine.conf` key: the host keeps the login, the password only as a salted hash,
in `/data/prosperosunshine/web_login`, and every change applies at once, without a restart. Setting a new login, **Turn
off** and **Forget login** also sign out every browser, those the page remembers included. While the password is typed it
shows as dots; R3 shows it. The page edits the same settings except the Experimental switches that depend on the firmware,
which stay here with their checks.

In every on-screen keyboard (PIN, names, the web login) R2 is **Done** (Options too), R1 switches to symbols, L2 is shift,
Square deletes and Triangle types a space.

The third row, **Experimental**, is for testers on firmware where a function isn't verified. Its tiles come from one list,
`app/src/psapp/experimental_list.h`, which also holds the keys, the names and where each function is verified, for the app and the host
alike. Cross flips a switch; off removes the
key. As with the other settings, **Save and restart host** applies it. Where a function is verified (on 13.00 all of them;
on 10.20 HEVC, HDR and those modes) its tile says "Verified here", doesn't react, and the host ignores the key. The bitrate tile
says "No effect here" on any firmware below 11.00 and behaves the same, because the encoder keeps its own rate there. HDR also offers HEVC, because it is HEVC at 10 bit. `hevc_mode = 1` in
the file beats the HEVC and HDR switches. What testers do and post is in the main README, under
[Testing other firmware](../README.md#testing-other-firmware); what the host does is in `docs/firmware-porting.md` (German). A
function that fails is off until the host restarts. The notice on Home then says, for example, "Experimental HEVC failed on Firmware
13.60 (off until the host restarts). Please post the Diagnostics report on GitHub."

**Diagnostics** collects a report when the page opens, and again on Cross. It says which app, host (with its build's commit) and
firmware run, how the last capture attempt ended (if the system didn't hand over the capture resource: no answer, refused or taken
away, how long the host waited, and what else was active; if the firmware is refused outright, that no switch changes this), which ports
answer, how each file the app and host need looks from the title, which settings differ from the defaults, which Experimental switches
are set, each function's result and code, how each try went (codec, mode, frame rate, bitrate, variant, the step that failed and its
code), the host's `host_diag.log` and the end of
`sunshine.log`. Addresses, device names, the console's name and long ids are taken out, so it can go into a
public issue. It is saved as `/data/prosperosunshine/diagnostics.txt`. While the page is open and the host
runs, a phone or PC in the same network can open it at `http://<console-ip>:47996`: a title may not listen on
the network (bind fails with errno 13), so the host answers there, for a few seconds after each `REPORT`.

## Where a title has no /data

On firmware 9.60 the app's sandbox has no `/data`. The app notices that at its start (`io::sandboxed`) and reaches its files through
the host instead, over the control socket (`src/control/file_bridge.hpp`): only the files it needs, and only from the console itself.
So Settings, Start at boot and the report need a running host, and **Start host** sends the ELF the app carries. The host closes
the app through `/app0/app.quit`.

## Start at boot: how the Payload Manager is set up

The Payload Manager keeps its payloads in `/data/pldmgr/payloads/`, one folder ("slot") each with the
ELF and a `meta.json`, and runs `/data/pldmgr/autoload.txt` from top to bottom at boot (after
`AUTOLOAD_DELAY` seconds, only when `AUTOLOAD_ENABLED=1` in `/data/pldmgr/pldmgr_config.txt`). A line
`!<ms>` waits; any other line names a slot, such as `repo/elfldr@e8cce5d1` or `local/<name>`, whose
ELF the manager sends to the ELF loader on `127.0.0.1:9021`. Absolute paths are accepted only under
`/data/pldmgr` and on USB drives (`slot_validate_payload_path` in its `src/slot.c`), so the host gets
a slot of its own.

Turning **Start at boot** on:

1. copies `/data/prosperosunshine/prospero_sunshine.elf` to
   `/data/pldmgr/payloads/local/prospero-sunshine/prospero_sunshine.elf` and writes a `meta.json`
   beside it (schema 2, a local slot);
2. appends the line `local/prospero-sunshine` to `autoload.txt`, so the host starts after everything
   the list loads before it (kstuff, the ELF loader, ...).

Turning it off removes that line and leaves everything else in the list as it was. The slot stays, so
the host can still be started from the Payload Manager's own page. When the app installs a new host,
it updates the slot's copy too. Each file is written as `<name>.tmp` and renamed over the old one.

Files the Payload Manager saves itself are for its own user only (mode 0600), and a title runs as another
one: `autoload.txt` can then not be read from the app, the way `pldmgr_config.txt` never can. The running host
may, so the app then asks it (`AUTOLOAD`). With the host stopped, the app says to start it first.

## The host's control socket

The app talks to the host on `127.0.0.1:47995` (`src/control/control_server.hpp`), one line per
request:

| Request | Answer |
| --- | --- |
| `STATUS` | `OK running=<0\|1> app=<name> pending=<n>` |
| `PAIRINGS` | `OK <id> <name> <address>` per pending request, then `END` |
| `PIN <id> <pin> [name]` | `OK paired` or `ERR ...`, once Moonlight has finished the handshake |
| `STREAMS` | `OK uuid=<uuid> address=<ip> codec=<H.264\|HEVC\|AV1> hdr=<0\|1> mode=<WxH@fps> kbps=<n> secs=<n> pads=<n> role=<capture\|joined\|starting> name=<name>` per running stream, then `END`. `-` stands for an unknown value. Mode, codec and bitrate are what the capture delivers, the same for every device that joined (`role=joined`); `role=starting` has not reached the capture yet and shows what it asked for. |
| `ENDSTREAM <uuid>` | `OK ended`, `ERR not streaming` or `ERR no such device`, once that device's stream has ended |
| `REPORT` | `OK port=47996`: for 8 s the host answers there with `diagnostics.txt` |
| `AUTOLOAD [ON\|OFF]` | `OK list=<0\|1> manager=<0\|1> slot=<0\|1>` or `ERR ...`: the autoload list, as the host sees it or after its change |
| `CANCELPIN <id>` | `OK cancelled` or `ERR no such request`: denies a pending pairing |
| `WEBLOGIN [SET <user> <password>\|ON\|OFF\|FORGET]` | `OK user=<name> enabled=<0\|1>` (the name empty without a login), `OK set` or `ERR <reason>`, `OK on` or `ERR no login`, `OK off` (the login is kept), `OK forgotten`: the web page's login; the password is the rest of the line |
| `QUIT` | `OK bye`, then the host ends |

The socket serves one client at a time and has no authentication; it is meant for the local network
during development. The app keeps each connection short (a poll sends `STATUS` and `PAIRINGS`, then
`DEVICES`, then `STREAMS`, each on a connection of its own), except for `PIN`, which waits up to 60 s for
Moonlight, and `UNPAIR` and `ENDSTREAM`, which wait up to 20 s for the stream to end.

## Building

Needs Docker. Build the host first (`scripts/build.ps1`), then:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File app/build.ps1
```

The result is `build/app/PPSA47989/` and `build/app/PPSA47989.zip`. `-HostElf <path>` bundles another
host build.

`app/build.ps1` builds the image `prosperosunshine-app:2` (`app/Dockerfile`: Debian with Clang 18, CMake and FFmpeg) and
runs `app/build.sh` in it, with a Docker volume `prosperosunshine-app-cache` for everything that is
fetched or built once:

1. ps5-homebrew-ui at a pinned commit (`kit_rev` in `app/build.sh`);
2. a staged copy of the kit in which `app/src` replaces the kit's own app (its designs, demo and
   `main.cpp`), and `app/sce_sys` its title data. The kit's music and second sound set are left out;
3. the kit's own `make app`: the PS5 payload SDK v0.42, its clean-room `libc.prx`, compile, link
   against ps5-opengl, sign, and the title folder;
4. the host ELF and the licences added to the folder.

The title data:

- `sce_sys/param.json`: a media app (Media row of the home screen), set up the way the console's
  homebrew media apps are (Jelly5): category 65536 with badge 1 and `gameIntent`, and
  `kernel.flexibleMemorySize` of 1 GiB. With the kit's game memory settings
  (`cpuPageTableSize`/`gpuPageTableSize`) a media title does not start (CE-100022-5; the system
  refuses its budget before the process exists). The kit's own checks allow a media app only with
  badge 2 and without `gameIntent`, so `app/build.sh` builds with a game variant of the file and puts
  the real one into the title folder afterwards.
- `sce_sys/icon0.png` (512x512) and `sce_sys/pic0.png` (1920x1080): `app/tools/make-backgrounds.sh`
  turns the picture into `pic0.dds` and `pic1.dds` (3840x2160, BC7, one level) at build time, with
  FFmpeg and a pinned bc7enc_rdo, the way PS5_Vulkan does; the result is cached by the picture's hash.

The one thing the build does not fetch is the **ps5-opengl SDK** (EGL and OpenGL 4.6 for the screen):
its release download is gone. PS5SD builds it from source with its `scripts/build-opengl-sdk.sh`
(about 30 to 60 minutes; see its `docs/BUILDING.md`). `app/build.ps1` copies it into the cache volume
from PS5SD's Docker volume `ps5sd-work` when that exists, or from a folder given with
`-OpenGlSdk <folder>`. The SDK's `manifest.sha256` is checked before every build.

PC tests for the `sunshine.conf` and autoload edits:

```powershell
docker run --rm -v "${PWD}:/repo" prosperosunshine-app:2 bash app/tests/run.sh
```

## Source

| File | What it is |
| --- | --- |
| `src/main.cpp` | The frame loop: display, fonts, pad, sound, the screen. Ends itself when `/data/prosperosunshine/app.quit` appears (for unattended tests). |
| `src/psapp/screen.*` | The three pages, the sheets, the motion. Drawn in three layers: the light behind the page, the page (frosted cards over the first layer), the sheet (over the frosted page). |
| `src/psapp/icons.*` | Line icons and the sun, from the kit's shapes. |
| `src/psapp/host.*` | The worker thread: polls the control socket, starts and stops the host, saves the settings, switches autoload. The screen reads a snapshot of its state every frame and never waits. |
| `src/psapp/conf.*` | `sunshine.conf`: the offered options, their checks, line-preserving edits. |
| `src/psapp/autoload.*` | The Payload Manager's `autoload.txt` and slot. |
| `src/psapp/io.*` | Files and loopback sockets. |
| `sce_sys/` | `param.json`, the icon and the background picture. |
| `tools/make-backgrounds.sh` | The background picture to `pic0.dds` and `pic1.dds`. |
| `tests/` | The PC tests. |

The app logs to the kernel log (klogsrv, port 3232) with the prefix `[PSAPP]`.

## Licence

GPL-3.0-only, like the host. ps5-homebrew-ui is GPL-3.0-or-later; its licence and third-party notices
(fonts, sounds) go into the title's `licenses` folder.
