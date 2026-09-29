const state = {
  live: null,
  samples: [],
  history: [],
  historyPage: 1,
  pageSize: 10,
  map: null,
  markers: {},
  search: "",
  nodeFilter: "all"
};

const GIS_NODES = {
  node1: { name: "Node 1", lat: 17.6599, lng: 75.9064 },
  node2: { name: "Node 2", lat: 17.6612, lng: 75.9081 }
};

const STATUS = {
  0: "NORMAL",
  1: "WARNING",
  2: "HIGH",
  3: "CRITICAL"
};

function num(v, fallback = 0) {
  const n = Number(v);
  return Number.isFinite(n) ? n : fallback;
}

function first(...values) {
  for (const v of values) {
    if (v !== undefined && v !== null && v !== "") return v;
  }
  return undefined;
}

function setText(id, value) {
  const el = document.getElementById(id);
  if (el) el.textContent = value;
}

function parseActive(value) {
  if (typeof value === "boolean") return value;
  if (typeof value === "number") return value !== 0;
  if (typeof value === "string") {
    return ["true","1","online","connected","active"].includes(value.toLowerCase());
  }
  return false;
}

function severityFromValue(tilt, distance, vibration, crack = 0) {
  if (Math.abs(tilt) >= 12 || Math.abs(distance) >= 10 || Math.abs(vibration) >= .50 || Math.abs(crack) >= 5) return 3;
  if (Math.abs(tilt) >= 7 || Math.abs(distance) >= 5 || Math.abs(vibration) >= .25 || Math.abs(crack) >= 2) return 2;
  if (Math.abs(tilt) >= 3 || Math.abs(distance) >= 2 || Math.abs(vibration) >= .10 || Math.abs(crack) > 0) return 1;
  return 0;
}

function normalize(raw) {
  raw = raw || {};
  const n1 = raw.node1 || {};
  const n2 = raw.node2 || {};

  const timestamp = num(raw.timestamp, 0);
  const age = timestamp ? Date.now() / 1000 - timestamp : Infinity;
  // A reading older than 5 seconds is considered stale.
  // This prevents the dashboard from showing ONLINE when the ESP32
  // has stopped sending data.
  const fresh = age >= -2 && age <= 5;

  const readNode = n => {
    const tiltX = num(first(n.tilt_x, n.tiltX));
    const tiltY = num(first(n.tilt_y, n.tiltY));
    const vibration = num(n.vibration);
    const displacement = num(first(n.displacement, n.displacement_mm, n.displacementMm));
    const crack = num(first(n.crack, n.crackIndex));
    const explicitActive = first(
      n.active,
      n.connected,
      n.online
    );

    const active =
      fresh &&
      explicitActive !== undefined &&
      parseActive(explicitActive);
    const reportedStatus = Number(first(n.overallStatus, n.overall_status));
    const calculated = severityFromValue(Math.max(Math.abs(tiltX), Math.abs(tiltY)), displacement, vibration, crack);

    return {
      active, tiltX, tiltY, vibration, displacement, crack,
      tiltStatus: severityFromValue(Math.max(Math.abs(tiltX), Math.abs(tiltY)), 0, 0, 0),
      displacementStatus: severityFromValue(0, displacement, 0, 0),
      vibrationStatus: severityFromValue(0, 0, vibration, 0),
      overallStatus: Number.isFinite(reportedStatus) ? Math.max(reportedStatus, calculated) : calculated
    };
  };

  const a = readNode(n1);
  const b = readNode(n2);

  return {
    timestamp, fresh,
    n1: a, n2: b,
    systemActive: fresh && (a.active || b.active),
    maxTilt: Math.max(Math.abs(a.tiltX), Math.abs(a.tiltY), Math.abs(b.tiltX), Math.abs(b.tiltY)),
    maxDistance: Math.max(Math.abs(a.displacement), Math.abs(b.displacement)),
    maxVibration: Math.max(Math.abs(a.vibration), Math.abs(b.vibration)),
    maxCrack: Math.max(Math.abs(a.crack), Math.abs(b.crack))
  };
}

function statusText(code) {
  return STATUS[Math.max(0, Math.min(3, Number(code) || 0))];
}

function statusClass(code) {
  const s = Number(code) || 0;
  if (s >= 3) return "critical";
  if (s === 2) return "high";
  if (s === 1) return "warning";
  return "normal";
}

function setBadge(id, code) {
  const el = document.getElementById(id);
  if (!el) return;
  const cls = statusClass(code);
  el.textContent = statusText(code);
  el.className = el.className.replace(/\b(normal|warning|high|critical)\b/g, "").trim() + " " + cls;
}

function updateClock() {
  const now = new Date();
  setText("clock", now.toLocaleTimeString([], {hour12:false}));
  setText("date", now.toLocaleDateString([], {day:"2-digit", month:"short", year:"numeric"}));
}

function updateTopStatus(data) {
  const overall = Math.max(data.n1.overallStatus, data.n2.overallStatus);
  const displayedOverall =
    data.systemActive ? overall : 3;

  setBadge("overallStatus", displayedOverall);

  if (data.n1.active) {
    setBadge("node1TopStatus", data.n1.overallStatus);
    setText("node1TopText", "All parameters within safe range");
  } else {
    setBadge("node1TopStatus", 3);
    setText("node1TopText", "Node disconnected");
  }

  if (data.n2.active) {
    setBadge("node2TopStatus", data.n2.overallStatus);
    setText("node2TopText", "All parameters within safe range");
  } else {
    setBadge("node2TopStatus", 3);
    setText("node2TopText", "Node disconnected");
  }

  const online = data.systemActive;
  setText("systemStatus", online ? "System Online" : "System Offline");
  setText("sidebarConnection", online ? "System Online" : "System Offline");

  ["systemDot","sidebarDot"].forEach(id => {
    const el = document.getElementById(id);
    if (el) el.classList.toggle("offline", !online);
  });
}

function updateNodeUI(number, node) {
  setText(`n${number}Tilt`, `${node.tiltX.toFixed(2)}°`);
  setText(`n${number}Vibration`, node.vibration.toFixed(2));
  setText(`n${number}Distance`, `${node.displacement.toFixed(2)}`);
  setText(`n${number}Crack`, node.crack.toFixed(2));

  const panel = document.getElementById(`nodePanel${number}`);
  const pill = document.getElementById(`node${number}Active`);
  const dot = panel ? panel.querySelector(".node-dot") : null;

  if (panel) panel.classList.toggle("offline", !node.active);
  if (pill) {
    pill.textContent = node.active ? "Online" : "Offline";
    pill.classList.toggle("offline", !node.active);
  }
  if (dot) dot.classList.toggle("offline", !node.active);
}

function updateRisk(data) {
  const overall = Math.max(data.n1.overallStatus, data.n2.overallStatus);
  const cls = statusClass(overall);
  const label = statusText(overall);

  setBadge("riskBadge", overall);
  setText("riskLabel", label);
  setText("riskTiltValue", `${data.maxTilt.toFixed(2)}°`);
  setText("riskDistanceValue", `${data.maxDistance.toFixed(2)} mm`);
  setText("riskVibrationValue", `${data.maxVibration.toFixed(2)} g`);
  setText("riskCrackValue", data.maxCrack.toFixed(2));

  const gauge = document.getElementById("riskGauge");
  const icon = document.getElementById("riskIcon");
  if (gauge) gauge.style.filter = cls === "normal" ? "none" : cls === "warning" ? "hue-rotate(35deg)" : "hue-rotate(130deg)";
  if (icon) icon.textContent = overall >= 2 ? "⚠" : "⌁";

  if (overall >= 3) {
    setText("riskSummaryTitle", "Critical ground condition detected");
    setText("riskSummaryText", "One or more monitored parameters have reached the configured critical condition.");
  } else if (overall === 2) {
    setText("riskSummaryTitle", "High condition detected");
    setText("riskSummaryText", "One or more monitored parameters are in the high condition range.");
  } else if (overall === 1) {
    setText("riskSummaryTitle", "Warning condition detected");
    setText("riskSummaryText", "A monitored parameter has moved above its normal operating range.");
  } else {
    setText("riskSummaryTitle", "System condition is normal");
    setText("riskSummaryText", "No significant abnormal sensor condition detected.");
  }
}

function updateMap(data) {
  if (!state.map) return;

  [["node1", data.n1], ["node2", data.n2]].forEach(([key, node]) => {
    const status = node.active ? statusClass(node.overallStatus) : "offline";
    const color = status === "normal" ? "#22e875" : status === "warning" ? "#ffbd22" : status === "high" || status === "critical" ? "#ff4050" : "#64748b";
    const marker = state.markers[key];
    if (!marker) return;

    marker.setIcon(L.divIcon({
      className: "strataguard-node-marker",
      html: `<div style="width:18px;height:18px;border-radius:50%;background:${color};border:3px solid white;box-shadow:0 0 18px ${color}"></div>`,
      iconSize:[24,24],
      iconAnchor:[12,12]
    }));

    marker.setPopupContent(`
      <div style="min-width:190px;color:#111827;font-family:Arial">
        <strong>${GIS_NODES[key].name}</strong><br>
        Status: <b>${status.toUpperCase()}</b><hr>
        Tilt X: ${node.tiltX.toFixed(2)}°<br>
        Tilt Y: ${node.tiltY.toFixed(2)}°<br>
        Displacement: ${node.displacement.toFixed(2)} mm<br>
        Vibration: ${node.vibration.toFixed(2)} g<br>
        Crack: ${node.crack.toFixed(2)}
      </div>
    `);
  });
}

function initMap() {
  const el = document.getElementById("gisMap");
  if (!el || typeof L === "undefined") return;

  state.map = L.map(el, {zoomControl:true}).setView([17.66055,75.90725], 15);

  L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
    maxZoom:19,
    attribution:"&copy; OpenStreetMap contributors"
  }).addTo(state.map);

  const positions = [
    ["node1", GIS_NODES.node1.lat, GIS_NODES.node1.lng],
    ["node2", GIS_NODES.node2.lat, GIS_NODES.node2.lng]
  ];

  positions.forEach(([key,lat,lng]) => {
    state.markers[key] = L.marker([lat,lng]).addTo(state.map);
    L.circle([lat,lng], {radius:100,color:"#ffffff",weight:1,opacity:.25,fillColor:"#ffffff",fillOpacity:.04}).addTo(state.map);
  });

  L.polyline(
    [[GIS_NODES.node1.lat,GIS_NODES.node1.lng],[GIS_NODES.node2.lat,GIS_NODES.node2.lng]],
    {color:"#ffffff",weight:1,dashArray:"5 7",opacity:.5}
  ).addTo(state.map);

  state.map.fitBounds(L.latLngBounds([
    [GIS_NODES.node1.lat,GIS_NODES.node1.lng],
    [GIS_NODES.node2.lat,GIS_NODES.node2.lng]
  ]), {padding:[35,35]});

  setTimeout(() => state.map.invalidateSize(), 300);
}

function addSample(data) {
  state.samples.push({
    tilt: data.maxTilt,
    distance: data.maxDistance,
    vibration: data.maxVibration,
    crack: data.maxCrack
  });
  if (state.samples.length > 30) state.samples.shift();
}

function drawTrendChart() {
  const canvas = document.getElementById("trendChart");
  if (!canvas || !state.samples.length) return;

  const rect = canvas.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, rect.width * ratio);
  canvas.height = Math.max(1, rect.height * ratio);

  const ctx = canvas.getContext("2d");
  ctx.setTransform(ratio,0,0,ratio,0,0);

  const w = rect.width, h = rect.height;
  ctx.clearRect(0,0,w,h);

  const pad = {l:30,r:10,t:8,b:18};
  const pw = w-pad.l-pad.r, ph = h-pad.t-pad.b;

  for(let i=0;i<=4;i++){
    const y=pad.t+ph*i/4;
    ctx.strokeStyle="rgba(255,255,255,.08)";
    ctx.beginPath();ctx.moveTo(pad.l,y);ctx.lineTo(w-pad.r,y);ctx.stroke();
  }

  const series = [
    ["tilt","#ff4f98"],
    ["distance","#19c8ff"],
    ["vibration","#ff8b22"],
    ["crack","#9d68ff"]
  ];

  const maxes = {
    tilt: Math.max(3,...state.samples.map(x=>x.tilt)),
    distance: Math.max(2,...state.samples.map(x=>x.distance)),
    vibration: Math.max(.1,...state.samples.map(x=>x.vibration)),
    crack: Math.max(1,...state.samples.map(x=>x.crack))
  };

  series.forEach(([key,color])=>{
    const vals=state.samples.map(x=>x[key]);
    ctx.strokeStyle=color;
    ctx.lineWidth=2;
    ctx.beginPath();
    vals.forEach((v,i)=>{
      const x=pad.l+(vals.length===1?pw/2:pw*i/(vals.length-1));
      const y=pad.t+ph-(v/maxes[key])*ph;
      i?ctx.lineTo(x,y):ctx.moveTo(x,y);
    });
    ctx.stroke();
  });
}

function drawSpark(id, values) {
  const canvas=document.getElementById(id);
  if(!canvas || values.length<2) return;

  const rect=canvas.getBoundingClientRect();
  const ratio=window.devicePixelRatio||1;
  canvas.width=Math.max(1,rect.width*ratio);
  canvas.height=Math.max(1,rect.height*ratio);
  const ctx=canvas.getContext("2d");
  ctx.setTransform(ratio,0,0,ratio,0,0);

  const min=Math.min(...values), max=Math.max(...values), range=max-min||1;
  ctx.clearRect(0,0,rect.width,rect.height);
  ctx.strokeStyle="#ffffff";
  ctx.lineWidth=1.4;
  ctx.beginPath();

  values.forEach((v,i)=>{
    const x=values.length===1?rect.width/2:rect.width*i/(values.length-1);
    const y=rect.height-2-((v-min)/range)*(rect.height-4);
    i?ctx.lineTo(x,y):ctx.moveTo(x,y);
  });
  ctx.stroke();
}

function redraw() {
  drawTrendChart();
  const h=state.samples;
  drawSpark("n1TiltSpark",h.map(x=>x.tilt));
  drawSpark("n1VibrationSpark",h.map(x=>x.vibration));
  drawSpark("n1DistanceSpark",h.map(x=>x.distance));
  drawSpark("n1CrackSpark",h.map(x=>x.crack));
  drawSpark("n2TiltSpark",h.map(x=>x.tilt));
  drawSpark("n2VibrationSpark",h.map(x=>x.vibration));
  drawSpark("n2DistanceSpark",h.map(x=>x.distance));
  drawSpark("n2CrackSpark",h.map(x=>x.crack));
}

async function fetchLiveData() {
  try {
    const response=await fetch(`/api/data?t=${Date.now()}`,{cache:"no-store"});
    if(!response.ok) throw new Error(`HTTP ${response.status}`);
    const raw=await response.json();
    const data=normalize(raw);

    state.live=data;
    addSample(data);

    updateTopStatus(data);
    updateNodeUI(1,data.n1);
    updateNodeUI(2,data.n2);
    updateRisk(data);
    updateMap(data);
    redraw();
  } catch(error) {
    console.error("Live data error:",error);
    setText("systemStatus","Server Offline");
    setText("sidebarConnection","Server Offline");
  }
}

function formatTime(ts) {
  const d=new Date(num(ts)*1000);
  if(Number.isNaN(d.getTime())) return "--";
  return d.toLocaleString([],{
    year:"numeric",month:"2-digit",day:"2-digit",
    hour:"2-digit",minute:"2-digit",second:"2-digit",
    hour12:false
  });
}

function historyStatus(row) {
  const reported=Number(row.overall_status);
  if(Number.isFinite(reported)) return Math.max(0,Math.min(3,reported));
  return severityFromValue(
    Math.max(Math.abs(num(row.tilt_x)),Math.abs(num(row.tilt_y))),
    num(row.displacement),
    num(row.vibration),
    num(row.crack)
  );
}

function getFilteredHistory() {
  const from=document.getElementById("historyDateFrom")?.value || "";
  const to=document.getElementById("historyDateTo")?.value || "";

  return state.history.filter(row=>{
    if(state.nodeFilter!=="all" && String(row.node_id)!==String(state.nodeFilter)) return false;

    const search=state.search.trim().toLowerCase();
    if(search){
      const hay=[
        `node ${row.node_id}`,
        row.tilt_x,row.tilt_y,row.vibration,row.displacement,row.crack,
        statusText(historyStatus(row))
      ].join(" ").toLowerCase();
      if(!hay.includes(search)) return false;
    }

    const day=new Date(num(row.timestamp)*1000);
    const iso=day.toISOString().slice(0,10);
    if(from && iso<from) return false;
    if(to && iso>to) return false;

    return true;
  });
}

function renderPagination(totalPages) {
  const el=document.getElementById("pagination");
  if(!el) return;

  el.innerHTML="";
  const prev=document.createElement("button");
  prev.className="page-btn";
  prev.textContent="‹";
  prev.disabled=state.historyPage<=1;
  prev.onclick=()=>{state.historyPage--;renderHistory()};
  el.appendChild(prev);

  for(let i=1;i<=totalPages;i++){
    if(totalPages>7 && i>3 && i<totalPages-1 && Math.abs(i-state.historyPage)>1){
      if(i===4){
        const dots=document.createElement("span");
        dots.textContent="…";
        dots.style.padding="0 4px";
        el.appendChild(dots);
      }
      continue;
    }
    const btn=document.createElement("button");
    btn.className="page-btn"+(i===state.historyPage?" active":"");
    btn.textContent=i;
    btn.onclick=()=>{state.historyPage=i;renderHistory()};
    el.appendChild(btn);
  }

  const next=document.createElement("button");
  next.className="page-btn";
  next.textContent="›";
  next.disabled=state.historyPage>=totalPages;
  next.onclick=()=>{state.historyPage++;renderHistory()};
  el.appendChild(next);
}

function renderHistory() {
  const rows=getFilteredHistory();
  const pageSize=state.pageSize;
  const totalPages=Math.max(1,Math.ceil(rows.length/pageSize));
  state.historyPage=Math.min(state.historyPage,totalPages);

  const start=(state.historyPage-1)*pageSize;
  const visible=rows.slice(start,start+pageSize);

  const body=document.getElementById("historyTableBody");
  if(!body) return;

  if(!visible.length){
    body.innerHTML=`<tr><td colspan="7" class="history-empty">No historical sensor data matches the current filters.</td></tr>`;
  }else{
    body.innerHTML=visible.map(row=>{
      const code=historyStatus(row);
      const cls=statusClass(code);
      return `
        <tr>
          <td>${formatTime(row.timestamp)}</td>
          <td><span class="history-node">Node ${row.node_id}</span></td>
          <td>${num(row.tilt_x).toFixed(2)}°</td>
          <td>${num(row.vibration).toFixed(2)} g</td>
          <td>${num(row.displacement).toFixed(2)} mm</td>
          <td>${num(row.crack).toFixed(2)}</td>
          <td><span class="history-status ${cls}">${statusText(code)}</span></td>
        </tr>
      `;
    }).join("");
  }

  setText("historyCount", `Showing ${rows.length ? start+1 : 0} to ${Math.min(start+visible.length,rows.length)} of ${rows.length} readings`);
  renderPagination(totalPages);
}

async function fetchHistory() {
  try{
    const response=await fetch("/api/history?limit=500",{cache:"no-store"});
    if(!response.ok) throw new Error(`HTTP ${response.status}`);
    state.history=await response.json();
    state.historyPage=1;
    renderHistory();
  }catch(error){
    console.error("History error:",error);
    const body=document.getElementById("historyTableBody");
    if(body) body.innerHTML=`<tr><td colspan="7" class="history-empty">Unable to load historical data.</td></tr>`;
  }
}

function showSection(section) {
  const dashboard = document.getElementById("dashboard");
  const history = document.getElementById("history");

  if (!dashboard || !history) return;

  const showHistory = section === "history";

  dashboard.classList.toggle("hidden", showHistory);
  history.classList.toggle("hidden", !showHistory);

  document.querySelectorAll(".nav-item").forEach(button => {
    button.classList.toggle(
      "active",
      button.dataset.section === section
    );
  });

  if (showHistory) {
    fetchHistory();
  } else {
    setTimeout(() => {
      if (state.map) state.map.invalidateSize();
    }, 150);
  }
}

function setupNavigation() {
  document.querySelectorAll(".nav-item").forEach(button => {
    button.type = "button";

    button.addEventListener("click", event => {
      event.preventDefault();
      event.stopPropagation();

      const section = button.dataset.section;
      if (section) {
        showSection(section);
      }
    });
  });

  // Fallback delegated handler so the History button still works
  // even if another dashboard element interferes with direct binding.
  document.addEventListener("click", event => {
    const button = event.target.closest(".nav-item");
    if (!button) return;

    const section = button.dataset.section;
    if (!section) return;

    event.preventDefault();
    showSection(section);
  });
}

function updateOfflineWatchdog() {
  if (!state.live) {
    setText("systemStatus", "System Offline");
    setText("sidebarConnection", "System Offline");

    document.getElementById("systemDot")?.classList.add("offline");
    document.getElementById("sidebarDot")?.classList.add("offline");

    return;
  }

  const ageSeconds =
    (Date.now() / 1000) - num(state.live.timestamp);

  if (ageSeconds > 5 || !state.live.systemActive) {
    setText("systemStatus", "System Offline");
    setText("sidebarConnection", "System Offline");

    document.getElementById("systemDot")?.classList.add("offline");
    document.getElementById("sidebarDot")?.classList.add("offline");

    updateNodeUI(1, { ...state.live.n1, active: false });
    updateNodeUI(2, { ...state.live.n2, active: false });
  }
}

function setupHistoryControls() {
  const node=document.getElementById("historyNodeFilter");
  const search=document.getElementById("historySearch");
  const from=document.getElementById("historyDateFrom");
  const to=document.getElementById("historyDateTo");
  const pageSize=document.getElementById("pageSize");
  const refresh=document.getElementById("refreshHistory");

  node.addEventListener("change",()=>{
    state.nodeFilter=node.value;
    state.historyPage=1;
    renderHistory();
  });

  search.addEventListener("input",()=>{
    state.search=search.value;
    state.historyPage=1;
    renderHistory();
  });

  [from,to].forEach(el=>el.addEventListener("change",()=>{
    state.historyPage=1;
    renderHistory();
  }));

  pageSize.addEventListener("change",()=>{
    state.pageSize=Number(pageSize.value);
    state.historyPage=1;
    renderHistory();
  });

  refresh.addEventListener("click",fetchHistory);
}

function initialize() {
  updateClock();
  setInterval(updateClock,1000);

  initMap();
  setupNavigation();
  setupHistoryControls();

  showSection("dashboard");

  fetchLiveData();
  fetchHistory();

  setInterval(fetchLiveData,2000);
  setInterval(fetchHistory,10000);
  setInterval(updateOfflineWatchdog,1000);

  window.addEventListener("resize",redraw);
}

document.addEventListener("DOMContentLoaded",initialize);
