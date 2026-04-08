fetch('/logo.svg').then(r=>r.text()).then(s=>{document.getElementById('logo').innerHTML=s});
fetch('/api/version').then(r=>r.text()).then(v=>{document.getElementById('ver').textContent=v});

var infoLoaded = false;

document.querySelectorAll('.tab').forEach(tab => {
  tab.addEventListener('click', () => {
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
    tab.classList.add('active');
    document.getElementById(tab.dataset.view).classList.add('active');

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
    document.getElementById('c-pool').textContent = d.pool_host;
    document.getElementById('c-port').textContent = d.pool_port;
    document.getElementById('c-worker').textContent = d.worker;
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
