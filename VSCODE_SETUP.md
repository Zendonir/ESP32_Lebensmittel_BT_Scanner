# VS Code Integration - Komplette Anleitung

Dieses Projekt ist vollständig für VS Code mit PlatformIO vorbereitet.

## 🚀 Schnellstart (3 Schritte)

### 1. **VS Code Installieren**
```bash
# Linux/Ubuntu
sudo apt install code

# macOS
brew install visual-studio-code

# Windows
# Download: https://code.visualstudio.com
```

### 2. **PlatformIO IDE Extension Installieren**
1. VS Code öffnen
2. Extensions (Ctrl+Shift+X) öffnen
3. Nach "PlatformIO" suchen
4. "PlatformIO IDE" installieren (von PlatformIO)
5. VS Code neu starten

### 3. **Projekt Öffnen**
```bash
code ESP32_Lebensmittel_BT_Scanner
# oder Workspace:
code ESP32_Scanner.code-workspace
```

---

## 📁 VS Code Struktur

Die `.vscode/` Ordner enthält:

```
.vscode/
├── settings.json              ← Editor & C++ Einstellungen
├── extensions.json            ← Empfohlene Extensions
├── tasks.json                 ← Build/Upload/Monitor Tasks
├── launch.json                ← Debug Konfiguration
├── c_cpp_properties.json      ← IntelliSense & Compiler
└── .c_cpp_database            ← Cache (auto-generated)
```

---

## ✨ Features

### ✅ **IntelliSense & Autocomplete**
- C++ Standard Library
- Arduino Framework API
- ESP32 SDK
- LVGL API
- Custom Projekt-Code

### ✅ **Build & Upload**
- One-Click Kompilierung
- Automatisches Upload
- Serial Monitor Integration
- Error-Highlighting

### ✅ **Debugging**
- GDB Integration
- Breakpoints
- Variable Inspection
- Stack Trace

### ✅ **Git Integration**
- GitLens (Blame, History)
- Commit/Push von VS Code

---

## ⌨️ Keyboard Shortcuts

| Shortcut | Funktion |
|----------|----------|
| **Ctrl+Shift+B** | Build (Kompilieren) |
| **Ctrl+Alt+U** | Upload (PlatformIO) |
| **Ctrl+Alt+S** | Serial Monitor (PlatformIO) |
| **F5** | Debug starten |
| **Ctrl+K Ctrl+0** | Fold all |
| **Ctrl+K Ctrl+J** | Unfold all |
| **Ctrl+/** | Toggle Comment |
| **Ctrl+Shift+P** | Command Palette |

---

## 🔧 Tasks Ausführen

### Via Command Palette (Ctrl+Shift+P)
```
Tasks: Run Task
```

Verfügbare Tasks:
1. **PlatformIO: Build** - Kompilieren
2. **PlatformIO: Upload** - Board hochladen
3. **PlatformIO: Monitor** - Serial Monitor (115200)
4. **PlatformIO: Upload and Monitor** - Upload + Monitor
5. **PlatformIO: Clean** - Build Cache löschen
6. **PlatformIO: Full Clean** - Alles löschen & neu bauen

### Via Terminal
```bash
# Build
platformio run

# Upload
platformio run -t upload

# Monitor
platformio device monitor -b 115200

# Clean
platformio run -t clean
```

---

## 🐛 Debugging

### Setup
1. Stelle sicher, dass USB-Kabel angeschlossen ist
2. Device wird von VS Code automatisch erkannt
3. F5 zum Starten (oder Debug-Button in Sidebar)

### Breakpoints
- Klick auf Zeilennummer links
- **Rote Punkt** = Breakpoint aktiv
- Code stoppt dort während Execution

### Variablen Inspizieren
- Hover über Variable für Wert
- "Debug Console" unten für Expressions

---

## 🔍 IntelliSense Troubleshooting

Wenn IntelliSense nicht funktioniert:

1. **Command Palette öffnen** (Ctrl+Shift+P)
2. **"C/C++: Rescan Solutions"** ausführen
3. **VS Code neu starten** (Ctrl+Shift+P → "Developer: Reload Window")
4. Wenn noch Probleme: `.vscode/.c_cpp_database` löschen und neu starten

---

## 📦 Empfohlene Extensions

Alle sind in `extensions.json` gelistet:

| Extension | Nutzen |
|-----------|--------|
| PlatformIO IDE | Build & Upload |
| C/C++ Tools | IntelliSense & Debugging |
| GitLens | Git Integration |
| Clang-Format | Code Formatting |
| Todo Tree | TODO-Kommentare |

**Auto-Installation:**
- VS Code zeigt Banner mit "Install Recommended Extensions"
- Oder: Command Palette → "Extensions: Show Recommended Extensions"

---

## 📝 Code Formatting

### Auto-Format on Save
```json
// In settings.json (bereits konfiguriert)
"editor.formatOnSave": true
"C_Cpp.clang_format_style": "{BasedOnStyle: LLVM, IndentWidth: 4}"
```

### Manuell Formatieren
- **Ctrl+Shift+I** - Format Document
- **Shift+Alt+F** - Format Selection

---

## 🚨 Häufige Probleme & Lösungen

### Problem: "platformio command not found"
```bash
# Lösung: PlatformIO Core installieren
pip install platformio

# Oder über Extension:
# PlatformIO IDE → Home → Install Core
```

### Problem: "Board wird nicht erkannt"
```bash
# USB-Treiber überprüfen
platformio device list

# Oder manuell Port eingeben:
platformio run -t upload --upload-port /dev/ttyUSB0
```

### Problem: "Too many IntelliSense errors"
```bash
# C++ Database neu bauen
rm -rf .vscode/.c_cpp_database

# VS Code neu starten
Ctrl+Shift+P → Developer: Reload Window
```

### Problem: "Build fehlgeschlagen"
```bash
# Clean Build
platformio run -t clean
platformio run

# Full Clean (wenn alles kaputt)
rm -rf .pio
platformio run
```

---

## 🎯 Workflow-Beispiel

### 1. **Projekt öffnen**
```bash
code ESP32_Scanner.code-workspace
```

### 2. **Code schreiben**
- IntelliSense nutzen (Ctrl+Space)
- Fehler werden automatisch unterstrichen

### 3. **Kompilieren**
- Ctrl+Shift+B (oder Tasks: Run Task → Build)

### 4. **Hochladen**
- Tasks: Run Task → Upload
- Oder: Ctrl+Alt+U (wenn konfiguriert)

### 5. **Testen**
- Tasks: Run Task → Monitor
- Serial Output in Terminal anschauen

### 6. **Debuggen** (optional)
- F5 zum Starten
- Breakpoints setzen
- Variablen inspizieren

---

## 🔐 Privacy & Settings

### Telemetrie ausschalten
```json
"telemetry.telemetryLevel": "off"
```

### Auto Update deaktivieren
Datei → Preferences → Settings → Search "update"

---

## 📚 Weitere Ressourcen

- [VS Code Docs](https://code.visualstudio.com/docs)
- [PlatformIO Docs](https://docs.platformio.org)
- [ESP32 Arduino Docs](https://docs.espressif.com)
- [LVGL Documentation](https://docs.lvgl.io)

---

## ✅ Checkliste

- [ ] VS Code installiert
- [ ] PlatformIO IDE Extension installiert
- [ ] Projekt via Workspace geöffnet
- [ ] Build erfolgreich (Ctrl+Shift+B)
- [ ] Serial Monitor funktioniert
- [ ] IntelliSense arbeitet
- [ ] Recommended Extensions installiert

**Alles fertig? → Los geht's! 🚀**
