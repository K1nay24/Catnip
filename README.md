# Catnip by KINAI

Ein minimalistischer Chop-Sampler als Plugin für **macOS** (AU, VST3, Standalone) und **Windows** (VST3, Standalone).

Sample reinziehen → **TAP** drücken (oder Leertaste) → beim Mithören auf die Pad-Tasten tippen. Jeder Tipp wird ein Chop. Danach zieht **SNAP** die Chops Stück für Stück auf den passendsten, stärksten Schlag in der Nähe.

## Bedienung

| Was | Wie |
|---|---|
| Sample laden | Datei aufs Plugin ziehen oder ins leere Display klicken (WAV, AIFF, MP3, FLAC, OGG) |
| **TAP** / Leertaste | Macht alles stumm und spielt den sichtbaren Teil ab. Jede Pad-Taste (oder Pad-Klick) setzt dort einen Chop. Der erste Tipp ersetzt die alten Chops. Endet nach 16 Chops, am Ende des Samples oder mit TAP/Leertaste |
| **SNAP** (Regler) | 0 % = Chops genau dort, wo getippt wurde. Aufdrehen zieht jeden Chop allmählich zum besten Schlag in der Nähe (bis 150 ms zurück, 40 ms nach vorn; laute Schläge zählen mehr, nähere auch). ▲ unten im Display = Ziel, gestrichelt = Tipp |
| **AUF SCHLAG** | SNAP auf 100 %, nochmal = zurück auf den vorherigen Wert |
| Chop verschieben | Im Display eine Chop-Linie anfassen und ziehen (SNAP zieht weiter mit) |
| **PITCH** | Gilt fürs ganze Sample, in Halbtönen (SHIFT beim Drehen = Cent). Wie bei der MPC ändert sich das Tempo mit – das Display zeigt das neue BPM, das du in der DAW einstellst/aufnimmst |
| **DAW SYNC** | AN: das Sample läuft immer im Tempo der DAW (Time-Stretch, bei großem Abstand halbes/doppeltes Tempo). PITCH ändert dann nur noch die Tonhöhe |
| **ZOOM** | In den Ausschnitt um den gewählten Chop hineinzoomen (auch Mausrad über dem Display). TAP und PLAY spielen nur den sichtbaren Teil |
| **VOLUME** | Gesamtlautstärke |
| REVERSE / GATE | Für den gewählten Chop: rückwärts / spielt nur solange gedrückt |
| PLAY / STOP | Sichtbaren Teil anhören / alles stoppen (auch ESC) |
| UNDO / CLEAR | Letzte Änderung zurück (auch Cmd/Strg+Z) / alle Chops löschen |
| **ZUR DAW** | Taste in eine Spur ziehen: alle Chops als WAVs, mit Pitch, Sync und Reverse. Klick = Ordner öffnen. Der Dateiname enthält das BPM |
| Tastatur | `1 2 3 4` = Pad 13–16 · `Q W E R` = Pad 9–12 · `A S D F` = Pad 5–8 · `Y/Z X C V` = Pad 1–4 |
| MIDI | Pad 1 = Note 36 (C1), chromatisch aufwärts bis Pad 16 |
| Abspielen | Ein neuer Chop schneidet den vorherigen ab – Chops überlappen nie |
| Regler | Ziehen hoch/runter, Doppelklick = zurücksetzen |
| Größe | Fenster unten rechts ziehen (60–150 %) |

Das Projekt speichert den Pfad zum Sample, die Chops und alle Einstellungen. Wird die Datei verschoben, zeigt Catnip „DATEI FEHLT“ – einfach neu laden.

**Tastatur in der DAW:** Einmal ins Plugin-Fenster klicken, damit es die Tasten bekommt. Manche DAWs fangen Tasten trotzdem ab (z. B. Ableton, wenn „Computer-MIDI-Keyboard“ aktiv ist) – dann dort ausschalten oder ein MIDI-Keyboard nutzen.

## Installieren (fertige Builds)

**macOS**
- `Catnip.component` → `~/Library/Audio/Plug-Ins/Components/`
- `Catnip.vst3` → `~/Library/Audio/Plug-Ins/VST3/`
- `Catnip.app` → Programme

Die Builds sind nicht von Apple notarisiert. Beim ersten Mal einmal im Terminal ausführen:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Catnip.component ~/Library/Audio/Plug-Ins/VST3/Catnip.vst3
```

**Windows**
- Ordner `Catnip.vst3` → `C:\Program Files\Common Files\VST3\`
- `Catnip.exe` ist die Standalone-Version.

## Selbst bauen

Voraussetzungen: CMake ≥ 3.22 und ein Compiler (macOS: Xcode, Windows: Visual Studio 2022 mit „Desktopentwicklung mit C++“). JUCE wird automatisch geladen.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Ergebnis: `build/Catnip_artefacts/Release/`

**Ohne eigenen Compiler:** Projekt in ein GitHub-Repository hochladen. Der Workflow `.github/workflows/build.yml` baut automatisch für macOS und Windows; die fertigen Plugins liegen danach unter *Actions → letzter Lauf → Artifacts*.

## Tests

```
cmake -S . -B build -DCATNIP_BUILD_TESTS=ON
cmake --build build --target CatnipTest CatnipEngineTest
./build/CatnipTest_artefacts/Release/CatnipTest
./build/CatnipEngineTest_artefacts/Release/CatnipEngineTest
```

## Lizenzen

- **JUCE 8**: kostenlos über die „Starter“-Lizenz, solange der Umsatz mit JUCE-Produkten unter 20.000 USD/Jahr bleibt; darüber Indie/Pro-Lizenz nötig (oder Veröffentlichung unter AGPLv3). Vor dem Verkauf die aktuelle JUCE-EULA prüfen.
- **Schriften**: VT323 und Archivo, beide SIL Open Font License (`resources/OFL-*.txt`).
- **VST** ist eine Marke der Steinberg Media Technologies GmbH.
