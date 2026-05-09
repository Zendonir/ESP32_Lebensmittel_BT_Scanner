#pragma once

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Lebensmittel-Scanner</title><style>body{font-family:system-ui;background:#101218;color:#f4f6fb;margin:0}.wrap{padding:24px}.card{background:#1d2230;border-radius:16px;padding:16px;margin:12px 0}button{background:#2f80ed;color:white;border:0;border-radius:12px;padding:12px 18px}</style></head><body><div class="wrap"><h1>Lebensmittel-Scanner</h1><div class="card" id="status">Lade Status…</div><button onclick="refresh()">Aktualisieren</button></div><script>async function refresh(){const r=await fetch('/api/system-info');document.getElementById('status').textContent=await r.text()}refresh()</script></body></html>
)HTML";
