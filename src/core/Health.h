#pragma once
#include <Arduino.h>

// Systemüberwachung: Watchdog, Reset-Ursache, Heap-/Stack-Beobachtung.
//
// Ziel: die beiden beobachteten Fehlerbilder (spontaner Neustart, Einfrieren)
// diagnostizierbar und selbstheilend machen.
//   * Beim Boot wird die Reset-Ursache des vorherigen Laufs auf SD protokolliert
//     (Panic, Brownout, Watchdog, …) – bisher ging diese Information verloren.
//   * Der Task-Watchdog überwacht die UI-Schleife und den Touch-Task. Bleibt einer
//     hängen (blockierender Socket, verklemmter I2C-Bus), erzwingt der Watchdog
//     einen Neustart mit Backtrace, statt das Gerät dauerhaft einfrieren zu lassen.
//   * Heap-Statistiken werden regelmäßig geprüft; Speicherknappheit (die typische
//     Ursache für Abstürze nach Stunden Laufzeit) wird gewarnt, bevor sie kippt.
namespace Health {

// Reset-Ursache + Startwerte protokollieren. Sehr früh in App::begin() aufrufen.
void logBootInfo();

// Task-Watchdog aktivieren und den aufrufenden Task (die Arduino-Loop) anmelden.
// Am Ende von App::begin() aufrufen – der Bootvorgang selbst wird nicht überwacht.
void beginWatchdog();

// true, sobald beginWatchdog() den Watchdog erfolgreich aktiviert hat.
bool watchdogActive();

// Aktuellen Task beim Watchdog anmelden (aus dem jeweiligen Task heraus aufrufen).
void watchCurrentTask();

// Watchdog füttern und periodisch Speicher-/Stack-Statistiken prüfen.
// Einmal pro Durchlauf aus App::loop() aufrufen.
void loop();

// Nur den Watchdog füttern – für lange, legitime Operationen (Backup, OTA).
void feed();

// true, wenn der interne Heap knapp wird. Aufrufer sollten dann keine neuen
// TLS-Verbindungen/Tasks starten, statt in ein fehlgeschlagenes malloc zu laufen.
bool lowMemory();

// Freier interner Heap in Bytes (für Diagnose-Ausgaben).
uint32_t freeInternalHeap();

}  // namespace Health
