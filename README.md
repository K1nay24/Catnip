# Catnip by KINAI

Ein minimalistischer Chop-Sampler als Plugin für **macOS** (AU, VST3, Standalone) und **Windows** (VST3, Standalone).

Sample reinziehen → Catnip erkennt die Transienten und legt automatisch 16 (oder 8) Chops auf die Pads → mit Maus, MIDI-Keyboard oder Computertastatur spielen.

## Bedienung

| Was | Wie |
|---|---|
| Sample laden | Datei aufs Plugin ziehen, oder Ordner-Symbol oben rechts (WAV, AIFF, MP3, FLAC, OGG) |
| Chops | Automatisch nach Transienten. **GRID** = gleich lange Stücke. **8 / 16** = Anzahl |
| Chop-Grenze verschieben | Im Display auf eine gestrichelte Linie zeigen und ziehen |
| Chop anhören | Im Display auf den Chop klicken oder Pad klicken |
| Tastatur | `1 2 3 4` = Pad 13–16 · `Q W E R` = Pad 9–12 · `A S D F` = Pad 5–8 · `Y/Z X C V` = Pad 1–4 |
| MIDI | Pad 1 = Note 36 (C1), chromatisch aufwärts bis Pad 16 |
| Pro Chop | PITCH (±12 Halbtöne), DECAY (wie viel vom Chop spielt), LEVEL, REVERSE, ONE-SHOT/GATE. Doppelklick auf einen Regler = zurücksetzen |
| In die DAW | Den Button **CHOPS ZUR DAW ZIEHEN** in die Spur ziehen – alle Chops landen als einzelne WAVs |
| Größe | Fenster unten rechts ziehen (75–200 %) |

Das Projekt speichert den Pfad zum Sample, die Chop-Grenzen und alle Einstellungen. Wird die Datei verschoben, zeigt Catnip „DATEI NICHT GEFUNDEN“ – einfach neu laden.

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
