/* ============================================================
   ESP32 Lebensmittel-Scanner – SPA Application
   ============================================================ */
'use strict';

/* ============================================================
   API MODULE
   ============================================================ */
const API = (() => {
  const TIMEOUT = 10000;

  async function request(method, endpoint, body) {
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), TIMEOUT);
    try {
      const opts = {
        method,
        signal: ctrl.signal,
        headers: body ? { 'Content-Type': 'application/json' } : undefined,
        body: body ? JSON.stringify(body) : undefined,
      };
      const res = await fetch(endpoint, opts);
      if (!res.ok) {
        const txt = await res.text().catch(() => '');
        throw new Error(`HTTP ${res.status}${txt ? ': ' + txt.slice(0, 80) : ''}`);
      }
      const text = await res.text();
      if (!text) return {};
      try {
        return JSON.parse(text);
      } catch {
        throw new Error(`Ungültiges JSON von ${endpoint}: ${text.slice(0, 60)}`);
      }
    } catch (e) {
      if (e.name === 'AbortError') throw new Error(`Timeout (${TIMEOUT/1000}s) bei ${endpoint}`);
      throw e;
    } finally {
      clearTimeout(timer);
    }
  }

  return {
    get:  (ep)      => request('GET',  ep),
    post: (ep, data) => request('POST', ep, data),
    slowPost: (ep, data) => {
      // Like post() but 25 s timeout — for long-running server operations (setup wizard)
      const ctrl = new AbortController();
      const timer = setTimeout(() => ctrl.abort(), 25000);
      return fetch(ep, {
        method: 'POST', signal: ctrl.signal,
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data),
      }).then(async res => {
        clearTimeout(timer);
        const text = await res.text();
        return text ? JSON.parse(text) : {};
      }).catch(e => {
        clearTimeout(timer);
        if (e.name === 'AbortError') throw new Error('Timeout (25s) bei ' + ep);
        throw e;
      });
    },

    async postForm(endpoint, formData) {
      const ctrl = new AbortController();
      const timer = setTimeout(() => ctrl.abort(), 60000);
      try {
        const res = await fetch(endpoint, { method: 'POST', body: formData, signal: ctrl.signal });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const text = await res.text();
        return text ? JSON.parse(text) : {};
      } finally {
        clearTimeout(timer);
      }
    },
  };
})();

/* ============================================================
   TOAST
   ============================================================ */
const Toast = (() => {
  const icons = {
    ok:     '<svg viewBox="0 0 24 24"><polyline points="20 6 9 17 4 12"/></svg>',
    warn:   '<svg viewBox="0 0 24 24"><path d="M10.29 3.86L1.82 18a2 2 0 001.71 3h16.94a2 2 0 001.71-3L13.71 3.86a2 2 0 00-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>',
    danger: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><line x1="15" y1="9" x2="9" y2="15"/><line x1="9" y1="9" x2="15" y2="15"/></svg>',
    info:   '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/></svg>',
  };

  function show(msg, type = 'info', duration = 3500) {
    const container = document.getElementById('toastContainer');
    const t = document.createElement('div');
    t.className = `toast toast-${type}`;
    t.innerHTML = `<span class="toast-icon">${icons[type] || icons.info}</span><span>${msg}</span>`;
    container.appendChild(t);
    setTimeout(() => {
      t.classList.add('out');
      t.addEventListener('animationend', () => t.remove(), { once: true });
    }, duration);
  }

  return {
    show,
    success: (m) => show(m, 'ok'),
    error:   (m) => show(m, 'danger', 5000),
    warn:    (m) => show(m, 'warn'),
    info:    (m) => show(m, 'info'),
  };
})();

/* ============================================================
   MODAL
   ============================================================ */
const Modal = (() => {
  const backdrop = () => document.getElementById('modalBackdrop');
  const title    = () => document.getElementById('modalTitle');
  const body     = () => document.getElementById('modalBody');
  const footer   = () => document.getElementById('modalFooter');

  function show(cfg) {
    title().textContent = cfg.title || '';
    body().innerHTML    = cfg.body  || '';
    footer().innerHTML  = '';
    (cfg.actions || []).forEach(a => {
      const btn = document.createElement('button');
      btn.className = `btn ${a.cls || ''}`;
      btn.textContent = a.label;
      btn.onclick = a.onclick;
      footer().appendChild(btn);
    });
    backdrop().hidden = false;
    document.body.style.overflow = 'hidden';
  }

  function hide() {
    backdrop().hidden = true;
    document.body.style.overflow = '';
  }

  function confirm(title, msg, onConfirm) {
    show({
      title,
      body: `<p style="color:var(--subtext)">${msg}</p>`,
      actions: [
        { label: 'Abbrechen', cls: '', onclick: hide },
        { label: 'Bestätigen', cls: 'btn-danger', onclick: () => { hide(); onConfirm(); } },
      ],
    });
  }

  backdrop()?.addEventListener('click', e => { if (e.target === backdrop()) hide(); });
  document.addEventListener('keydown', e => { if (e.key === 'Escape') hide(); });

  return { show, hide, confirm };
})();

/* ============================================================
   LOADING
   ============================================================ */
const Loading = {
  show() { document.getElementById('loadingOverlay').hidden = false; },
  hide() { document.getElementById('loadingOverlay').hidden = true; },
};

/* ============================================================
   ROUTER
   ============================================================ */
const Router = (() => {
  let current = null;
  const titles = {
    dashboard:  'Dashboard',
    inventory:  'Inventar',
    templates:  'Vorlagen',
    categories: 'Kategorien',
    shopping:   'Einkaufsliste',
    design:     'Design',
    system:     'System',
    network:    'Netzwerk',
    printer:    'Drucker',
    scanner:    'Scanner',
    mqtt:       'MQTT',
    telegram:   'Telegram',
    serversync: 'Server-Sync',
    statistics: 'Statistiken',
    ota:        'OTA Update',
    logs:       'Logs',
  };

  function go(page) {
    if (current === page) return;
    document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
    document.querySelectorAll('.nav-link, .bnav-item').forEach(l => l.classList.remove('active'));

    const section = document.getElementById(`page-${page}`);
    if (!section) return;
    section.classList.add('active');
    current = page;

    document.querySelectorAll(`[data-page="${page}"]`).forEach(l => l.classList.add('active'));
    const t = titles[page] || page;
    document.getElementById('mobileTitle').textContent = t;
    document.title = `${t} – Lebensmittel-Scanner`;

    App.closeSidebar();
    window.location.hash = page;

    const pg = Pages[page];
    if (pg && typeof pg.load === 'function') pg.load();
  }

  function init() {
    document.querySelectorAll('[data-page]').forEach(el => {
      el.addEventListener('click', e => {
        e.preventDefault();
        go(el.dataset.page);
      });
    });
    const hash = (window.location.hash || '#dashboard').slice(1);
    go(hash in titles ? hash : 'dashboard');
  }

  return { go, init, current: () => current };
})();

/* ============================================================
   STATE
   ============================================================ */
const State = {
  inventory:  [],
  templates:  [],
  categories: [],
  shopping:   [],
  uiConfig:   {},
  logs:       [],
};

/* ============================================================
   UTILS
   ============================================================ */
function esc(str) {
  return String(str ?? '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function daysUntil(dateStr) {
  if (!dateStr) return Infinity;
  const now  = new Date(); now.setHours(0,0,0,0);
  const then = new Date(dateStr);
  return Math.round((then - now) / 86400000);
}

function expiryClass(days) {
  if (days < 0)  return 'danger';
  if (days <= 7) return 'warn';
  return 'ok';
}

function expiryBadge(days) {
  const cls = expiryClass(days);
  const label = days < 0 ? `${Math.abs(days)}d abgelaufen` : days === 0 ? 'Heute' : `${days}d`;
  return `<span class="badge badge-${cls}">${label}</span>`;
}

function formatDate(str) {
  if (!str) return '—';
  try { return new Date(str).toLocaleDateString('de-DE'); } catch { return str; }
}

function formatBytes(b) {
  if (b < 1024) return `${b} B`;
  if (b < 1048576) return `${(b/1024).toFixed(1)} KB`;
  return `${(b/1048576).toFixed(1)} MB`;
}

function rgb16ToHex(color565) {
  const r = ((color565 >> 11) & 0x1F) << 3;
  const g = ((color565 >> 5)  & 0x3F) << 2;
  const b = (color565 & 0x1F) << 3;
  return '#' + [r,g,b].map(v => v.toString(16).padStart(2,'0')).join('');
}

function hexToRgb16(hex) {
  const n = parseInt(hex.slice(1), 16);
  const r = (n >> 16) & 0xFF;
  const g = (n >> 8)  & 0xFF;
  const b =  n        & 0xFF;
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

function makeColorField(id, label, value) {
  return `
    <div class="color-field">
      <label for="${id}">${esc(label)}</label>
      <div class="color-preview" id="${id}Preview" style="background:${esc(value)}"
           onclick="document.getElementById('${id}Native').click()"></div>
      <input type="color" class="color-input-native" id="${id}Native" value="${esc(value)}"
             oninput="onColorChange('${id}',this.value)">
    </div>`;
}

function progressBar(used, total, cls='') {
  const pct = total > 0 ? Math.round(used / total * 100) : 0;
  const barCls = pct > 85 ? 'danger' : pct > 65 ? 'warn' : '';
  return `
    <div style="display:flex;align-items:center;gap:10px;font-size:.8rem;margin-bottom:8px">
      <span style="flex:1;color:var(--subtext)">${cls}</span>
      <span>${formatBytes(used)} / ${formatBytes(total)}</span>
      <span style="color:var(--subtext)">${pct}%</span>
    </div>
    <div class="progress-wrap" style="margin-bottom:12px">
      <div class="progress-bar ${barCls}" style="width:${pct}%"></div>
    </div>`;
}

/* ============================================================
   PAGES
   ============================================================ */
const Pages = {};

/* ---- DASHBOARD ---- */
Pages.dashboard = {
  async load() {
    let sys = {}, inv = [], shop = [];

    try { sys = await API.get('/api/system-info'); }
    catch (e) { Toast.warn('System-Info: ' + e.message); }

    try {
      const r = await API.get('/api/inventory');
      inv = Array.isArray(r) ? r : [];
    } catch (e) { Toast.warn('Inventar: ' + e.message); }

    try {
      const r = await API.get('/api/shopping-list');
      shop = Array.isArray(r) ? r : [];
    } catch (e) { Toast.warn('Einkaufsliste: ' + e.message); }

    State.inventory = inv;
    State.shopping  = shop;
    try { this.render(sys); }
    catch (e) { Toast.error('Dashboard render: ' + e.message); }
  },

  render(sys) {
    const items = State.inventory;
    const total   = items.length;
    const expiring = items.filter(i => { const d=daysUntil(i.expiryDate); return d>=0 && d<=7; }).length;
    const expired  = items.filter(i => daysUntil(i.expiryDate) < 0).length;
    const shopOpen = State.shopping.filter(s => !s.bought).length;

    document.getElementById('dashStatGrid').innerHTML = `
      <div class="stat-card stat-accent">
        <div class="stat-label">Eingelagert</div>
        <div class="stat-value">${total}</div>
        <div class="stat-sub">Artikel gesamt</div>
      </div>
      <div class="stat-card stat-warn">
        <div class="stat-label">Bald ablaufend</div>
        <div class="stat-value">${expiring}</div>
        <div class="stat-sub">≤ 7 Tage</div>
      </div>
      <div class="stat-card stat-danger">
        <div class="stat-label">Abgelaufen</div>
        <div class="stat-value">${expired}</div>
        <div class="stat-sub">Artikel prüfen</div>
      </div>
      <div class="stat-card">
        <div class="stat-label">Einkaufsliste</div>
        <div class="stat-value">${shopOpen}</div>
        <div class="stat-sub">Offen</div>
      </div>`;

    const sorted = [...items].sort((a,b) => daysUntil(a.expiryDate) - daysUntil(b.expiryDate));
    const soon = sorted.filter(i => daysUntil(i.expiryDate) <= 14).slice(0,8);
    document.getElementById('dashExpiry').innerHTML = soon.length
      ? soon.map(i => `
          <div class="item-row">
            <div>
              <div class="item-row-name">${esc(i.name)}</div>
              <div class="item-row-sub">${esc(i.category || '—')}</div>
            </div>
            <div>${expiryBadge(daysUntil(i.expiryDate))}</div>
          </div>`).join('')
      : '<div class="muted" style="padding:8px">Keine ablaufenden Produkte</div>';

    const wifiConn = sys.ip && sys.ip !== '0.0.0.0';
    document.getElementById('dashStatus').innerHTML = `
      <div class="status-row">
        <span class="status-dot ${wifiConn ? 'ok' : 'danger'}"></span>
        <span class="status-row-label">WLAN</span>
        <span class="status-row-value">${wifiConn ? esc(sys.ip) : 'Getrennt'}</span>
      </div>
      <div class="status-row">
        <span class="status-dot"></span>
        <span class="status-row-label">MQTT</span>
        <span class="status-row-value">—</span>
      </div>
      <div class="status-row">
        <span class="status-dot"></span>
        <span class="status-row-label">Server-Sync</span>
        <span class="status-row-value">—</span>
      </div>`;

    document.getElementById('dashActivity').innerHTML = `
      <div class="item-row">
        <div class="item-row-name">Firmware ${esc(sys.firmware || '—')}</div>
        <div class="item-row-sub">Uptime: ${esc(sys.uptime || '—')}</div>
      </div>`;

    const heapPct = sys.heapTotal > 0 ? Math.round(sys.heap/sys.heapTotal*100) : 0;
    document.getElementById('dashStorage').innerHTML = `
      <div style="font-size:.8rem;color:var(--subtext);margin-bottom:6px">Heap-Speicher</div>
      <div class="progress-wrap"><div class="progress-bar ${heapPct>80?'danger':heapPct>60?'warn':''}" style="width:${heapPct}%"></div></div>
      <div style="font-size:.75rem;color:var(--subtext);margin-top:4px">${formatBytes(sys.heap||0)} frei</div>`;
  },
};

/* ---- INVENTORY ---- */
Pages.inventory = {
  async load() {
    try {
      const [inv, cats] = await Promise.all([
        API.get('/api/inventory'),
        API.get('/api/categories'),
      ]);
      State.inventory  = Array.isArray(inv) ? inv : [];
      State.categories = Array.isArray(cats) ? cats : [];
      this.populateCatFilter();
      this.filter();
    } catch(e) {
      Toast.error('Inventar: ' + e.message);
    }
  },

  populateCatFilter() {
    const sel = document.getElementById('invCatFilter');
    const cur = sel.value;
    sel.innerHTML = '<option value="">Alle Kategorien</option>';
    State.categories.forEach(c => {
      sel.innerHTML += `<option value="${esc(c.name)}">${esc(c.name)}</option>`;
    });
    sel.value = cur;
  },

  filter() {
    const q      = document.getElementById('invSearch').value.toLowerCase();
    const cat    = document.getElementById('invCatFilter').value;
    const status = document.getElementById('invStatusFilter').value;
    const sort   = document.getElementById('invSort').value;

    let items = State.inventory.filter(i => {
      const matchQ = !q || i.name?.toLowerCase().includes(q) || i.brand?.toLowerCase().includes(q) || i.barcode?.includes(q);
      const matchC = !cat || i.category === cat;
      const d = daysUntil(i.expiryDate);
      const matchS = !status ||
        (status === 'ok'     && d >= 7) ||
        (status === 'warn'   && d >= 0 && d < 7) ||
        (status === 'danger' && d < 0);
      return matchQ && matchC && matchS;
    });

    items = items.sort((a, b) => {
      if (sort === 'name')    return (a.name||'').localeCompare(b.name||'');
      if (sort === 'expiry')  return daysUntil(a.expiryDate) - daysUntil(b.expiryDate);
      if (sort === 'category')return (a.category||'').localeCompare(b.category||'');
      if (sort === 'added')   return (b.addedDate||'').localeCompare(a.addedDate||'');
      return 0;
    });

    this.renderTable(items);
  },

  renderTable(items) {
    const tbody = document.getElementById('invBody');
    const empty = document.getElementById('invEmpty');
    if (!items.length) {
      tbody.innerHTML = '';
      empty.hidden = false;
      return;
    }
    empty.hidden = true;
    tbody.innerHTML = items.map((i, idx) => {
      const days = daysUntil(i.expiryDate);
      const cls  = expiryClass(days);
      return `
        <tr>
          <td>
            <div style="font-weight:600">${esc(i.name)}</div>
            ${i.brand ? `<div style="font-size:.75rem;color:var(--subtext)">${esc(i.brand)}</div>` : ''}
          </td>
          <td>${i.category ? `<span class="badge badge-info">${esc(i.category)}</span>` : '—'}</td>
          <td>${esc(i.quantity || 1)}</td>
          <td>${formatDate(i.expiryDate)}</td>
          <td>${expiryBadge(days)}</td>
          <td class="actions">
            <button class="btn btn-sm" onclick="Pages.inventory.edit(${idx})">Bearb.</button>
            <button class="btn btn-sm btn-danger" onclick="Pages.inventory.remove(${idx})">Löschen</button>
          </td>
        </tr>`;
    }).join('');
    this._filtered = items;
  },

  _filtered: [],

  openAdd() {
    this._openForm(null);
  },

  edit(idx) {
    this._openForm(this._filtered[idx]);
  },

  _openForm(item) {
    const isEdit = !!item;
    const catOpts = State.categories.map(c =>
      `<option value="${esc(c.name)}" ${item?.category===c.name?'selected':''}>${esc(c.name)}</option>`).join('');

    Modal.show({
      title: isEdit ? 'Artikel bearbeiten' : 'Artikel hinzufügen',
      body: `
        <form id="invForm" class="form-stack">
          <div class="form-field"><label>Name *</label><input class="input" id="ifName" value="${esc(item?.name||'')}" required></div>
          <div class="form-field"><label>Marke</label><input class="input" id="ifBrand" value="${esc(item?.brand||'')}"></div>
          <div class="form-field"><label>Barcode</label><input class="input" id="ifBarcode" value="${esc(item?.barcode||'')}"></div>
          <div class="form-field"><label>Kategorie</label><select class="select" id="ifCat"><option value="">—</option>${catOpts}</select></div>
          <div class="form-field"><label>Menge</label><input class="input" type="number" id="ifQty" value="${item?.quantity||1}" min="1"></div>
          <div class="form-field"><label>Ablaufdatum</label><input class="input" type="date" id="ifExpiry" value="${item?.expiryDate||''}"></div>
        </form>`,
      actions: [
        { label: 'Abbrechen', cls: '', onclick: Modal.hide },
        { label: isEdit ? 'Speichern' : 'Hinzufügen', cls: 'btn-ok', onclick: () => this._submitForm(item) },
      ],
    });
  },

  async _submitForm(original) {
    const name = document.getElementById('ifName').value.trim();
    if (!name) { Toast.warn('Name ist erforderlich'); return; }
    const data = {
      name,
      brand:      document.getElementById('ifBrand').value.trim(),
      barcode:    document.getElementById('ifBarcode').value.trim(),
      category:   document.getElementById('ifCat').value,
      quantity:   parseInt(document.getElementById('ifQty').value) || 1,
      expiryDate: document.getElementById('ifExpiry').value,
      ...(original ? { labelBarcode: original.labelBarcode } : {}),
    };
    try {
      await API.post('/api/inventory', data);
      Toast.success(original ? 'Artikel aktualisiert' : 'Artikel hinzugefügt');
      Modal.hide();
      this.load();
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },

  remove(idx) {
    const item = this._filtered[idx];
    Modal.confirm('Artikel löschen', `"${esc(item.name)}" wirklich löschen?`, async () => {
      try {
        await API.post('/api/inventory/delete', { labelBarcode: item.labelBarcode, barcode: item.barcode });
        Toast.success('Artikel gelöscht');
        this.load();
      } catch(e) {
        Toast.error('Fehler: ' + e.message);
      }
    });
  },
};

/* ---- TEMPLATES ---- */
Pages.templates = {
  _all: [],

  async load() {
    try {
      const [tpls, cats] = await Promise.all([
        API.get('/api/custom-products'),
        API.get('/api/categories'),
      ]);
      State.categories = Array.isArray(cats) ? cats : [];
      // Annotate each template with its array index so edit/delete are index-based
      this._all = (Array.isArray(tpls) ? tpls : []).map((t, i) => ({ ...t, _idx: i }));
      State.templates = this._all;
      this.filter();
    } catch(e) {
      Toast.error('Vorlagen: ' + e.message);
    }
  },

  filter() {
    const q = document.getElementById('tplSearch').value.toLowerCase();
    const items = this._all.filter(t => !q || t.name?.toLowerCase().includes(q) || t.brand?.toLowerCase().includes(q));
    this.renderGrid(items);
  },

  renderGrid(items) {
    const grid  = document.getElementById('tplGrid');
    const empty = document.getElementById('tplEmpty');
    if (!items.length) { grid.innerHTML = ''; empty.hidden = false; return; }
    empty.hidden = true;
    // Group by category
    const byCategory = {};
    items.forEach((t, i) => {
      const cat = t.category || 'Ohne Kategorie';
      if (!byCategory[cat]) byCategory[cat] = [];
      byCategory[cat].push({ t, i });
    });
    let html = '';
    for (const [cat, entries] of Object.entries(byCategory).sort()) {
      html += `<div style="grid-column:1/-1;margin:12px 0 4px"><span style="font-size:.8rem;font-weight:600;color:var(--accent);text-transform:uppercase;letter-spacing:.05em">${esc(cat)}</span></div>`;
      html += entries.map(({ t, i }) => `
      <div class="tpl-card">
        <div class="tpl-header">
          <div>
            <div class="tpl-name">${esc(t.name)}</div>
            ${t.brand ? `<div class="tpl-brand">${esc(t.brand)}</div>` : ''}
          </div>
          <div class="btn-group">
            <button class="btn btn-sm" onclick="Pages.templates.edit(${i})">Bearbeiten</button>
            <button class="btn btn-sm btn-danger" onclick="Pages.templates.remove(${i})">Löschen</button>
          </div>
        </div>
        <div class="tpl-meta">
          ${t.defaultDays ? `<span class="tpl-chip">${t.defaultDays} Tage MHD</span>` : ''}
          ${t.askQty ? `<span class="tpl-chip">Menge abfragen</span>` : ''}
          ${t.barcode ? `<span class="tpl-chip mono">${esc(t.barcode)}</span>` : ''}
        </div>
        ${t.description ? `<div style="font-size:.8rem;color:var(--subtext);margin-top:8px">${esc(t.description)}</div>` : ''}
      </div>`).join('');
    }
    grid.innerHTML = html;
    this._displayed = items;
  },

  _displayed: [],

  openAdd() { this._openForm(null); },
  edit(i)   { this._openForm(this._displayed[i]); },

  _openForm(t) {
    const catOpts = State.categories.map(c =>
      `<option value="${esc(c.name)}" ${t?.category===c.name?'selected':''}>${esc(c.name)}</option>`).join('');
    Modal.show({
      title: t ? 'Vorlage bearbeiten' : 'Neue Vorlage',
      body: `
        <form class="form-stack" id="tplForm">
          <div class="form-field"><label>Name *</label><input class="input" id="tfName" value="${esc(t?.name||'')}" required></div>
          <div class="form-field"><label>Marke</label><input class="input" id="tfBrand" value="${esc(t?.brand||'')}"></div>
          <div class="form-field"><label>Barcode</label><input class="input" id="tfBarcode" value="${esc(t?.barcode||'')}"></div>
          <div class="form-field"><label>Kategorie</label><select class="select" id="tfCat"><option value="">—</option>${catOpts}</select></div>
          <div class="form-field"><label>Beschreibung</label><input class="input" id="tfDesc" value="${esc(t?.description||'')}"></div>
          <div class="form-field"><label>Standard-Haltbarkeit (Tage)</label><input class="input" type="number" id="tfDays" value="${t?.defaultDays||0}" min="0"></div>
          <div class="form-field"><label>Menge abfragen</label><label class="switch"><input type="checkbox" id="tfAskQty" ${t?.askQty?'checked':''}><span class="slider"></span></label></div>
        </form>`,
      actions: [
        { label: 'Abbrechen', cls: '', onclick: Modal.hide },
        { label: t ? 'Speichern' : 'Erstellen', cls: 'btn-ok', onclick: () => this._submit(t) },
      ],
    });
  },

  async _submit(original) {
    const name = document.getElementById('tfName').value.trim();
    if (!name) { Toast.warn('Name erforderlich'); return; }
    const data = {
      name,
      brand:       document.getElementById('tfBrand').value.trim(),
      barcode:     document.getElementById('tfBarcode').value.trim(),
      category:    document.getElementById('tfCat').value,
      description: document.getElementById('tfDesc').value.trim(),
      defaultDays: parseInt(document.getElementById('tfDays').value) || 0,
      askQty:      document.getElementById('tfAskQty').checked,
    };
    try {
      if (original !== null && original._idx !== undefined) {
        await API.post('/api/custom-products/update', { _idx: original._idx, ...data });
      } else {
        await API.post('/api/custom-products', data);
      }
      Toast.success('Vorlage gespeichert');
      Modal.hide();
      this.load();
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },

  remove(i) {
    const t = this._displayed[i];
    Modal.confirm('Vorlage löschen', `"${esc(t.name)}" löschen?`, async () => {
      try {
        await API.post('/api/custom-products/delete', { _idx: t._idx });
        Toast.success('Vorlage gelöscht');
        this.load();
      } catch(e) {
        Toast.error('Fehler: ' + e.message);
      }
    });
  },
};


/* ---- CATEGORIES ---- */
Pages.categories = {
  async load() {
    try {
      const cats = await API.get('/api/categories');
      State.categories = Array.isArray(cats) ? cats : [];
      this.render();
    } catch(e) {
      Toast.error('Kategorien: ' + e.message);
    }
  },

  render() {
    const grid  = document.getElementById('catGrid');
    const empty = document.getElementById('catEmpty');
    const cats  = State.categories;
    if (!cats.length) { grid.innerHTML = ''; empty.hidden = false; return; }
    empty.hidden = true;
    grid.innerHTML = cats.map((c, i) => {
      const bg  = rgb16ToHex(c.bgColor   || 0x2435);
      const txt = rgb16ToHex(c.textColor || 0xFFFF);
      return `
        <div class="cat-card" style="background:${bg};color:${txt}">
          <span class="cat-name">${esc(c.name)}</span>
          <div class="cat-actions">
            <button class="cat-btn" onclick="Pages.categories.edit(${i})" style="color:${txt}">Bearbeiten</button>
            <button class="cat-btn" onclick="Pages.categories.remove(${i})" style="color:${txt}">Löschen</button>
          </div>
        </div>`;
    }).join('');
  },

  openAdd() { this._openForm(null); },
  edit(i)   { this._openForm(State.categories[i]); },

  _openForm(c) {
    const bg  = rgb16ToHex(c?.bgColor   || 0x2435);
    const txt = rgb16ToHex(c?.textColor || 0xFFFF);
    Modal.show({
      title: c ? 'Kategorie bearbeiten' : 'Neue Kategorie',
      body: `
        <form class="form-stack" id="catForm">
          <div class="form-field"><label>Name *</label><input class="input" id="cfName" value="${esc(c?.name||'')}" required></div>
          <div class="form-field" style="margin-top:8px">
            <label>Hintergrundfarbe</label>
            ${makeColorField('cfBg', '', bg)}
          </div>
          <div class="form-field" style="margin-top:8px">
            <label>Textfarbe</label>
            ${makeColorField('cfTxt', '', txt)}
          </div>
          <div style="margin-top:12px">
            <div class="card-title">Vorschau</div>
            <div id="catPreview" class="cat-card" style="background:${bg};color:${txt};display:inline-flex;padding:14px 20px">
              <span class="cat-name" id="catPreviewName">${esc(c?.name||'Kategorie')}</span>
            </div>
          </div>
        </form>`,
      actions: [
        { label: 'Abbrechen', cls: '', onclick: Modal.hide },
        { label: c ? 'Speichern' : 'Erstellen', cls: 'btn-ok', onclick: () => this._submit(c) },
      ],
    });
    document.getElementById('cfName').addEventListener('input', e => {
      const el = document.getElementById('catPreviewName');
      if (el) el.textContent = e.target.value || 'Kategorie';
    });
  },

  async _submit(original) {
    const name = document.getElementById('cfName').value.trim();
    if (!name) { Toast.warn('Name erforderlich'); return; }
    const bg  = document.getElementById('cfBgNative').value;
    const txt = document.getElementById('cfTxtNative').value;
    try {
      await API.post('/api/categories', {
        name,
        bgColor:   hexToRgb16(bg),
        textColor: hexToRgb16(txt),
        ...(original ? { oldName: original.name } : {}),
      });
      Toast.success('Kategorie gespeichert');
      Modal.hide();
      this.load();
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },

  remove(i) {
    const c = State.categories[i];
    Modal.confirm('Kategorie löschen', `"${esc(c.name)}" löschen?`, async () => {
      try {
        await API.post('/api/categories/delete', { name: c.name });
        Toast.success('Kategorie gelöscht');
        this.load();
      } catch(e) {
        Toast.error('Fehler: ' + e.message);
      }
    });
  },
};

/* Helper for color picker in modals */
Pages.design = Pages.design || {};
function onColorChange(id, val) {
  const preview = document.getElementById(id + 'Preview');
  if (preview) preview.style.background = val;
  const catPrev = document.getElementById('catPreview');
  if (catPrev && id === 'cfBg')  catPrev.style.background = val;
  if (catPrev && id === 'cfTxt') catPrev.style.color = val;
  if (typeof Pages.design.onColorChange === 'function') Pages.design.onColorChange(id, val);
}
window.onColorChange = onColorChange; // expose for inline handlers in makeColorField

/* ---- SHOPPING ---- */
Pages.shopping = {
  async load() {
    try {
      const [shop, cats] = await Promise.all([
        API.get('/api/shopping-list'),
        API.get('/api/categories'),
      ]);
      State.shopping   = Array.isArray(shop) ? shop : [];
      State.categories = Array.isArray(cats) ? cats : [];
      this.populateCatFilter();
      this.filter();
    } catch(e) {
      Toast.error('Einkaufsliste: ' + e.message);
    }
  },

  populateCatFilter() {
    const sel = document.getElementById('shopCatFilter');
    const cur = sel.value;
    sel.innerHTML = '<option value="">Alle Kategorien</option>';
    State.categories.forEach(c => {
      sel.innerHTML += `<option value="${esc(c.name)}">${esc(c.name)}</option>`;
    });
    sel.value = cur;
  },

  filter() {
    const cat    = document.getElementById('shopCatFilter').value;
    const status = document.getElementById('shopStatusFilter').value;
    const items  = State.shopping.filter(i => {
      const mc = !cat || i.category === cat;
      const ms = !status || (status === 'open' && !i.bought) || (status === 'bought' && i.bought);
      return mc && ms;
    });
    this.renderList(items);
  },

  renderList(items) {
    const list  = document.getElementById('shopList');
    const empty = document.getElementById('shopEmpty');
    if (!items.length) { list.innerHTML = ''; empty.hidden = false; return; }
    empty.hidden = true;
    list.innerHTML = items.map((it, i) => `
      <div class="shop-item ${it.bought ? 'bought' : ''}">
        <input type="checkbox" class="shop-check" ${it.bought ? 'checked' : ''}
               onchange="Pages.shopping.toggle(${i}, this.checked)">
        <div style="flex:1">
          <div class="shop-name">${esc(it.name)}</div>
          ${it.category ? `<div class="shop-cat">${esc(it.category)}</div>` : ''}
        </div>
        <button class="shop-del" onclick="Pages.shopping.remove(${i})" title="Löschen">
          <svg viewBox="0 0 24 24" width="18" height="18"><polyline points="3 6 5 6 21 6"/><path d="M19 6l-1 14a2 2 0 01-2 2H8a2 2 0 01-2-2L5 6"/><path d="M10 11v6M14 11v6"/></svg>
        </button>
      </div>`).join('');
    this._displayed = items;
  },

  _displayed: [],

  openAdd() {
    const catOpts = State.categories.map(c =>
      `<option value="${esc(c.name)}">${esc(c.name)}</option>`).join('');
    Modal.show({
      title: 'Artikel hinzufügen',
      body: `
        <form class="form-stack" id="shopForm">
          <div class="form-field"><label>Name *</label><input class="input" id="sfName" required></div>
          <div class="form-field"><label>Kategorie</label><select class="select" id="sfCat"><option value="">—</option>${catOpts}</select></div>
        </form>`,
      actions: [
        { label: 'Abbrechen', cls: '', onclick: Modal.hide },
        { label: 'Hinzufügen', cls: 'btn-ok', onclick: () => this._submit() },
      ],
    });
  },

  async _submit() {
    const name = document.getElementById('sfName').value.trim();
    if (!name) { Toast.warn('Name erforderlich'); return; }
    try {
      await API.post('/api/shopping-list', {
        name,
        category:   document.getElementById('sfCat').value,
        addedDate:  new Date().toISOString().split('T')[0],
        bought:     false,
      });
      Toast.success('Hinzugefügt');
      Modal.hide();
      this.load();
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },

  async toggle(i, checked) {
    const item = { ...this._displayed[i], bought: checked };
    try {
      await API.post('/api/shopping-list/update', item);
      State.shopping = State.shopping.map(s =>
        s.name === item.name ? item : s);
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },

  remove(i) {
    const it = this._displayed[i];
    Modal.confirm('Eintrag löschen', `"${esc(it.name)}" löschen?`, async () => {
      try {
        await API.post('/api/shopping-list/delete', { name: it.name });
        Toast.success('Gelöscht');
        this.load();
      } catch(e) {
        Toast.error('Fehler: ' + e.message);
      }
    });
  },
};

/* ---- DESIGN ---- */
Pages.design = {
  _cfg: {},

  async load() {
    try {
      const [ui, font] = await Promise.all([
        API.get('/api/ui-config'),
        API.get('/api/font-config'),
      ]);
      this._cfg = { ...ui, ...font };
      this.renderFields();
      this.updatePreview();
    } catch(e) {
      Toast.error('Design: ' + e.message);
    }
  },

  _colorDefs: [
    ['bg',      'Hintergrund'],
    ['text',    'Text'],
    ['subtext', 'Subtext'],
    ['ok',      'OK-Farbe'],
    ['warn',    'Warnung'],
    ['danger',  'Gefahr'],
    ['accent',  'Akzent'],
    ['header',  'Header'],
    ['surface', 'Oberfläche'],
    ['btn_ok',  'Button OK'],
    ['btn_back','Button Back'],
  ],
  _layoutDefs: [
    ['brightness',   'Helligkeit (0-255)', 'number'],
    ['btn_height',   'Button-Höhe (px)',   'number'],
    ['btn_gap',      'Button-Abstände',    'number'],
    ['corner_radius','Eckenradius',         'number'],
    ['cat_cols',     'Kategorie-Spalten',  'number'],
    ['cat_gap',      'Kategorie-Gap',      'number'],
    ['list_height',  'Listenhöhe',         'number'],
    ['list_radius',  'Listenradius',       'number'],
    ['header_height','Headerhöhe',         'number'],
    ['timeout',      'Timeout (s)',        'number'],
  ],
  _fontDefs: [
    ['title','Titel'],['body','Body'],['small','Klein'],
    ['value','Wert'],['key','Key'],['btn','Button'],
  ],
  _behaviorDefs: [
    ['warn_days',    'Warngrenze (Tage)',  'number'],
    ['date_format',  'Datumsformat',       'text'],
    ['show_clock',   'Uhr anzeigen',       'checkbox'],
    ['sounds',       'Sounds aktivieren',  'checkbox'],
    ['power_save',   'Power-Save (s)',     'number'],
  ],

  renderFields() {
    const cfg = this._cfg;
    document.getElementById('designColorFields').innerHTML = this._colorDefs.map(([key, label]) => {
      const val = cfg[key] || '#111827';
      return makeColorField(`dc_${key}`, label, val);
    }).join('');

    document.getElementById('designFontFields').innerHTML = this._fontDefs.map(([key, label]) => `
      <div class="form-field">
        <label>${label}</label>
        <input class="input" type="number" id="df_${key}" value="${cfg['font_'+key] || 16}" min="8" max="48"
               oninput="Pages.design.updatePreview()">
      </div>`).join('');

    document.getElementById('designLayoutFields').innerHTML = this._layoutDefs.map(([key, label, type]) => `
      <div class="form-field">
        <label>${label}</label>
        <input class="input" type="${type||'number'}" id="dl_${key}" value="${cfg[key] || ''}">
      </div>`).join('');

    document.getElementById('designBehaviorFields').innerHTML = this._behaviorDefs.map(([key, label, type]) => {
      if (type === 'checkbox') return `
        <div class="form-field"><label>${label}</label>
          <label class="switch"><input type="checkbox" id="db_${key}" ${cfg[key]?'checked':''}><span class="slider"></span></label>
        </div>`;
      return `
        <div class="form-field"><label>${label}</label>
          <input class="input" type="${type}" id="db_${key}" value="${cfg[key]||''}">
        </div>`;
    }).join('');

    this._colorDefs.forEach(([key]) => {
      const native = document.getElementById(`dc_${key}Native`);
      if (native) native.addEventListener('input', e => this.onColorChange(`dc_${key}`, e.target.value));
    });
  },

  onColorChange(id, val) {
    const preview = document.getElementById(id + 'Preview');
    if (preview) preview.style.background = val;
    this.updatePreview();
  },

  updatePreview() {
    const get = (id) => {
      const el = document.getElementById(id + 'Native') || document.getElementById(id);
      return el?.value || '';
    };
    const p = document.getElementById('designPreview');
    if (!p) return;
    const bg      = get('dc_bg')      || 'var(--bg)';
    const surface = get('dc_surface') || 'var(--surface)';
    const header  = get('dc_header')  || 'var(--header)';
    const text    = get('dc_text')    || 'var(--text)';
    const accent  = get('dc_accent')  || 'var(--accent)';
    const ok      = get('dc_btn_ok')  || 'var(--btn-ok)';
    const warn    = get('dc_warn')    || 'var(--warn)';
    const danger  = get('dc_danger')  || 'var(--danger)';
    p.style.background = bg;
    p.style.color = text;
    p.innerHTML = `
      <div class="preview-header" style="background:${header};color:${accent}">Scanner</div>
      <div class="preview-card" style="background:${surface};color:${text}">
        <div style="font-size:.8rem;opacity:.7;margin-bottom:8px">Produktkarte</div>
        <div style="font-weight:700">Joghurt Natur</div>
        <div style="font-size:.75rem;opacity:.6">Milchprodukte · 3 Tage</div>
      </div>
      <div style="display:flex;gap:6px;flex-wrap:wrap">
        <span class="preview-btn" style="background:${ok}">OK</span>
        <span class="preview-btn preview-warn" style="background:${warn};color:#000">Warnung</span>
        <span class="preview-btn preview-danger" style="background:${danger}">Abgelaufen</span>
      </div>`;
  },

  async save() {
    const cfg = {};
    this._colorDefs.forEach(([key]) => {
      const el = document.getElementById(`dc_${key}Native`);
      if (el) cfg[key] = el.value;
    });
    this._layoutDefs.forEach(([key]) => {
      const el = document.getElementById(`dl_${key}`);
      if (el) cfg[key] = el.type==='checkbox' ? el.checked : el.value;
    });
    this._behaviorDefs.forEach(([key]) => {
      const el = document.getElementById(`db_${key}`);
      if (el) cfg[key] = el.type==='checkbox' ? el.checked : el.value;
    });
    const fontCfg = {};
    this._fontDefs.forEach(([key]) => {
      const el = document.getElementById(`df_${key}`);
      if (el) fontCfg['font_'+key] = parseInt(el.value) || 16;
    });
    try {
      await Promise.all([
        API.post('/api/ui-config',   cfg),
        API.post('/api/font-config', fontCfg),
      ]);
      Toast.success('Design gespeichert');
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },

  exportTheme() {
    const data = this._cfg;
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'scanner-theme.json';
    a.click();
  },

  importTheme() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json';
    input.onchange = async (e) => {
      try {
        const text = await e.target.files[0].text();
        this._cfg = { ...this._cfg, ...JSON.parse(text) };
        this.renderFields();
        this.updatePreview();
        Toast.info('Theme importiert — Speichern zum Übernehmen');
      } catch {
        Toast.error('Ungültiges Theme-Datei');
      }
    };
    input.click();
  },

  resetDefaults() {
    Modal.confirm('Design zurücksetzen', 'Alle Design-Einstellungen auf Standard zurücksetzen?', async () => {
      try {
        await API.post('/api/ui-config/reset', {});
        Toast.success('Design zurückgesetzt');
        this.load();
      } catch(e) {
        Toast.error('Fehler: ' + e.message);
      }
    });
  },
};

/* ---- SYSTEM ---- */
Pages.system = {
  async load() {
    try {
      const [sys, cfg] = await Promise.all([
        API.get('/api/system-info'),
        API.get('/api/device-config'),
      ]);
      this.render(sys);
      document.getElementById('cfgHousehold').value  = cfg.household  || '';
      document.getElementById('cfgDeviceName').value = cfg.deviceName || '';
      try {
        const disp = await API.get('/api/display-config');
        document.getElementById('cfgStandby').value = String(disp.standby_sec ?? 0);
      } catch(_) {}
    } catch(e) {
      Toast.error('System: ' + e.message);
    }
  },

  async saveDeviceConfig(e) {
    e.preventDefault();
    try {
      const standbySec = parseInt(document.getElementById('cfgStandby').value, 10) || 0;
      await Promise.all([
        API.post('/api/device-config', {
          household:  document.getElementById('cfgHousehold').value.trim(),
          deviceName: document.getElementById('cfgDeviceName').value.trim(),
        }),
        API.post('/api/display-config', { standby_sec: standbySec }),
      ]);
      Toast.success('Gerät & Haushalt gespeichert');
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },

  render(sys) {
    document.getElementById('sysInfoList').innerHTML = `
      <dt>Haushalt</dt><dd>${esc(sys.household || 'Standard')}</dd>
      ${sys.deviceName ? `<dt>Gerätename</dt><dd>${esc(sys.deviceName)}</dd>` : ''}
      <dt>Firmware</dt><dd>${esc(sys.firmware || '—')}</dd>
      <dt>Uptime</dt><dd>${esc(sys.uptime || '—')}</dd>
      <dt>CPU-Frequenz</dt><dd>${esc(sys.cpuFreq || '—')} MHz</dd>
      <dt>IP-Adresse</dt><dd>${esc(sys.ip || '—')}</dd>
      <dt>MAC-Adresse</dt><dd>${esc(sys.mac || '—')}</dd>
      <dt>Hostname</dt><dd>${esc(sys.hostname || '—')}</dd>
      <dt>Freier Heap</dt><dd>${formatBytes(sys.heap || 0)}</dd>
      <dt>PSRAM</dt><dd>${formatBytes(sys.psram || 0)}</dd>`;

    const heapPct  = sys.heapTotal > 0 ? Math.round(sys.heap  / sys.heapTotal  * 100) : 0;
    const psramPct = sys.psramTotal> 0 ? Math.round(sys.psram / sys.psramTotal * 100) : 0;
    document.getElementById('sysStorageInfo').innerHTML =
      progressBar(sys.heapTotal - sys.heap, sys.heapTotal,  'Heap') +
      (sys.psramTotal > 0 ? progressBar(sys.psramTotal - sys.psram, sys.psramTotal, 'PSRAM') : '') +
      progressBar(sys.fsUsed || 0, sys.fsTotal || 1, 'LittleFS');
  },

  restart() {
    Modal.confirm('Neustart', 'Gerät wirklich neu starten?', async () => {
      try { await API.post('/api/restart', {}); Toast.info('Neustart…'); } catch {}
    });
  },

  factoryReset() {
    Modal.confirm('Werksreset', 'ALLE Daten löschen und auf Werkseinstellungen zurücksetzen?', async () => {
      try { await API.post('/api/factory-reset', {}); Toast.warn('Werksreset wird durchgeführt…'); } catch {}
    });
  },

  formatFS() {
    Modal.confirm('LittleFS formatieren', 'Dateisystem formatieren? Alle Daten werden gelöscht!', async () => {
      try { await API.post('/api/format-fs', {}); Toast.warn('Formatiert. Neustart erforderlich.'); } catch(e) { Toast.error(e.message); }
    });
  },

  async clearScanLog() {
    try { await API.post('/api/scan-log/clear', {}); Toast.success('Scanlog gelöscht'); } catch(e) { Toast.error(e.message); }
  },

  async clearCache() {
    try { await API.post('/api/cache/clear', {}); Toast.success('Cache gelöscht'); } catch(e) { Toast.error(e.message); }
  },
};

/* ---- NETWORK ---- */
Pages.network = {
  async load() {
    try {
      const cfg = await API.get('/api/wifi');
      document.getElementById('wifiSsid').value     = cfg.ssid     || '';
      document.getElementById('apSsid').value        = cfg.apSsid   || '';
      document.getElementById('apPass').value        = cfg.apPass   || '';
      document.getElementById('ntpServer').value     = cfg.ntpServer    || 'pool.ntp.org';
      document.getElementById('ntpTimezone').value   = cfg.timezone     || 'CET-1CEST,M3.5.0,M10.5.0/3';
      document.getElementById('ntpCurrentTime').textContent = cfg.currentTime || '—';

      const dot  = cfg.connected ? 'ok' : 'danger';
      const rssi = cfg.rssi ? `${cfg.rssi} dBm` : '';
      document.getElementById('wifiStatus').innerHTML = `
        <span class="status-dot ${dot}"></span>
        <span>${cfg.connected ? 'Verbunden' : 'Getrennt'}</span>
        ${cfg.ssid ? `<span class="muted">${esc(cfg.ssid)}</span>` : ''}
        ${rssi ? `<span class="muted">${rssi}</span>` : ''}`;
      this.renderScanResults(cfg.networks || [], cfg.scanSource || 'cache');
      if (!document.getElementById('wifiSsid').value.trim() && (cfg.networks || []).length) {
        this.selectWifi(cfg.networks[0].ssid, false);
      }
    } catch(e) {
      Toast.error('Netzwerk: ' + e.message);
    }
  },

  selectWifi(ssid, focusPassword = true) {
    document.getElementById('wifiSsid').value = ssid || '';
    if (focusPassword) document.getElementById('wifiPass')?.focus();
  },

  renderScanResults(nets, source = '') {
    const box = document.getElementById('wifiScanResults');
    if (!box) return;
    box.hidden = false;
    box.innerHTML = nets.length
      ? nets.map(n => `
        <div class="scan-result-item" onclick="Pages.network.selectWifi('${esc(n.ssid)}')">
          <span>${esc(n.ssid)}</span>
          <span class="scan-rssi">${n.rssi} dBm ${n.open ? '' : '&#128274;'}</span>
        </div>`).join('')
      : `<div class="muted" style="padding:8px">Keine Netzwerke gefunden${source ? ` (${esc(source)})` : ''}</div>`;
  },

  async scanWifi() {
    const btn = document.querySelector('#wifiForm .btn-sm');
    btn.textContent = 'Scanne…';
    btn.disabled = true;
    try {
      let res = await API.get('/api/wifi/scan-start');
      // New firmware returns the scan result directly; polling stays as fallback.
      let nets = [];
      if (!res.scanning && res.ready !== false) {
        if (res.error) throw new Error(`${res.error} (Code ${res.result})`);
        nets = res.networks || [];
      } else {
        // Poll until scan completes (up to 15s)
        for (let i = 0; i < 15; i++) {
          await new Promise(r => setTimeout(r, 1000));
          res = await API.get('/api/wifi/scan-result');
          if (!res.scanning && res.ready !== false) {
            if (res.error) throw new Error(`${res.error} (Code ${res.result})`);
            nets = res.networks || [];
            break;
          }
        }
      }
      this.renderScanResults(nets, res.source || 'scan');
    } catch(e) {
      Toast.error('Scan: ' + e.message);
    } finally {
      btn.textContent = 'Scannen';
      btn.disabled = false;
    }
  },

  async connectWifi(e) {
    e.preventDefault();
    const data = {
      ssid:     document.getElementById('wifiSsid').value.trim(),
      password: document.getElementById('wifiPass').value,
    };
    if (!data.ssid) { Toast.warn('Bitte erst ein WLAN aus der Liste auswählen oder die SSID eintragen'); return; }
    try {
      try {
        await API.post('/api/wifi/connect', data);
      } catch (primaryError) {
        console.warn('Fallback /api/wifi connect', primaryError);
        await API.post('/api/wifi', data);
      }
      Toast.info('Verbindung wird hergestellt…');
      // Poll status for up to 20s
      for (let i = 0; i < 20; i++) {
        await new Promise(r => setTimeout(r, 1000));
        const st = await API.get('/api/wifi/status').catch(() => ({}));
        if (st.connected) {
          Toast.success(`Verbunden! IP: ${st.ip}`);
          setTimeout(() => this.load(), 1000);
          return;
        }
        if (st.failed) { Toast.error('Verbindung fehlgeschlagen – SSID/Passwort prüfen'); return; }
      }
      Toast.warn('Timeout – Verbindung noch nicht hergestellt');
      this.load();
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },

  async saveAP(e) {
    e.preventDefault();
    try {
      await API.post('/api/wifi/ap', {
        apSsid: document.getElementById('apSsid').value.trim(),
        apPass: document.getElementById('apPass').value,
      });
      Toast.success('AP-Einstellungen gespeichert');
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },

  async saveNtp(e) {
    e.preventDefault();
    try {
      await API.post('/api/wifi', {
        ntpServer: document.getElementById('ntpServer').value.trim(),
        timezone:  document.getElementById('ntpTimezone').value.trim(),
      });
      Toast.success('NTP-Einstellungen gespeichert');
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },
};

/* ---- PRINTER ---- */
Pages.printer = {
  async load() {
    try {
      const cfg = await API.get('/api/printer-config');
      document.getElementById('prBaud').value     = cfg.baudrate  || 9600;
      document.getElementById('prPostFeed').value = cfg.postFeed ?? 86;
      document.getElementById('printerStatus').innerHTML =
        `<span class="status-dot ${cfg.ready ? 'ok' : 'warn'}"></span><span>${cfg.ready ? 'Bereit' : 'Nicht bereit'} · TTL TX ${cfg.txPin ?? '-'} / RX ${cfg.rxPin ?? '-'} · ${cfg.baudrate || 9600} Baud</span>`;
    } catch(e) {
      Toast.error('Drucker: ' + e.message);
    }
  },

  async save(e) {
    e.preventDefault();
    try {
      await API.post('/api/printer-config', {
        baudrate: parseInt(document.getElementById('prBaud').value),
        postFeed: parseInt(document.getElementById('prPostFeed').value),
      });
      Toast.success('Drucker-Einstellungen gespeichert');
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },

  async testLabel() {
    try {
      const res = await API.post('/api/test-print', { type: 'label' });
      Toast.success(res.message || 'Testlabel gesendet');
    } catch(e) { Toast.error('Fehler: ' + e.message); }
  },

  async setVolume() {
    const vol = parseInt(document.getElementById('audioVolume').value);
    try {
      const res = await API.post('/api/audio/volume', { volume: vol });
      res.ok ? Toast.success(`Lautstärke: ${vol}%`) : Toast.error(res.message || 'Fehler');
    } catch(e) { Toast.error('Fehler: ' + e.message); }
  },

  async audioTest() {
    const freq = parseInt(document.getElementById('audioFreq').value) || 1000;
    const dur  = parseInt(document.getElementById('audioDur').value)  || 300;
    await this._playTone(freq, dur);
  },

  async audioPreset(freq, dur) {
    document.getElementById('audioFreq').value = freq;
    document.getElementById('audioDur').value  = dur;
    await this._playTone(freq, dur);
  },

  async _playTone(freq, dur) {
    const el = document.getElementById('audioTestResult');
    if (el) el.textContent = `⏳ ${freq} Hz · ${dur} ms …`;
    try {
      const res = await API.post('/api/audio-test', { freq, dur });
      if (el) el.textContent = res.ok ? `✓ ${freq} Hz · ${dur} ms` : `✗ ${res.message || 'Fehler'}`;
      res.ok ? Toast.success(`Ton: ${freq} Hz, ${dur} ms`) : Toast.error(res.message || 'Fehler');
    } catch(e) {
      if (el) el.textContent = '✗ Fehler';
      Toast.error('Fehler: ' + e.message);
    }
  },

  async printManual() {
    const text  = document.getElementById('prManualText').value.trim();
    const align = document.querySelector('input[name="prManualAlign"]:checked')?.value || 'left';
    const el    = document.getElementById('prManualResult');
    if (!text) { Toast.error('Kein Text eingegeben'); return; }
    if (el) el.textContent = '⏳ Drucken …';
    try {
      const res = await API.post('/api/print-manual', { text, align });
      if (el) el.textContent = res.ok ? `✓ Gedruckt (${align})` : `✗ ${res.message || 'Fehler'}`;
      res.ok ? Toast.success('Manueller Druck gesendet') : Toast.error(res.message || 'Fehler');
    } catch(e) {
      if (el) el.textContent = '✗ Fehler';
      Toast.error('Fehler: ' + e.message);
    }
  },

};

/* ---- MANUELLES ETIKETT ---- */
Pages.manuallabel = {
  async load() {},

  async print() {
    const text  = document.getElementById('mlText').value.trim();
    const align = document.querySelector('input[name="mlAlign"]:checked')?.value || 'left';
    const el    = document.getElementById('mlResult');
    if (!text) { Toast.error('Kein Text eingegeben'); return; }
    if (el) el.textContent = '⏳ Drucken …';
    try {
      const res = await API.post('/api/print-manual', { text, align });
      if (el) el.textContent = res.ok ? `✓ Gedruckt (${align})` : `✗ ${res.message || 'Fehler'}`;
      res.ok ? Toast.success('Etikett gedruckt') : Toast.error(res.message || 'Fehler');
    } catch(e) {
      if (el) el.textContent = '✗ Fehler';
      Toast.error('Fehler: ' + e.message);
    }
  },
};

/* ---- SCANNER ---- */
Pages.scanner = {
  _pollTimer: null,
  _bleDevices: [],

  async load() {
    try {
      const cfg = await API.get('/api/scanner-config');
      document.getElementById('scanAutoReconnect').checked = cfg.autoReconnect !== false;
      document.getElementById('scanIdleTimeout').value = cfg.idleTimeoutMin ?? 0;
      document.getElementById('scanBleDevice').textContent = cfg.bleDevice || cfg.bleAddress || '—';
      const status = cfg.bleLastError ? `${cfg.bleStatus}: ${cfg.bleLastError}` : (cfg.bleStatus || (cfg.bleConnected ? 'connected' : 'disconnected'));
      document.getElementById('scanBleStatus').textContent = status;
      document.getElementById('scanLastResult').textContent = cfg.lastScan || '—';
    } catch(e) {
      Toast.error('Scanner: ' + e.message);
    }
  },

  async save(e) {
    e.preventDefault();
    try {
      await API.post('/api/scanner-config', {
        mode: 'ble_hid',
        autoReconnect: document.getElementById('scanAutoReconnect').checked,
        idleTimeoutMin: parseInt(document.getElementById('scanIdleTimeout').value, 10) || 0,
      });
      Toast.success('Bluetooth-Scanner-Einstellungen gespeichert');
      this.load();
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },

  async bleScan() {
    const box = document.getElementById('scanBleResults');
    box.hidden = false;
    box.innerHTML = '<div class="muted" style="padding:8px">Suche Bluetooth-HID-Scanner…</div>';
    try {
      const res = await API.get('/api/scanner/ble-scan');
      this._bleDevices = Array.isArray(res) ? res : [];
      box.innerHTML = this._bleDevices.length
        ? this._bleDevices.map((d, idx) => `
          <div class="scan-result-item" onclick="Pages.scanner.bleConnect(${idx})">
            <span>${esc(d.name || d.address)}</span>
            <span class="scan-rssi">${d.rssi} dBm ${d.hid ? 'HID' : ''}</span>
          </div>`).join('')
        : '<div class="muted" style="padding:8px">Keine Bluetooth-Geräte gefunden</div>';
    } catch(e) {
      Toast.error('Bluetooth-Scan: ' + e.message);
      box.hidden = true;
    }
  },

  async bleConnect(index) {
    const device = this._bleDevices[index];
    if (!device) { Toast.warn('Bluetooth-Gerät nicht mehr in der Liste'); return; }
    try {
      await API.post('/api/scanner/ble-connect', { address: device.address, name: device.name || '' });
      Toast.info('Bluetooth-Verbindung wird hergestellt…');
      this.pollConnectionStatus();
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },

  pollConnectionStatus() {
    if (this._pollTimer) clearInterval(this._pollTimer);
    let attempts = 0;
    this._pollTimer = setInterval(async () => {
      attempts++;
      try {
        const cfg = await API.get('/api/scanner-config');
        document.getElementById('scanBleStatus').textContent = cfg.bleStatus || '—';
        document.getElementById('scanBleDevice').textContent = cfg.bleDevice || cfg.bleAddress || '—';
        if (cfg.bleConnected) {
          clearInterval(this._pollTimer);
          this._pollTimer = null;
          Toast.success('Bluetooth-Scanner verbunden');
        } else if (cfg.bleStatus === 'error') {
          clearInterval(this._pollTimer);
          this._pollTimer = null;
          Toast.error('Bluetooth-Verbindung fehlgeschlagen: ' + (cfg.bleLastError || 'unbekannter Fehler'));
        } else if (attempts >= 20) {
          clearInterval(this._pollTimer);
          this._pollTimer = null;
          Toast.warn('Bluetooth-Verbindung dauert länger als erwartet');
        }
      } catch {}
    }, 1000);
  },

  async bleDisconnect() {
    try {
      await API.post('/api/scanner/ble-disconnect', {});
      Toast.info('Bluetooth-Scanner getrennt');
      this.load();
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },

  startTest() {
    document.getElementById('scanTestResult').textContent = 'Warte auf Bluetooth-Scan…';
    if (this._pollTimer) clearInterval(this._pollTimer);
    this._pollTimer = setInterval(async () => {
      try {
        const r = await API.get('/api/scanner-config');
        document.getElementById('scanBleStatus').textContent = r.bleStatus || '—';
        if (r.lastScan) {
          document.getElementById('scanTestResult').textContent = r.lastScan;
          document.getElementById('scanLastResult').textContent = r.lastScan;
        }
      } catch {}
    }, 1500);
    setTimeout(() => {
      if (this._pollTimer) { clearInterval(this._pollTimer); this._pollTimer = null; }
      document.getElementById('scanTestResult').textContent = 'Test beendet';
    }, 30000);
  },
};

/* ---- MQTT ---- */
Pages.mqtt = {
  async load() {
    try {
      const cfg = await API.get('/api/mqtt');
      document.getElementById('mqttHost').value   = cfg.host   || '';
      document.getElementById('mqttPort').value   = cfg.port   || 1883;
      document.getElementById('mqttUser').value   = cfg.user   || '';
      document.getElementById('mqttPrefix').value = cfg.prefix || '';
      document.getElementById('mqttHaDisc').checked = !!cfg.haDiscovery;

      const dot = cfg.connected ? 'ok' : 'danger';
      document.getElementById('mqttStatus').innerHTML =
        `<span class="status-dot ${dot}"></span><span>${cfg.connected ? 'Verbunden' : 'Getrennt'}</span>`;

      const prefix = cfg.prefix || 'scanner';
      document.getElementById('mqttTopics').innerHTML = [
        `${prefix}/eingelagert`,
        `${prefix}/ausgelagert`,
        `${prefix}/warnung`,
      ].map(t => `<div class="topic-item">${esc(t)}</div>`).join('');
    } catch(e) {
      Toast.error('MQTT: ' + e.message);
    }
  },

  async save(e) {
    e.preventDefault();
    try {
      await API.post('/api/mqtt', {
        host:        document.getElementById('mqttHost').value.trim(),
        port:        parseInt(document.getElementById('mqttPort').value) || 1883,
        user:        document.getElementById('mqttUser').value.trim(),
        password:    document.getElementById('mqttPass').value,
        prefix:      document.getElementById('mqttPrefix').value.trim(),
        haDiscovery: document.getElementById('mqttHaDisc').checked,
      });
      Toast.success('MQTT-Einstellungen gespeichert');
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },

  async testConnection() {
    try {
      const r = await API.post('/api/mqtt/test', {});
      Toast[r.ok ? 'success' : 'error'](r.message || (r.ok ? 'Verbunden' : 'Fehler'));
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },
};

/* ---- TELEGRAM ---- */
Pages.telegram = {
  async load() {
    try {
      const cfg = await API.get('/api/telegram');
      document.getElementById('tgToken').value  = cfg.token  || '';
      document.getElementById('tgChatId').value = cfg.chatId || '';
      const dot = cfg.connected ? 'ok' : (cfg.token ? 'warn' : '');
      document.getElementById('telegramStatus').innerHTML =
        `<span class="status-dot ${dot}"></span><span>${cfg.connected ? 'Verbunden' : (cfg.token ? 'Nicht verbunden' : 'Nicht konfiguriert')}</span>`;
    } catch(e) {
      Toast.error('Telegram: ' + e.message);
    }
  },

  async save(e) {
    e.preventDefault();
    try {
      await API.post('/api/telegram', {
        token:  document.getElementById('tgToken').value.trim(),
        chatId: document.getElementById('tgChatId').value.trim(),
      });
      Toast.success('Telegram-Einstellungen gespeichert');
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },

  async sendTest() {
    try {
      const r = await API.post('/api/telegram/test', {});
      Toast[r.ok ? 'success' : 'error'](r.message || (r.ok ? 'Testnachricht gesendet' : 'Fehler'));
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },
};

/* ---- SERVER-SYNC ---- */
Pages.serversync = {
  async load() {
    try {
      const cfg = await API.get('/api/server-sync');
      document.getElementById('syncIp').value   = cfg.ip   || '';
      document.getElementById('syncUser').value = cfg.user || '';
      document.getElementById('syncPass').value = cfg.pass || '';
      document.getElementById('syncLastSync').textContent = cfg.lastSync ? formatDate(cfg.lastSync) : '—';
      const dot = cfg.connected ? 'ok' : 'danger';
      document.getElementById('syncStatus').innerHTML =
        `<span class="status-dot ${dot}"></span><span>${cfg.connected ? 'Verbunden' : 'Getrennt'}</span>`;
    } catch(e) {
      Toast.error('Server-Sync: ' + e.message);
    }
  },

  async save(e) {
    e.preventDefault();
    try {
      await API.post('/api/server-sync', {
        ip:   document.getElementById('syncIp').value.trim(),
        user: document.getElementById('syncUser').value.trim(),
        pass: document.getElementById('syncPass').value,
      });
      Toast.success('Verbindungseinstellungen gespeichert');
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },

  async testConnection() {
    const btn = document.getElementById('btnTestSync');
    const orig = btn.textContent;
    btn.disabled = true;
    btn.textContent = 'Teste…';
    try {
      const r = await API.post('/api/server-sync/test', {});
      Toast[r.ok ? 'success' : 'error'](r.message || (r.ok ? 'Verbunden' : 'Fehler'));
    } catch(e) {
      Toast.error('Verbindungstest: ' + e.message);
    } finally {
      btn.disabled = false;
      btn.textContent = orig;
    }
  },

  async showQueue() {
    const btn = document.getElementById('btnShowQueue');
    const orig = btn.textContent;
    btn.disabled = true;
    btn.textContent = 'Lädt…';
    try {
      const q = await API.get('/api/server-sync/queue');
      const card = document.getElementById('syncQueueCard');
      const list = document.getElementById('syncQueue');
      const items = Array.isArray(q) ? q : [];
      list.innerHTML = items.length
        ? items.map(qi => `<div class="item-row"><div class="item-row-name">${esc(qi.type||'')}</div><div class="item-row-sub">${esc(qi.data||'')}</div></div>`).join('')
        : '<div class="muted" style="padding:8px">Queue ist leer</div>';
      card.hidden = false;
      card.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
    } catch(e) {
      Toast.error('Queue: ' + e.message);
    } finally {
      btn.disabled = false;
      btn.textContent = orig;
    }
  },

  clearQueue() {
    Modal.confirm('Queue löschen', 'Sync-Queue wirklich löschen?', async () => {
      try {
        await API.post('/api/server-sync/queue/clear', {});
        Toast.success('Queue gelöscht');
        document.getElementById('syncQueueCard').hidden = true;
      } catch(e) {
        Toast.error('Fehler: ' + e.message);
      }
    });
  },

  toggleSetup() {
    const body    = document.getElementById('syncSetupBody');
    const chevron = document.getElementById('syncSetupChevron');
    body.hidden   = !body.hidden;
    chevron.style.transform = body.hidden ? '' : 'rotate(180deg)';
  },

  async runSetup() {
    const host  = document.getElementById('wizHost').value.trim();
    const rUser = document.getElementById('wizRootUser').value.trim();
    const rPass = document.getElementById('wizRootPass').value;
    const sPass = document.getElementById('wizSyncPass').value;
    const sPass2= document.getElementById('wizSyncPass2').value;

    if (!host || !rUser || !rPass || !sPass) {
      Toast.warn('Alle Felder ausfüllen');
      return;
    }
    if (sPass !== sPass2) {
      Toast.warn('Sync-Passwörter stimmen nicht überein');
      return;
    }
    if (sPass.length < 8) {
      Toast.warn('Sync-Passwort muss mindestens 8 Zeichen haben');
      return;
    }

    // Show progress
    document.getElementById('syncWizStep1').hidden = true;
    document.getElementById('syncWizStep2').hidden = false;
    document.getElementById('syncWizStep3').hidden = true;

    try {
      const r = await API.slowPost('/api/server-sync/setup', {
        host: host, rootUser: rUser, rootPass: rPass, syncPass: sPass,
      });

      document.getElementById('syncWizStep2').hidden = true;
      document.getElementById('syncWizStep3').hidden = false;
      const out = document.getElementById('syncWizStep3');

      if (r.ok) {
        const syncUser = r.syncUser || 'lebensmittel_sync';
        // The backend already saved ip/user/pass — just update the DOM fields.
        document.getElementById('syncIp').value   = host;
        document.getElementById('syncUser').value = syncUser;
        document.getElementById('syncPass').value = sPass;

        out.innerHTML = `
          <div style="border:1px solid var(--ok);border-radius:8px;padding:16px;background:rgba(80,200,100,.08)">
            <div style="font-weight:600;color:var(--ok);margin-bottom:8px">&#10003; Einrichtung erfolgreich</div>
            <div style="font-size:.85rem;line-height:1.7">
              ${esc(r.message || '')}<br>
              Die Verbindungsdaten wurden automatisch gespeichert.
            </div>
            <button class="btn btn-ok" style="margin-top:12px" onclick="Pages.serversync.load()">&#8635; Status aktualisieren</button>
          </div>`;
        // Clear root credentials from DOM
        document.getElementById('wizRootPass').value  = '';
        document.getElementById('wizSyncPass').value  = '';
        document.getElementById('wizSyncPass2').value = '';
      } else {
        out.innerHTML = `
          <div style="border:1px solid var(--danger);border-radius:8px;padding:16px;background:rgba(220,60,60,.08)">
            <div style="font-weight:600;color:var(--danger);margin-bottom:8px">&#10007; Fehler</div>
            <div style="font-size:.85rem">${esc(r.error || 'Unbekannter Fehler')}</div>
            <button class="btn" style="margin-top:12px" onclick="Pages.serversync._wizBack()">&#8592; Zurück</button>
          </div>`;
      }
    } catch(e) {
      document.getElementById('syncWizStep2').hidden = true;
      document.getElementById('syncWizStep3').hidden = false;
      document.getElementById('syncWizStep3').innerHTML = `
        <div style="border:1px solid var(--danger);border-radius:8px;padding:16px;background:rgba(220,60,60,.08)">
          <div style="font-weight:600;color:var(--danger);margin-bottom:8px">&#10007; Verbindungsfehler</div>
          <div style="font-size:.85rem">${esc(e.message)}</div>
          <button class="btn" style="margin-top:12px" onclick="Pages.serversync._wizBack()">&#8592; Zurück</button>
        </div>`;
    }
  },

  _wizBack() {
    document.getElementById('syncWizStep1').hidden = false;
    document.getElementById('syncWizStep2').hidden = true;
    document.getElementById('syncWizStep3').hidden = true;
  },
};

/* ---- STATISTICS ---- */
Pages.statistics = {
  async load() {
    try {
      const [stats, scanlog] = await Promise.all([
        API.get('/api/stats'),
        API.get('/api/scan-log'),
      ]);
      this.render(stats, Array.isArray(scanlog) ? scanlog : []);
    } catch(e) {
      Toast.error('Statistiken: ' + e.message);
    }
  },

  render(stats, scanlog) {
    document.getElementById('statsTopCards').innerHTML = `
      <div class="stat-card stat-accent">
        <div class="stat-label">Gesamt eingelagert</div>
        <div class="stat-value">${stats.totalStored || 0}</div>
      </div>
      <div class="stat-card stat-ok">
        <div class="stat-label">Verbraucht</div>
        <div class="stat-value">${stats.totalConsumed || 0}</div>
      </div>
      <div class="stat-card stat-danger">
        <div class="stat-label">Weggeworfen</div>
        <div class="stat-value">${stats.totalWasted || 0}</div>
      </div>
      <div class="stat-card stat-warn">
        <div class="stat-label">Ø Lagerdauer</div>
        <div class="stat-value">${stats.avgStorageDays || 0}d</div>
      </div>`;

    const top = stats.topProducts || [];
    const maxTop = Math.max(...top.map(p => p.count || 0), 1);
    document.getElementById('statsTopProducts').innerHTML = top.length
      ? top.slice(0,10).map(p => `
        <div class="bar-row">
          <span class="bar-label">${esc(p.name)}</span>
          <div class="bar-track"><div class="bar-fill" style="width:${Math.round(p.count/maxTop*100)}%"></div></div>
          <span class="bar-count">${p.count}</span>
        </div>`).join('')
      : '<div class="muted">Keine Daten</div>';

    const cats = stats.categoryUsage || [];
    const maxCat = Math.max(...cats.map(c => c.count || 0), 1);
    document.getElementById('statsCatUsage').innerHTML = cats.length
      ? cats.map(c => `
        <div class="bar-row">
          <span class="bar-label">${esc(c.name)}</span>
          <div class="bar-track"><div class="bar-fill" style="width:${Math.round(c.count/maxCat*100)}%;background:var(--ok)"></div></div>
          <span class="bar-count">${c.count}</span>
        </div>`).join('')
      : '<div class="muted">Keine Daten</div>';

    const hist = stats.history || [];
    document.getElementById('statsBody').innerHTML = hist.length
      ? hist.map(h => `
        <tr>
          <td>${formatDate(h.date)}</td>
          <td>${h.stored || 0}</td>
          <td>${h.consumed || 0}</td>
          <td>${h.wasted || 0}</td>
        </tr>`).join('')
      : '<tr><td colspan="4" class="muted" style="text-align:center;padding:16px">Keine Verlaufsdaten</td></tr>';

    document.getElementById('scanlogBody').innerHTML = scanlog.length
      ? scanlog.slice(-50).reverse().map(l => `
        <tr>
          <td class="mono" style="font-size:.75rem">${esc(l.time || l.timestamp || '')}</td>
          <td class="mono">${esc(l.barcode || '')}</td>
          <td>${esc(l.action || '')}</td>
          <td>${esc(l.product || '')}</td>
        </tr>`).join('')
      : '<tr><td colspan="4" class="muted" style="text-align:center;padding:16px">Keine Einträge</td></tr>';
  },
};

/* ---- OTA ---- */
Pages.ota = {
  _ws: null,

  load() {},

  async uploadFile(e) {
    e.preventDefault();
    const file = document.getElementById('otaFile').files[0];
    if (!file) { Toast.warn('Datei wählen'); return; }
    const card = document.getElementById('otaProgressCard');
    card.hidden = false;
    document.getElementById('otaProgress').style.width = '0%';
    document.getElementById('otaProgressText').textContent = 'Hochladen…';
    document.getElementById('otaLog').innerHTML = '';

    try {
      const fd = new FormData();
      fd.append('firmware', file);
      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/api/update');
      xhr.upload.onprogress = (ev) => {
        if (ev.lengthComputable) {
          const pct = Math.round(ev.loaded / ev.total * 100);
          document.getElementById('otaProgress').style.width = `${pct}%`;
          document.getElementById('otaProgressText').textContent = `${pct}%`;
        }
      };
      xhr.onload = () => {
        if (xhr.status === 200) {
          document.getElementById('otaProgress').style.width = '100%';
          document.getElementById('otaProgressText').textContent = 'Abgeschlossen — Neustart…';
          Toast.success('Update erfolgreich. Gerät startet neu.');
        } else {
          Toast.error('Update fehlgeschlagen: ' + xhr.statusText);
        }
      };
      xhr.onerror = () => Toast.error('Upload-Fehler');
      xhr.send(fd);
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },

  async updateFromUrl(e) {
    e.preventDefault();
    const url = document.getElementById('otaUrl').value.trim();
    if (!url) { Toast.warn('URL erforderlich'); return; }
    const card = document.getElementById('otaProgressCard');
    card.hidden = false;
    document.getElementById('otaProgress').style.width = '0%';
    document.getElementById('otaProgressText').textContent = 'Starte Update…';
    try {
      await API.post('/api/ota-url', { url });
      Toast.info('Update gestartet. Fortschritt über WebSocket…');
      this._pollProgress();
    } catch(e) {
      Toast.error('Fehler: ' + e.message);
    }
  },

  _pollProgress() {
    let pct = 0;
    const iv = setInterval(() => {
      pct = Math.min(pct + 5, 95);
      document.getElementById('otaProgress').style.width = `${pct}%`;
      document.getElementById('otaProgressText').textContent = `${pct}%`;
    }, 2000);
    setTimeout(() => clearInterval(iv), 40000);
  },

  async fetchGithubRelease() {
    const repo = document.getElementById('otaGithub').value.trim();
    if (!repo) { Toast.warn('GitHub Repo eingeben (user/repo)'); return; }
    try {
      const url = `https://api.github.com/repos/${repo}/releases/latest`;
      const res = await fetch(url);
      if (!res.ok) throw new Error('Nicht gefunden');
      const data = await res.json();
      const asset = data.assets?.find(a => a.name.endsWith('.bin'));
      if (asset) {
        document.getElementById('otaUrl').value = asset.browser_download_url;
        Toast.info(`Gefunden: ${asset.name}`);
      } else {
        Toast.warn('Kein .bin Asset gefunden');
      }
    } catch(e) {
      Toast.error('GitHub: ' + e.message);
    }
  },
};

/* ---- LOGS ---- */
Pages.logs = {
  _all: [],
  _pollTimer: null,

  async load() {
    await this.fetchLogs();
    if (!this._pollTimer) {
      this._pollTimer = setInterval(() => this.fetchLogs(), 5000);
    }
  },

  async fetchLogs() {
    try {
      const logs = await API.get('/api/logs');
      this._all = Array.isArray(logs) ? logs : [];
      this.applyFilter();
    } catch {}
  },

  applyFilter() {
    const checked = [...document.querySelectorAll('.filter-chip input:checked')].map(i => i.value);
    const filtered = this._all.filter(l => checked.includes(l.level || 'INFO'));
    const out = document.getElementById('logOutput');
    out.innerHTML = filtered.map(l =>
      `<div class="log-line log-${l.level || 'INFO'}">[${esc(l.time||'')}] [${esc(l.level||'INFO')}] [${esc(l.module||'')}] ${esc(l.message||'')}</div>`
    ).join('');
    if (document.getElementById('logAutoScroll')?.checked) {
      out.scrollTop = out.scrollHeight;
    }
  },

  download() {
    const text = this._all.map(l =>
      `[${l.time||''}] [${l.level||'INFO'}] [${l.module||''}] ${l.message||''}`
    ).join('\n');
    const blob = new Blob([text], { type: 'text/plain' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `scanner-logs-${Date.now()}.txt`;
    a.click();
  },

  clear() {
    Modal.confirm('Logs löschen', 'Alle Logs löschen?', async () => {
      try {
        await API.post('/api/logs/clear', {});
        this._all = [];
        this.applyFilter();
        Toast.success('Logs gelöscht');
      } catch(e) {
        Toast.error('Fehler: ' + e.message);
      }
    });
  },

  cleanup() {
    if (this._pollTimer) { clearInterval(this._pollTimer); this._pollTimer = null; }
  },
};

/* ============================================================
   APP
   ============================================================ */
const App = {
  _sidebarOpen: false,

  toggleSidebar() {
    this._sidebarOpen = !this._sidebarOpen;
    this._applySidebar();
  },

  closeSidebar() {
    this._sidebarOpen = false;
    this._applySidebar();
  },

  _applySidebar() {
    document.getElementById('sidebar').classList.toggle('open', this._sidebarOpen);
    document.getElementById('sidebarOverlay').classList.toggle('visible', this._sidebarOpen);
    document.getElementById('hamburgerBtn').classList.toggle('open', this._sidebarOpen);
  },

  init() {
    document.getElementById('hamburgerBtn').addEventListener('click', () => this.toggleSidebar());
    document.getElementById('sidebarOverlay').addEventListener('click', () => this.closeSidebar());
    document.getElementById('refreshBtn').addEventListener('click', () => {
      const pg = Pages[Router.current()];
      if (pg && typeof pg.load === 'function') pg.load();
    });

    window.addEventListener('hashchange', () => {
      const hash = window.location.hash.slice(1);
      Router.go(hash || 'dashboard');
    });

    window.addEventListener('beforeunload', () => {
      Pages.logs.cleanup();
      if (Pages.scanner._pollTimer) clearInterval(Pages.scanner._pollTimer);
    });

    Router.init();
  },
};

/* ============================================================
   BOOT
   ============================================================ */
document.addEventListener('DOMContentLoaded', () => App.init());
