#pragma once

// WebAssets.h – retained for backward compatibility only.
// The SPA is now served from LittleFS (/data directory).
// Upload the /data directory to LittleFS using the PlatformIO
// "Upload Filesystem Image" task before flashing firmware.
//
// This minimal fallback page is only shown if LittleFS is not
// mounted or the index.html file is missing.
static const char INDEX_HTML_FALLBACK[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lebensmittel-Scanner</title>
<style>
  body{font-family:system-ui;background:#0f1117;color:#e8eaf6;margin:0;display:flex;align-items:center;justify-content:center;min-height:100vh;flex-direction:column;gap:16px;padding:24px;text-align:center}
  h1{color:#5c7cfa;margin:0}
  p{color:#8892b0;max-width:400px}
  .card{background:#1a1d2e;border:1px solid #2a2d3e;border-radius:12px;padding:24px;max-width:420px}
  code{background:#0f1117;padding:3px 8px;border-radius:6px;font-size:.85rem}
</style>
</head>
<body>
<div class="card">
  <h1>Lebensmittel-Scanner</h1>
  <p>Web-Interface nicht geladen. LittleFS-Dateisystem ist nicht eingebunden oder <code>index.html</code> fehlt.</p>
  <p>Bitte das <code>/data</code>-Verzeichnis mit <em>PlatformIO &rarr; Upload Filesystem Image</em> auf das Gerät übertragen.</p>
  <p><a href="/api/system-info" style="color:#5c7cfa">System-Info (JSON)</a></p>
</div>
</body>
</html>
)HTML";
