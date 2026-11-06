# Changelog

## Unreleased

### Added

- **Vibration of PS5 games, as rumble (experimental).** PS5 games send their vibration through a haptic channel and leave the classic
  motors alone, so Moonlight got nothing. With **PS5 game vibration** on, the host reads that channel during a stream and passes its
  level on as rumble. It is a rough match for a DualSense on the console, since Moonlight has only two motor values. **Vibration
  strength** and **Trigger knock strength** adjust it, within seconds of saving and without a restart. Tested on 9.60 only.

### Fixed

- **Firmware below 10 gets its own capture record.** It got 13.00's, which 9.60 refuses at `OpenVideo` with `0x81950001` and
  8.40 with `0x81950008` (issue #23). The firmware probe found the size 9.60 opens, `0x2c0`, with frames in H.264 and HEVC up
  to 1440p, and H.264 1080p60 streamed with sound. Its QP sits lower than on 10.x, and now follows the client's bitrate there too,
  so the picture is no longer stuck at the encoder's low default. Not tested on 8.40. Firmware 10.x and above keep their records byte for byte.
- **The app's settings work where a title has no `/data`.** On 9.60 it reported `/data/prosperosunshine` and `/data/pldmgr` as
  missing, because its sandbox has no `/data`. It now reaches the files it needs through the running host, and only those, so
  Settings, the Experimental switches, **Start at boot** and the report work there. The host can also close the app before
  it starts a game. **Start host** sends the host the app carries.

## 0.4.0 (2026-11-06)

### Added

- **A web page to control the host** from a phone or PC, so nobody has to leave the game for the app: the host's status,
  version and firmware, the PIN for a pending pairing, the running streams with "End stream" per device, renaming and
  removing paired devices, the settings (without the Experimental switches, which stay in the app), the end of
  `sunshine.log` and `host_diag.log` with a download, the same diagnostic report as the app's (scrubbed for a public issue),
  and a host restart. It is off until you set a user name and password
  under **Web page** in the app's Settings, and the app can turn it off again. Then it is at `https://<console-ip>:47990`
  (the base port plus one) with the host's own self-signed certificate, so the browser warns once. It answers only the LAN
  and Tailscale (100.64.0.0/10), and comes back after rest mode like the other services. Don't forward 47990 to the
  internet. After 5 failed sign-ins from one address it waits 30 seconds, doubling up to 15 minutes, and after 20 from all
  addresses together every address waits the same way until the next good sign-in; failed sign-ins are
  logged with the address, never the password, and the password is stored only as a salted PBKDF2-SHA256 hash. The control
  socket has `WEBLOGIN [SET <user> <password>|ON|OFF|FORGET]` for it (GitHub issue #21, thanks to @EnriqueWood for two
  rounds of testing on 10.20 and the iPhone). Measured on 13.00, and the page
  was checked in a phone's browser.
- **After a tester's round on 10.20 (PR #27):**
  - "Restart host" on the page no longer has the host send itself to the loader while it is still going away; once on
    10.20 that hung the console. The host hands a small helper to the loader and stops, and the helper starts the host
    once the old process is gone, as the app does.
  - The page has its own certificate (serverAuth, the console's addresses, 825 days, renewed by the host), so iPhones show
    a plain warning instead of refusing. Moonlight's pinned certificate stays as it is.
  - Turning the page off in the app keeps the login for **Turn on**; **Forget login** removes it.
  - Deny a pending pairing, on the page and in the app (`CANCELPIN`): Moonlight hears that the operator cancelled it, and
    the device can ask again right away instead of getting "a pairing session already exists" for 5 minutes.
  - **End stream** on the Devices tab too, and it offers to unpair the device as well; the round trip is coloured (below
    40 ms, up to 80 ms, above).
  - After **Save** the page offers to restart the host, and a banner stays while saved settings aren't applied yet.
    Notices stay at the top of the window.
  - In the app's keyboards R2 is **Done**, and R3 shows the web page's password while it is typed.
  - Clients over ps5-tailscale all arrive as 127.0.0.1, so they share one lockout; the log and the README say so.
  - **End stream** asks once, with three answers: **End and unpair**, **Only end stream** and **Cancel**. It used to ask
    twice in a row.
  - **Remember me on this device (30 days)** on the sign-in, off unless ticked. Such a session outlasts a host restart
    and a closed tab; the host keeps only a hash of its token. Changing or forgetting the login, turning the page off and
    **Sign out** end every remembered session.
  - The video sent is coloured against the bit rate asked for, like the round trip: from 85 % good, from 60 % fair,
    below that poor. A still picture needs less, so the colour waits for the first 10 seconds and says so in its tooltip.
  - The app's home screen counts the paired devices from the running host, as **Manage** does, so a device paired on the
    page shows up there right away instead of after **Reload**.

### Fixed

- **A broken `sunshine_state.json` no longer stops pairing.** After a console hang a tester's file was all zeros; from
  then on the host advertised an empty `uniqueid` and never saved a new pairing again. The file is now written to a
  temporary file, synced and renamed over the old one, so a cut leaves the old or the new file. One that can't be read is
  kept as `sunshine_state.json.corrupt` and the host starts over without pairings (PR #27).
- **An unpaired Moonlight no longer fills `sunshine.log`.** Moonlight asks for `/serverinfo` every few seconds, and each
  failed certificate check logged a warning. Now it is logged once per client certificate, with its address, and then at
  most every 10 minutes.

## 0.3.4 (2026-11-06)

### Fixed

- **"Who's using this controller?" no longer gets stuck.** Some clients announce a controller that never sends anything,
  such as Steam Input's virtual controller next to the real one on Windows, or the on-screen controls on iOS. The console
  asked who uses it, and only that controller could answer, so the dialog stayed on screen and blocked everything. Now a
  guest's controller appears on the console only once it is used (a button, a trigger or a stick pushed a quarter of the
  way), so one that is never used never asks. The host's controller still appears right away (thanks to @Darksk0pe for
  reporting and testing, issue #26).

## 0.3.3 (2026-11-06)

### Added

- **A friend without a controller can play with keyboard and mouse** (thanks to @EnriqueWood, PR #20). While a device has
  no controller, its keyboard and mouse become a controller of its own instead of going to the console's keyboard and mouse, which few games take. If you
  already play with keyboard and mouse on a PC, the controls should feel like home: W A S D move, the mouse turns the
  camera, the mouse buttons are the triggers, Esc pauses, Tab opens the map. It takes a player slot like a controller,
  from the first key or mouse button that is one of its buttons (moving the mouse alone doesn't, so someone who only
  watches takes no slot), and the console asks "Who's using this controller?" unless its device is the host. PS (F1) and
  Create (F2) work for the host only, unless **PS for all players** is on. Moonlight's own shortcuts (Ctrl+Alt+Shift and a
  key) stay Moonlight's. The arrows, Enter, Backspace and Esc walk menus the same in every profile, and holding the key
  right of the space bar (right Alt, AltGr, a Mac's ⌘) gives the menu keys: `standard`'s, with W A S D on the D-pad, which
  every menu takes.
- **Seven profiles, one per way of playing:** `standard`, `action` (third-person and souls-likes: Space jumps, Shift
  dodges, Ctrl walks), `shooter` (Shift runs, C crouches, R reloads, the wheel changes weapon), `driving` (W and S are
  throttle and brake and ease in, A and D steer), `fighting` (W A S D on the D-pad, U I O / J K L the attacks),
  `platformer` (arrows and Z X C) and `sports` (the right stick also on the numeric keypad). Pick one with
  `keyboard_mouse_profile` in `sunshine.conf`; the tables are in the README.
- **A vehicle layer for open worlds:** in `standard`, `action` and `shooter`, Caps Lock turns W and S into throttle and
  brake, A and D into steering and Space into the handbrake, for games where you walk and drive (GTA, Cyberpunk, Far Cry),
  and its light shows when it is on. The other keys and the mouse stay as on foot.
- **The profile by game (advanced, off by default):** with `keyboard_mouse_profile_by_game = 1`, the game in front picks
  the profile from a map of about 3,000 PS4 and PS5 games the host carries, made from Wikidata (CC0) by
  `tools/title_profiles.py`: Gran Turismo 7 drives, Tekken 8 fights, Elden Ring plays as action. With no game running
  (none open, not even suspended behind the home screen) the keys are the menu keys whatever the profile, so W A S D move
  on the home screen.
- **Your own list:** `keyboard_profiles.txt` next to `sunshine.conf` picks the profile per title, for any title ID, also
  homebrew and forwarders (say, your own Mario Kart 8 Deluxe forwarder as `driving`), and holds profiles of your own with
  some keys changed. The host writes your installed games into it as comments, each with the profile it would get.
  `docs/keyboard-profiles.md` has every table and the file's format.
- **A controller connected later takes that player's place quietly.** It gets the same slot and user, so the console
  doesn't ask again; from then on the keyboard and mouse are the console's keyboard and mouse again. If it drops, the
  console says so, and the next key takes a controller again. A second controller on that device is a new player.
- **The mouse sensitivity, set while playing:** Ctrl+Alt and the wheel turn it up or down a step, from 10 % to 1000 %.
  Nothing shows on the console, which everyone streaming would see. The host keeps it per device and profile, as it
  depends on the device and the game.
- **Settings in `sunshine.conf`:** `keyboard_mouse_pad = 0` keeps the old behaviour, `keyboard_mouse_profile` picks the
  profile, `keyboard_mouse_sensitivity` (in percent, 100 by default) is where a new device's mouse starts. Not in the app
  yet.
- **A firmware probe for firmware nobody has measured yet** (thanks to @EnriqueWood, PR #25). `firmware_probe.elf` in the
  release archive collects in about 2.5 minutes what the host needs on a new firmware: the capture record, the encoder's
  quality fields, which modes and codecs open. It never runs during a stream, stops the host for its run and starts it
  again, uploads nothing, and masks addresses in its report (`/data/prosperosunshine/firmware_probe/report.txt`). Send it
  to the ELF loader (port 9021) when asked to in an issue; `docs/firmware-probe.md` has the details.

### Fixed

- **Surround sound when a wider stream joins** (thanks to @BuffMcBigHuge, PR #16). If a stereo stream opened the audio
  channel first and a 5.1/7.1 client joined later, the reader now reopens the 12-channel bed instead of leaving the new
  client with stereo in the front pair.
- **Malformed audio packets are dropped.** A packet whose size does not fit the channel count is no longer split into the
  wrong number of frames.
- **The host no longer crashes when another program holds a stream port.** If UDP 47999 (the control port) was taken,
  Sunshine's code read a null pointer while setting up the stream and the whole host went down; Moonlight showed "RTSP
  handshake failed" (issue #24, firmware 11.40). Now the stream fails cleanly, the host keeps running, and `sunshine.log`
  says which port is busy and why (`ENet couldn't open UDP port 47999: Address already in use`).

## 0.3.2 (2026-11-05)

### Changed

- **The host role belongs to a device, and guests lose Create too.** Until now the controller in the first slot played as
  the console's user and had PS, so when its device left, whoever got that slot next took both over. Now the host is the
  device whose controller came first while none was connected. When it leaves, nobody inherits the role, and its next
  controller takes it back, also after a reconnect; once every controller is gone, the next device is the host. Guests can
  no longer press Create either, since its menu captures and broadcasts what the console shows. **PS for all players**
  in the app's Settings still gives both to everyone (GitHub issue #15).

### Fixed

- **Firmware 13.40: the capture opens.** 13.40 refused 13.00's capture record at `OpenVideo` (`0x80c70010`), as 13.60 did
  before 0.1.5. It now gets 13.60's record, as does any version between 13.40 and 13.60. A tester confirmed it on 13.40
  with a test build of this change: `OpenVideo` returns 0 and Moonlight on Android shows the picture with H.264 (issue #14).
  Not tested here. Firmware 13.00 and every other version keep their record byte for byte.

### Known limits

- **The slice fields from 0.3.1 do nothing on 10.20.** A contributor measured it on a PS5 Pro: the console accepts them and
  still cuts every 1080p H.264 frame into 34 slices, so Moonlight on Windows with hardware decoding still fails on H.264
  there. The fallback doesn't help either, since nothing is refused. Use HEVC on 10.x; it has 17 slices and decodes in
  hardware. Where 10.x takes the slice count is being looked into.

## 0.3.1 (2026-11-04)

### Added

- **Surround sound, as an experimental switch.** Turn on **Surround** under **Experimental** in the app's Settings, and a
  Moonlight that asks for 5.1 or 7.1 gets the console's multichannel sound instead of stereo. Which of the console's channels
  goes to which speaker is not verified yet: the console here outputs stereo, so in every test only front left and right
  carried sound. With a 5.1 or 7.1 system you can help: the host logs the level of each of the console's 12 channels every
  30 seconds. Off by default; a client that asks for stereo, or a second device next to a surround one, gets stereo as before.

### Changed

- **H.264 on firmware 10.x asks for 4 slices per frame.** 10.x cut every 1080p frame into 34 slices, more than the hardware
  decoders on Windows take (a white picture there). The host now sets the slice fields Remote Play uses. If the firmware
  refuses them, the host opens the stream without them as before and leaves them out until it restarts. Not tested on 10.x
  yet: `host_diag.log`, and with it the app's Diagnostics report, says `first frame has N slices`, and a report with that
  line helps.
- **The Experimental row has room for six switches.** "HDR (10 bit)" and "150 Mbit/s" are the shorter names of two of them.

## 0.3.0 (2026-11-04)

### Added

- **Only player 1 can press PS.** A friend can no longer leave your game, open the control center or reach the console's
  settings by accident (or on purpose): the PS button works only for the controller that plays as the console's user. Every
  further player's PS press is ignored, also the "hold Select" PS on a phone. The new setting **PS for all players** in the
  app's Settings (off by default) gives it back to everyone.
- **The app shows what really reaches each device.** In the list of streams, every device shows the bitrate it actually
  gets, the round-trip time to it, and how often it lost a picture and had to ask for a new one, so you can see who has a
  weak connection. The control socket's `STREAMS` has them as `sent_kbps`, `rtt_ms` and `recoveries`.
- **A section on playing over the internet** in the README: which ports to forward, the bitrate, and that a friend who asks
  for 720p gets a capture channel and bitrate of their own.

### Changed

- **A friend who joins sees the picture sooner.** The keyframe a joining device needs went out before its client was ready
  for it, so the picture started only when Moonlight asked again after 2 seconds. The host now sends two more, and the
  picture comes after about 0.4 s.
- **Taking a channel over no longer pauses the picture.** When the device holding a capture channel leaves, the next one
  now takes the open channel as it is, instead of closing and reopening it (a pause of about 185 ms).
- **The settings page has two rows of four,** to make room for the new setting. The encryption settings are now called "LAN
  encryption" and "Internet encryption".

### Tested

- **Several controllers on one device** are separate players: measured with the test client, and a contributor played with
  two controllers on one Moonlight client on 10.20.

## 0.2.4 (2026-11-04)

### Added

- **The app shows who is streaming, and can end one device's stream.** While someone streams, the home page has a
  **Streaming now** card: each device with its name, codec and HDR, resolution and frame rate, bitrate, how long it has
  been streaming and how many controllers it has, and in its list also the address. Select a device there to end its
  stream, after a confirmation; the game and the other devices' streams go on, and the device stays paired. For a device
  that joined another's stream, it shows the picture it really gets, not what its Moonlight asked for. The control socket
  has two new commands for it, `STREAMS` and `ENDSTREAM <uuid>`.
- **Each device can get its own codec, dynamic range or resolution.** A second device that asks for another codec, HDR
  where the first has none (or the other way round), or another resolution up to 1080p now gets a capture channel of its
  own next to the first, instead of the first stream's mode or a refusal. A device that asks for the same mode as a running
  stream still shares its picture. There are at most two channels; a third device joins the one with its codec and dynamic
  range. `STREAMS` tells which channel each stream uses (`channel=0|1`).

### Changed

- **Paired devices marks every device that streams.** With two players, the list showed only the device that started or
  joined last as streaming.

### Fixed

- **HDR streams tell the client their colour space.** The host sent the HDR metadata as zeros, and Moonlight for Windows
  passes these on to the display, where a tester saw oversaturated colours with hardware decoding. The host now sends the
  usual HDR10 values (BT.2020 primaries, D65 white point, 1000 nits peak), as Sunshine does on Windows. Measured with the
  test client; whether the colours on a Windows HDR display are right is not checked yet.

## 0.2.3 (2026-11-04)

### Changed

- **A stream that takes the capture over keeps the running mode.** When the device holding the capture left, the next one
  reopened it in its own mode, so everyone could jump from 720p to 1080p and a friend on a mobile link got twice the data.
  Now the takeover keeps the resolution, frame rate and bitrate everyone had.
- **A device that can't join is told so before its stream starts.** While a stream runs, the host offers new devices only
  its codec and dynamic range, so Moonlight picks them on its own. A device whose Moonlight still asks for another dynamic
  range is refused at the start with a message Moonlight shows ("Another device is streaming without HDR. Turn HDR off in
  Moonlight to join."), instead of "error code -1" after its stream had started. A client set to H.264 is still turned
  away from an HEVC stream once it has started, since Moonlight can't be made to pick HEVC.

### Fixed

- **Starting or closing a game no longer drops a stream below 1080p to 1080p.** On 10.20 the console's output has no mode
  for 1.6 to 2.1 s while a title that sets its own output (Black Myth: Wukong) starts or closes, and the capture refuses
  every mode until it has one. A reopen gave up after about 1.2 s and stayed at 1080p for the rest of the stream, which a
  PS Vita can't show: its picture froze while the controls kept working. Now a reopen in a running stream keeps a mode
  below 1080p while the output isn't settled, for up to 6 s, and each reopen asks for the client's mode again. A mode the
  system really refuses still falls back to 1080p. The first open of a stream and everything from 1080p up stay as they
  were. Measured on 10.20 with a PS Vita and with Moonlight for Windows at 960x544, 1080p and 4K.
- **A slow grant of the capture no longer fails the stream.** The host gave the console 2 s to grant the capture, though
  Moonlight waits 10 s for the first picture. A tester on 13.60 saw the grant take longer twice in a row, and the stream
  failed. Now the host waits up to 6 s, and the log notes a grant that took half a second or more.

## 0.2.2 (2026-11-03)

### Added

- **A second player from another device.** A stream from another device no longer gets turned away: it joins the running
  one and gets the same picture and sound, in the codec and dynamic range the first stream opened. Its controller is new to
  the console, which asks "Who's using this controller?", so the friend picks a user or plays as a guest (Add User, Quick
  Play). Every controller after the first is asked this way. When the stream holding the capture ends, a joined one takes it
  over. While another device streams, quitting the app in Moonlight ends only your own stream, so a friend who leaves
  doesn't close your game; the last one to quit ends the app as before. Measured on 13.00 with two test clients at once,
  with H.264 and HEVC, a guest signed in through the second one; a game for two players isn't tested yet
  ([#11](https://github.com/Darkatek7/ProsperoSunshine/issues/11)).

### Changed

- **A second controller on the same client is another player too.** Until now every controller played as the console's
  user. Now only the first does, and the console asks who uses each further one.
- **Waking from Moonlight is documented as not possible.** The PS5 ignores Wake-on-LAN packets, and the console stops the
  host in rest mode, so it can't wake the console either. A Remote Play client such as chiaki-ng can
  ([#10](https://github.com/Darkatek7/ProsperoSunshine/issues/10)).

## 0.2.1 (2026-11-03)

### Added

- **Keyboard and mouse:** Moonlight's keyboard and mouse now reach the console as a virtual keyboard and mouse, so you can
  play with them from Windows. They are added the first time you press a key or move the mouse in a stream and removed when
  the stream ends. Keys move the focus in the console's menus and type into text fields while the on-screen keyboard is open,
  Shift included; text sent from a phone's keyboard is typed as on a US keyboard. The mouse moves the pointer and clicks in
  games that support a mouse, measured in Planet Zoo; the console's browser doesn't use one. Measured on 13.00 with the
  test client, which got `--kbm-seq` for this, and used through Moonlight Web. Wheel, the right and middle
  buttons and firmware 10.20 are not tested yet.

### Changed

- **A new stream from the same device replaces its old one.** The console can only capture one stream, so until now a
  second start was turned away while the first still held the capture. A client that retries when its first attempt
  hangs, like Moonlight Web behind a strict proxy, then waited half a minute or more until the old stream timed out. Now the
  host ends the old stream and the new one takes over within a few milliseconds. A stream from another device is still
  turned away.

## 0.2.0 (2026-11-03)

### Added

- Modes below 1080p on firmware 10.20 and 13.00 (thanks to @EnriqueWood): a client asking for less than 1080p gets whichever of 1280x720, 960x540 and 640x360 is
  nearest to its request, instead of 1080p to scale down, so weak clients and Wi-Fi links have less to carry and decode (a PS
  Vita asking for 960x544 gets 960x540; 1600x900 still gets 1080p). Requests from 1080 lines up get exactly what they got
  before, on every firmware. Measured on 10.20: all three start in H.264, HEVC and HEVC 10 bit at 30 and 60 fps; at the QP the
  host sets for 16 Mbit/s, an IDR of the home screen cost about two thirds of 1080p's at 720p, 40 % at 540p and 20 % at 360p,
  and Moonlight on an iPhone got each mode it asked for. If the system refuses one, the stream falls back to 1080p, as 1440p
  and 4K do, so the client gets what it got before these modes rather than no stream. Elsewhere the new Experimental switch
  `experimental_small_modes` offers them; there a mode that is refused before its first picture also stays off until the host
  restarts.
- On 13.00 these modes needed two more things, measured there: HEVC gets a smaller frame budget per mode (`cfg+0x10c`), and the
  bitrate is capped per mode, because 13.00 won't start them with too many bits per frame: 30 Mbit/s for 360p, 60 for 540p and
  120 for 720p, half of that at 30 fps. A client asking for more gets that much, and the small picture stays. Below 1080p,
  `OpenVideo` is repeated 5 times instead of 25, as above 1080p, so that the fallback to 1080p still fits in the time a client
  waits.

## 0.1.9 (2026-11-02)

### Added

- **Paired devices in the app:** the tile on the home page opens a list. Each device shows the mode it last asked for, like
  `1280x720@60`, or that it is streaming now, and it can be renamed or removed, for example a friend's device that shouldn't
  stream any more. Removing asks first and also ends that device's running stream. The list stays in the app, nothing of it goes
  into the diagnostic report. The control socket has the new commands `DEVICES`, `RENAME <uuid> <name>` and `UNPAIR <uuid>`.

### Changed

- Pairing asks for a name after the PIN, already filled in with "Moonlight 1", "Moonlight 2" and so on. Moonlight sends the same
  name from every device, `roth`, so every paired device used to show up as that. A client that sends its own name keeps it as the
  suggestion. Devices already paired keep their names.

## 0.1.8 (2026-11-02)

### Added

- **Quit closes the game**, a switch in the app's Settings (`close_game_on_quit = 1` in `sunshine.conf`), off by default: quitting
  in Moonlight (`/cancel`) closes the game the stream started or found in front, through the system's own close as from the home
  screen, and unsaved progress is lost. After the stream the game stays listed as running while it is in front, as Sunshine on a
  PC keeps its app, so Moonlight offers Resume and Quit and asks to quit it before starting another game. Only `/cancel` closes it, never a stream's end or the host's shutdown. Starting the same
  game again right after waits until ShadowMountPlus has released it. Measured on 10.20 with Moonlight for PC, Android and
  iOS: resume, quit and switching games work, and a close took about 1 s. On 13.00 the same with a test client, not yet with
  Moonlight: a close took 0.9 s in GTA III and 2.3 to 3.1 s in the Mario 64 demo.

### Changed

- Firmware 10.x: the picture is no longer blocky. The encoder there takes no bitrate and was left at its own QP, which sent
  0.3 to 0.4 Mbit/s in a moving game whatever Moonlight asked for. Its QP now follows the client's bitrate: 30/34 (IDR/other
  frames) below 8 Mbit/s, 27/31 from 8, 24/28 from 12 and 21/25 from 20. Measured on 10.20 in GTA III, driving fast, HEVC
  10 bit at 1080p60: about 2, 2.4, 3 and 5.3 Mbit/s on average, with a busiest second of 3.1, 3.7, 5.1 and 10 Mbit/s. The
  encoder still doesn't hold the bitrate set in Moonlight, and busier games will send more.
- The diagnostics report no longer names what you play: game titles and title IDs show as `<title>`. It also no longer holds the
  console's local user ID, which the host's log line for the virtual pads gave away.
- The app's Settings page has a new layout, to fit the new switch: four tiles above three.

### Fixed

- An empty `/mnt/sandbox/<title>_000`, left behind when ShadowMountPlus missed a title's exit, no longer counts as an open
  title. It made the check for a game open in the background refuse every other start with 409.

### Known limits

- Without `close_game_on_quit`, quitting a game from Moonlight for PC's app list, also by picking another game and confirming,
  sends nothing to the host: it reports no running game once the stream has ended, so Moonlight skips its `/cancel` and waits at
  "Quitting…". Other clients are untested.
- The new QP on firmware 10.x is measured only on 10.20 and with HEVC. On 10.40 and with H.264 it is untested.
- The 13.00 tests of the switch and of closing a game used a test client. Moonlight's own Resume and Quit menus were only tried on 10.20.

## 0.1.7 (2026-11-01)

### Added

- Firmware 10.x at 1080p, H.264, and on 10.20 also HEVC (8 bit and HDR): the capture record there is smaller, the encoder
  keeps its own default bitrate, and HEVC starts with `cfg+0x24` alone. Measured on a 10.20 console (issue #2): Moonlight
  streams on Android and iOS (tested with H.264) and on Windows (H.264 and HEVC, also HDR). H.264 there comes as 34 slices per
  frame, more than FFmpeg's hardware decoders on Windows take (32), so Moonlight on Windows needs software decoding for it; HEVC
  comes as 17 and decodes by hardware. Other 10.x versions are untested and get H.264 only.
- An **Experimental** section in the app's Settings, for testers on firmware where a function isn't verified: one switch each for
  HEVC (8 bit), HDR (HEVC 10 bit), 1440p and 4K, and a bitrate cap of 150 instead of 50 Mbit/s, all off by default. They go to
  `sunshine.conf` as `experimental_hevc`, `experimental_hdr`, `experimental_large_modes` and `experimental_high_bitrate`. On verified
  firmware (13.00; 10.20 for HEVC and HDR) a switch shows as not needed and changes nothing. The bitrate switch has no effect on any
  10.x, whose encoder keeps its own rate, and says so. HEVC tries `cfg+0x24` alone first, then 13.00's full setup. A function whose every
  try fails is off until the host restarts, and the host offers H.264, 1080p and 50 Mbit/s again: a lower rate or mode takes over in the
  same stream, while a stream that failed on HEVC or HDR ends and the next one works. The
  diagnostics report lists the switches, each function's result and code, and every try (codec, mode, variant, the step that failed and
  its code), and the Home
  page asks for it to be posted, also after a success. None of this has run on a console yet. The switches come from one list,
  `app/src/psapp/experimental_list.h`, so a new lock starts with one entry there.
- `host version=` in `host_diag.log` and the diagnostics report name the build's commit, so a test build can be told from the
  release. Without git data it says `unknown`.
- When the wait for the capture resource ends without it, `host_diag.log` and the report say which case it was: no answer within
  2 seconds, refused (with the value) or taken away, how long it waited and which resources were active. The report also says outright
  when the firmware is refused because the ResourceArbitrator calls can't be found, which no switch changes.

### Changed

- The bitrate cap is 150 Mbit/s on firmware 13.00, up from 50, and stays at 50 elsewhere. Measured with a test client on wired LAN
  and a noise image at 4K: HEVC follows the setting up to about 151 Mbit/s with no missing frames. Not yet tested with Moonlight or
  in a game, and games may stay well below the setting.
- Firmware 10.20 counts as verified: the app no longer calls it untested, and the host knows its ResourceArbitrator offsets.
  Moonlight on Android, iOS, Windows and Nintendo Switch streamed from a 10.20 console.

### Fixed

- Game audio no longer stays silent after the capture is reopened, as happens when a title starts during a stream. The
  microphone read the session's generation before taking the lock that opens its channel; a reopen in between left the new
  channel marked as old, and every later open failed on it until the capture was reopened again or the stream ended.
- Game audio also comes when the capture isn't available as the stream starts, as can happen while the game picked in
  Moonlight starts. The stream used to stay silent to its end; the audio now retries every half second, as after a later
  loss. Not yet seen on the console.
- Picking a game in Moonlight while another one is in front now fails the launch instead of reporting success. The host still
  doesn't close the game in front; before, Moonlight named the game it asked for while the stream showed the other one. "PS5
  Screen" streams whatever is in front.
- A game picked in Moonlight no longer starts while another one is still open in the background. A game opened before the
  settings app stays open behind it, and once the app has closed, the system no longer reports it as running. Starting another
  title could close it, so the launch fails with 409 as with a game in front. The host reads this from `/mnt/sandbox`.
- A start refused while ShadowMountPlus still releases a title (`0x80940033`, about a second after the settings app closed
  itself, seen once on firmware 10.20 with ShadowMountPlus 1.7beta11) is retried for up to 3 seconds when nothing else is
  open. The retry itself is not yet seen working: in the next try the first start went through.

### Known limits

- The Experimental switches have only run on firmware 13.00, where they change nothing. On other firmware they are untested.
- Firmware 10.40 is unconfirmed. One report from 0.1.6 failed waiting for the capture resource, and the 10.x change has not been
  confirmed to run there.
- Rest mode with a game open and a stream running can still leave the host unreachable until it is restarted. End the stream first.

## 0.1.6 (2026-11-01)

### Added

- The console's installed games appear in Moonlight's app list with their covers, after "PS5 Screen". Picking one starts it.
  The host never closes a game: quitting in Moonlight ends the stream only, and with another game in front the new one doesn't
  start.
- HDR: HEVC Main 10 in BT.2020 and PQ when the client asks for it, up to 4K. Measured with the test client and seen on one real
  setup, Moonlight on an HDR TV at 4K in one game. The host sends no HDR metadata, because the capture provides none. `hevc_mode = 2`
  in `sunshine.conf` turns HDR off.
- Touchpad and motion sensors of the Moonlight controller reach the console's virtual pad. Measured as a title reads them; not yet
  in a game that uses them.

### Changed

- The capture check that told users to turn HDR off was about PS VR, as the system's own names for those bits show. The app's message
  now says so.

### Known limits

- Rest mode with a game open and a stream running: in one test the console and the game came back, but the host stayed unreachable
  until it was restarted. End the stream before rest mode.
- Waking the console from Moonlight did not work in the one try, from a PC in the same network.

## 0.1.5 (2026-11-01)

### Added

- HEVC at 1440p and 4K on firmware 13.00, when the client asks for it. The console's fallback to 1080p in some titles applies
  here too. Measured on the home screen, in a quiet game scene and with noise in the console's browser; not yet during active play.
- First attempt at firmware 13.60, for H.264 at 1080p: the capture record there is larger. The layout comes from public projects
  that run on 13.60, and it's untested here, because no 13.60 console is available.

### Fixed

- HEVC no longer skips frames after a key frame or an oversized frame: the frame budget is now as large as the encoder accepts, and
  the bitrate still holds. Measured longest gap under load: about 19 ms, was 33 to 37 ms.
- A 1440p or 4K request falls back to 1080p also when opening or starting the capture fails, not only when the console refuses the
  resource. Others report this failure for a console output smaller than the request, which ended the stream. A real smaller output
  is not tested here.

## 0.1.4 (2026-10-31)

### Added

- H.264 at 1440p and 4K on firmware 13.00, when the client asks for it. HEVC stays at 1080p. If the console refuses the larger
  capture, which two titles did, the stream runs at 1080p instead of failing, also when such a title starts during a stream.
  Measured on the home screen, in quiet game scenes and with noise in the console's browser; not yet during active play at 4K.

### Fixed

- HEVC no longer pauses after each key frame. The encoder skipped frames to make up for it: one per second at 15 Mbit/s,
  100 ms per second at 5 Mbit/s. Key frames now come every 10 seconds instead of every second, for both codecs, and clients
  still get one right away after packet loss.
- Opening the capture is repeated for about a second when the service doesn't list the host's resource as active, seen on
  firmware 10.40. If it keeps failing, the diagnostic report lists the active resources. Whether this helps on 10.40 is not known.
- A stream no longer fails when the capture resource is still listed right after the last session closed it.
- Docs: the host does give paired clients the console's MAC address. Whether Wake-on-LAN wakes the console is untested.

## 0.1.3 (2026-10-31)

### Added

- Diagnostics page in the app: a report for bug reports, without addresses, device names or ids. Read it on the console,
  open it from a phone or PC at `http://<console-ip>:47996` while the page is open and the host runs, or fetch
  `/data/prosperosunshine/diagnostics.txt`.
- The home page says when the firmware is untested, and why capture failed: HDR on, capture in use, firmware not
  supported, or the step that failed.
- The host writes its steps to `/data/prosperosunshine/host_diag.log`, which survives a crash.
- Adaptive triggers: effects a game sends reach a DualSense on Moonlight for PC. Other clients ignore them. If a stream
  ends during an effect, the trigger stays stiff until the next connection, because Moonlight doesn't reset it.

### Changed

- After rest mode the host starts its services again by itself instead of staying unreachable. This worked in two tests on
  the home screen, with no game open and no stream running. An earlier test without this ended in a hung console, for
  an unknown reason, so to be safe, turn the console off instead.

### Fixed

- "Start at boot" failed with "Cannot read the Payload Manager's autoload.txt" once the Payload Manager had saved that
  file itself, for root only. The host now makes the change when the app may not, so the host has to run for it.

## 0.1.2 (2026-10-31)

### Added

- Rumble: classic two-motor vibration from the console reaches the Moonlight client, including short pulses.
- Light bar: the colour a title sets on the controller reaches the Moonlight client.
- HEVC (8-bit, firmware 13.00): clients that support it can stream HEVC. `hevc_mode = 1` in `sunshine.conf` turns it off.

### Changed

- Bitrate cap raised from 15 to 50 Mbit/s, for H.264 too. The encoder now opens at the client's bitrate instead of being
  raised afterwards, which is what stopped it above 15 Mbit/s.

## 0.1.1 (2026-10-31)

### Added

- PS button for clients without one, such as Moonlight's on-screen controller: hold Select for half a second. A short
  press sends Create on release. `back_button_timeout` in `sunshine.conf` sets the time; -1 turns it off.

### Fixed

- README: a second ELF does not restart the host, it exits and the old host keeps running.

## 0.1.0 (2026-10-30)

First release. Tested on PS5 firmware 13.00; other versions are untested.

### Added

- Sunshine-compatible host as an ELF payload: pairing, 1080p H.264 video from the console's encoder, game audio, and
  a virtual DualSense per Moonlight gamepad.
- The stream follows the client's frame rate (30 or 60 fps) and bitrate (up to 15 Mbit/s). Other resolutions are
  delivered as 1080p; Moonlight scales them.
- The console announces itself over mDNS, so Moonlight lists it without entering an address (confirmed on iPhone).
- Control socket for status and PIN entry, bound to the console itself by default.
- PS5 app `PPSA47989` for starting and stopping the host, autoload, settings and PIN entry.

### Known limits

- H.264 SDR 4:2:0 only: no HEVC, AV1 or HDR.
- One stream at a time.
- Rest mode is untested.
