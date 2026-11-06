# Haptik als Rumble

Spiele für die PS5 steuern den DualSense über einen Audio-ähnlichen Haptik-Kanal und setzen die klassischen Motoren vermutlich nicht
(`pad-output.md`). Moonlight hat dafür keinen Kanal, aber die zwei Motoren. Der Host liest deshalb mit `haptic_rumble = 1` in
`sunshine.conf` (App: „PS5 game vibration“) während eines Streams `sceAvcap2ReadHaptic` und gibt den Pegel als Rumble weiter
(`poll_haptics` in `src/sunshine/platform_ps5.cpp`, `src/platform/ps5/haptic_envelope.hpp`).

## Gemessen auf 9.60

`sceAvcap2OpenHaptic(h**, cfg*0x18, u8)` öffnet mit dem Aufbau des ersten Blocks der Audio-Konfiguration: Größe `0x18`, Art 0, Zweck 2,
Encoder 2, RA-Id, Flag 0. Encoder 0 und 1 geben `0x81950001`. Danach endete der Host dreimal Millisekunden später, ohne dass ein Paket
im Log stand, ohne die Stelle zu zeigen.

Seitdem läuft das Lesen in `ps_haptic_guarded`, und `haptic_attempt` legt `haptic_blocked` an, wenn der Host mit offenem Kanal stirbt.
Solange diese Datei da ist, bleibt der Kanal zu.
`haptic_code.bin` enthält den Code der Bibliothek um `ReadHaptic`.

Der Fehler war die Signatur. Der Code zeigt `sceAvcap2ReadHaptic(h, buffer, header, flags)`: Die Funktion ruft den gemeinsamen Ring-Reader
mit `buffer + 8`, Kapazität `0x800`, `header`, Headergröße `0x18` und `flags`. Mit der Form von `ReadAudio` wurde die Kapazität `0x10000`
zum Header, und der Lesezugriff schlug bei Adresse `0x10000` fehl (Signal 11).

In 2 Minuten 40 Sekunden Uncharted mit `min_log_level = debug` standen Trigger-Blöcke, aber keine Motorwerte im Log.

Danach kamen Daten, auch mit einem virtuellen Pad und mit Encoder 2: 47 Pakete je Sekunde zu 2048 Byte, also Float Stereo, kein Opus
(`opus_decode` meldete ein ungültiges Paket). 256 Frames je Paket ergeben 12 kHz. In den 11 s Uncharted, die `haptic_dump.bin` festhielt,
waren beide Kanäle fast gleich und die Ereignisse Klicks von 20 bis 40 ms, Spitze 0,22, RMS 0,04, durch einzelne Pakete getrennt.

Die erste Fassung nahm Pakete mit winzigen Werten für 16 Bit und meldete daraus einen Pegel von 0,13: ein ständiger Rumble von 80 bis 96
von 255. Laut Besitzer war die Vibration damit spürbar, aber nicht punktgenau.

Moonlight bekommt zwei Motorwerte, also bleibt es eine Annäherung an die Haptik eines DualSense. `HapticEnvelope` nimmt Spitzen statt
Mittelwerte und lässt in etwa 60 ms los; der Anteil unter 120 Hz geht zum großen Motor, der Rest zum kleinen. In einem Gefecht erreichten
die Spitzen 0,77 und der tiefe Anteil 0,49. Mit Quadratwurzel und voller Motorstärke fand der Besitzer das viel zu stark, daher linear,
höchstens 128 von 255, und was länger als eine Viertelsekunde anhält, klingt aus.

`haptic_rumble_strength` (Prozent, Vorgabe 100) skaliert die Motorwerte, `trigger_knock_strength` (Vorgabe 70) die Stärken des Effekts
`0x26`, den Uncharted beim Schießen mit Stärke 7 in allen Zonen schickt. Der Host prüft alle 2 s, ob sich `sunshine.conf` geändert hat;
beides gilt also Sekunden nach dem Speichern, auch im laufenden Stream, ohne Neustart von Host oder Konsole.
