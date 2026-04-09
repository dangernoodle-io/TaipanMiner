fetch('/logo.svg').then(r=>r.text()).then(s=>{document.getElementById('logo').innerHTML=s});
fetch('/api/version').then(r=>r.text()).then(v=>{document.getElementById('ver').textContent=v});

var infoLoaded = false;
var settingsLoaded = false;

document.querySelectorAll('.tab').forEach(tab => {
  tab.addEventListener('click', () => {
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
    tab.classList.add('active');
    document.getElementById(tab.dataset.view).classList.add('active');

    if (tab.dataset.view === 'diagnostics') {
      startLogStream();
    } else {
      stopLogStream();
    }

    if (tab.dataset.view === 'info' && !infoLoaded) {
      fetch('/api/info').then(r=>r.json()).then(d => {
        document.getElementById('i-board').textContent = d.board;
        document.getElementById('i-version').textContent = d.version;
        document.getElementById('i-idf').textContent = d.idf_version;
        document.getElementById('i-cores').textContent = d.cores;
        document.getElementById('i-mac').textContent = d.mac;
        if (d.ssid) document.getElementById('i-ssid').textContent = d.ssid;
        if (d.free_heap !== undefined) {
          document.getElementById('i-heap').textContent = fmtBytes(d.free_heap) + ' / ' + fmtBytes(d.total_heap);
          document.getElementById('i-flash').textContent = fmtBytes(d.app_size) + ' / ' + fmtBytes(d.flash_size);
        }
        infoLoaded = true;
      }).catch(()=>{});
    }

    if (tab.dataset.view === 'settings' && !settingsLoaded) {
      loadSettings();
    }
  });
});

function fmtHash(h) {
  if (h >= 1e12) return (h/1e12).toFixed(1) + ' TH/s';
  if (h >= 1e9) return (h/1e9).toFixed(1) + ' GH/s';
  if (h >= 1e6) return (h/1e6).toFixed(1) + ' MH/s';
  if (h >= 1e3) return (h/1e3).toFixed(1) + ' kH/s';
  return h.toFixed(0) + ' H/s';
}

function fmtUptime(s) {
  if (s < 60) return s + 's';
  if (s < 3600) return Math.floor(s/60) + 'm ' + (s%60) + 's';
  var h = Math.floor(s/3600);
  var m = Math.floor((s%3600)/60);
  if (h < 24) return h + 'h ' + m + 'm';
  var d = Math.floor(h/24);
  return d + 'd ' + (h%24) + 'h';
}

function fmtDiff(d) {
  if (d >= 1e12) return (d/1e12).toFixed(1) + 'T';
  if (d >= 1e9) return (d/1e9).toFixed(1) + 'G';
  if (d >= 1e6) return (d/1e6).toFixed(1) + 'M';
  if (d >= 1e3) return (d/1e3).toFixed(1) + 'K';
  if (d >= 1) return d.toFixed(0);
  return parseFloat(d.toFixed(4));
}

function fmtLastShare(s) {
  if (s < 0) return '--';
  if (s < 60) return s + 's ago';
  if (s < 3600) return Math.floor(s/60) + 'm ago';
  return Math.floor(s/3600) + 'h ago';
}

function fmtBytes(b) {
  if (b >= 1048576) return (b/1048576).toFixed(1) + ' MB';
  if (b >= 1024) return (b/1024).toFixed(1) + ' KB';
  return b + ' B';
}

function refreshStats() {
  fetch('/api/stats').then(r=>r.json()).then(d => {
    document.getElementById('s-hashrate').textContent = fmtHash(d.hashrate);
    document.getElementById('s-hashrate-avg').textContent = fmtHash(d.hashrate_avg);
    document.getElementById('s-temp').textContent = d.temp_c.toFixed(1) + '\u00B0C';
    document.getElementById('s-uptime').textContent = fmtUptime(d.uptime_s);
    document.getElementById('s-shares').textContent = d.session_shares;
    document.getElementById('s-rejected').textContent = d.session_rejected;
    var total = d.session_shares + d.session_rejected;
    document.getElementById('s-acceptrate').textContent = total > 0 ? '(' + (100 * d.session_shares / total).toFixed(1) + '%)' : '';
    document.getElementById('s-lifetime').textContent = d.lifetime_shares;
    if (d.best_diff > 0 && d.pool_difficulty > 0) {
      var mult = d.best_diff / d.pool_difficulty;
      document.getElementById('s-bestdiff').textContent = fmtDiff(d.best_diff) + ' (' + Math.floor(mult) + 'x)';
    } else if (d.best_diff > 0) {
      document.getElementById('s-bestdiff').textContent = fmtDiff(d.best_diff);
    } else {
      document.getElementById('s-bestdiff').textContent = '--';
    }
    document.getElementById('s-lastshare').textContent = fmtLastShare(d.last_share_ago_s);
    document.getElementById('s-worker').textContent = d.worker;
    document.getElementById('s-pool').textContent = d.pool_host + ':' + d.pool_port;
    document.getElementById('s-pooldiff').textContent = d.pool_difficulty > 0 ? parseFloat(d.pool_difficulty.toFixed(4)) : '--';
    document.getElementById('i-host').textContent = d.pool_host;
    document.getElementById('i-port').textContent = d.pool_port;
    document.getElementById('i-worker').textContent = d.worker;
    if (d.wallet && d.wallet.length > 10) {
      document.getElementById('i-wallet').textContent = d.wallet.slice(0,6) + '...' + d.wallet.slice(-4);
    } else {
      document.getElementById('i-wallet').textContent = d.wallet || '--';
    }
    document.getElementById('u-version').textContent = d.version;
    document.getElementById('u-board').textContent = d.board;
    document.getElementById('u-build').textContent = d.build_date + ' ' + d.build_time;
  }).catch(()=>{});
}

refreshStats();
setInterval(refreshStats, 5000);

document.getElementById('otaCheckBtn').addEventListener('click', function() {
  var btn = document.getElementById('otaCheckBtn');
  var status = document.getElementById('otaStatus');
  status.style.display = 'block';
  status.textContent = 'Checking for updates...';
  btn.disabled = true;

  var pollStartTime = Date.now();
  var maxPollTime = 15000;
  var pollInterval = 2000;

  function doPoll() {
    fetch('/api/ota/check').then(r => {
      if (r.status === 202) {
        if (Date.now() - pollStartTime < maxPollTime) {
          setTimeout(doPoll, pollInterval);
        } else {
          status.textContent = 'Failed to check for updates';
          btn.disabled = false;
        }
      } else if (r.status === 200) {
        return r.json().then(d => {
          if (d.update_available) {
            status.textContent = 'Update available: ' + d.latest_version;
          } else {
            status.textContent = 'Firmware is up to date (' + d.current_version + ')';
          }
          btn.disabled = false;
        });
      } else {
        status.textContent = 'Failed to check for updates';
        btn.disabled = false;
      }
    }).catch(() => {
      status.textContent = 'Failed to check for updates';
      btn.disabled = false;
    });
  }

  doPoll();
});

try { document.getElementById('u-host').textContent = location.host; } catch(e) {}

var logEs = null;
var LOG_MAX_LINES = 200;

document.getElementById('log-panel').addEventListener('scroll', function() {
  var atBottom = this.scrollHeight - this.scrollTop - this.clientHeight < 8;
  document.getElementById('log-autoscroll').checked = atBottom;
});

function setLogStatus(msg, color) {
  var status = document.getElementById('log-status');
  if (status) { status.textContent = msg; status.style.color = color; }
}

function startLogStream() {
  if (logEs) return;
  setLogStatus('Connecting...', 'var(--label)');
  logEs = new EventSource('/api/logs?source=browser');
  logEs.onopen = function() { setLogStatus('Connected', '#4a4'); };
  logEs.onmessage = function(e) {
    var auto = document.getElementById('log-autoscroll').checked;
    var span = document.createElement('span');
    span.textContent = e.data + '\n';
    var panel = document.getElementById('log-panel');
    panel.appendChild(span);
    while (panel.childNodes.length > LOG_MAX_LINES) panel.removeChild(panel.firstChild);
    if (auto) panel.scrollTop = panel.scrollHeight;
  };
  logEs.onerror = function() {
    logEs.close();
    logEs = null;
    fetch('/api/logs/status').then(function(r) { return r.json(); }).then(function(d) {
      if (d.active && d.client === 'external') {
        setLogStatus('External client connected', '#ca4');
      } else {
        setLogStatus('Disconnected (retrying...)', '#c44');
      }
    }).catch(function() {
      setLogStatus('Disconnected (retrying...)', '#c44');
    });
    setTimeout(startLogStream, 3000);
  };
}

function stopLogStream() {
  if (!logEs) return;
  logEs.close();
  logEs = null;
  setLogStatus('Disconnected', 'var(--label)');
}

document.getElementById('rebootBtn').addEventListener('click', function() {
  if (!confirm('Reboot the device? Mining will be interrupted.')) return;
  var btn = this;
  btn.disabled = true;
  btn.textContent = 'Rebooting...';
  fetch('/api/reboot', { method: 'POST' }).then(function() {
    btn.textContent = 'Device is rebooting...';
  }).catch(function() {
    btn.textContent = 'Reboot Device';
    btn.disabled = false;
  });
});

var savedSettings = {};

function loadSettings() {
  fetch('/api/settings').then(function(r) { return r.json(); }).then(function(d) {
    savedSettings = d;

    // Populate read-only view
    document.getElementById('sv-pool').textContent = d.pool_host || '--';
    document.getElementById('sv-port').textContent = d.pool_port || '--';
    if (d.wallet && d.wallet.length > 10) {
      document.getElementById('sv-wallet').textContent = d.wallet.slice(0,6) + '...' + d.wallet.slice(-4);
    } else {
      document.getElementById('sv-wallet').textContent = d.wallet || '--';
    }
    document.getElementById('sv-worker').textContent = d.worker || '--';
    document.getElementById('sv-poolpass').textContent = d.pool_pass ? '••••••••' : '(none)';
    document.getElementById('sv-display-text').textContent = d.display_en ? 'On' : 'Off';
    document.getElementById('sv-otaskip-text').textContent = d.ota_skip_check ? 'On' : 'Off';

    // Populate edit form
    document.getElementById('set-pool').value = d.pool_host || '';
    document.getElementById('set-port').value = d.pool_port || '';
    document.getElementById('set-wallet').value = d.wallet || '';
    document.getElementById('set-worker').value = d.worker || '';
    document.getElementById('set-poolpass').value = d.pool_pass || '';
    document.getElementById('set-display').checked = d.display_en;
    document.getElementById('set-otaskip').checked = d.ota_skip_check;

    settingsLoaded = true;
  }).catch(function() {});
}

document.getElementById('editSettingsBtn').addEventListener('click', function() {
  document.getElementById('settings-view').style.display = 'none';
  document.getElementById('settings-edit').style.display = 'block';
  document.getElementById('settingsStatus').style.display = 'none';
});

document.getElementById('cancelSettingsBtn').addEventListener('click', function() {
  document.getElementById('settings-edit').style.display = 'none';
  document.getElementById('settings-view').style.display = 'block';
  settingsLoaded = false;
  loadSettings();
});

function setFieldError(inputId, errId, msg) {
  var input = document.getElementById(inputId);
  var err = document.getElementById(errId);
  if (msg) {
    input.classList.add('invalid');
    err.textContent = msg;
    err.classList.add('visible');
  } else {
    input.classList.remove('invalid');
    err.textContent = '';
    err.classList.remove('visible');
  }
}

// Clear inline errors on input
['set-pool', 'set-port', 'set-wallet', 'set-worker'].forEach(function(id) {
  document.getElementById(id).addEventListener('input', function() {
    var errId = 'err-' + id.replace('set-', '');
    setFieldError(id, errId, '');
  });
});

document.getElementById('saveSettingsBtn').addEventListener('click', function() {
  var btn = this;
  var status = document.getElementById('settingsStatus');

  // Validate required fields
  var pool = document.getElementById('set-pool').value.trim();
  var portStr = document.getElementById('set-port').value.trim();
  var wallet = document.getElementById('set-wallet').value.trim();
  var worker = document.getElementById('set-worker').value.trim();
  var poolpass = document.getElementById('set-poolpass').value;
  var displayEn = document.getElementById('set-display').checked;
  var otaSkip = document.getElementById('set-otaskip').checked;
  var port = parseInt(portStr, 10);

  var valid = true;
  setFieldError('set-pool', 'err-pool', pool ? '' : 'Required');
  setFieldError('set-port', 'err-port', (port && port >= 1 && port <= 65535) ? '' : 'Valid port (1\u201365535) required');
  setFieldError('set-wallet', 'err-wallet', wallet ? '' : 'Required');
  setFieldError('set-worker', 'err-worker', worker ? '' : 'Required');
  if (!pool || !port || port < 1 || port > 65535 || !wallet || !worker) return;

  // Build PATCH payload with only changed fields
  var payload = {};
  if (pool !== (savedSettings.pool_host || '')) payload.pool_host = pool;
  if (port !== (savedSettings.pool_port || 0)) payload.pool_port = port;
  if (wallet !== (savedSettings.wallet || '')) payload.wallet = wallet;
  if (worker !== (savedSettings.worker || '')) payload.worker = worker;
  if (poolpass !== (savedSettings.pool_pass || '')) payload.pool_pass = poolpass;
  if (displayEn !== savedSettings.display_en) payload.display_en = displayEn;
  if (otaSkip !== savedSettings.ota_skip_check) payload.ota_skip_check = otaSkip;

  if (Object.keys(payload).length === 0) {
    document.getElementById('settings-edit').style.display = 'none';
    document.getElementById('settings-view').style.display = 'block';
    return;
  }

  btn.disabled = true;
  btn.textContent = 'Saving...';
  status.style.display = 'none';

  fetch('/api/settings', {
    method: 'PATCH',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  }).then(function(r) {
    if (!r.ok) return r.text().then(function(t) { throw new Error(t); });
    return r.json();
  }).then(function(d) {
    if (d.reboot_required) {
      status.style.display = 'block';
      status.textContent = 'Settings saved. Rebooting...';
      btn.textContent = 'Rebooting...';
      setTimeout(function() {
        fetch('/api/reboot', { method: 'POST' });
      }, 500);
    } else {
      // Only display_en changed — no reboot needed
      status.style.display = 'block';
      status.textContent = 'Settings saved.';
      btn.textContent = 'Save Settings';
      btn.disabled = false;
      // Switch back to read-only view
      document.getElementById('settings-edit').style.display = 'none';
      document.getElementById('settings-view').style.display = 'block';
      settingsLoaded = false;
      loadSettings();
    }
  }).catch(function(err) {
    status.style.display = 'block';
    status.textContent = 'Failed to save: ' + err.message;
    btn.textContent = 'Save Settings';
    btn.disabled = false;
  });
});
