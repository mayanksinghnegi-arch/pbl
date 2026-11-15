// ============================================================
//  FoodBridge — frontend logic
//  Talks to the C++ backend through fetch() calls on /api/*
// ============================================================

// Works when served by the C++ server, and also if index.html is opened directly
const API = location.protocol.startsWith("http") ? "" : "http://localhost:8080";

const $ = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];

const state = {
  view: "overview",
  ngoId: localStorage.getItem("fb_ngo") || "",
  ngos: [],
  orderItem: null,
};

// ---------------- helpers ----------------
async function api(path, options = {}) {
  const opts = { headers: { "Content-Type": "application/json" }, ...options };
  if (opts.body && typeof opts.body !== "string") opts.body = JSON.stringify(opts.body);
  let res;
  try {
    res = await fetch(API + path, opts);
  } catch (e) {
    setStatus(false);
    throw new Error("Cannot reach the server. Is ./foodbridge running?");
  }
  setStatus(true);
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.error || `Request failed (${res.status})`);
  return data;
}

function esc(s) {
  return String(s ?? "").replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}
const inr = n => "₹" + Number(n || 0).toLocaleString("en-IN", { maximumFractionDigits: 2 });
const qty = (n, unit = "kg") => `${Number(n || 0).toLocaleString("en-IN", { maximumFractionDigits: 2 })} ${unit}`;
const titleCase = s => String(s).replace(/_/g, " ").toLowerCase().replace(/\b\w/g, c => c.toUpperCase());

function timeLeft(h) {
  if (h <= 0) return "Expired";
  if (h < 1) return `${Math.max(1, Math.round(h * 60))} min left`;
  const mins = Math.floor(h * 60);
  if (h < 48) return `${Math.floor(mins / 60)}h ${mins % 60}m left`;
  return `${Math.floor(mins / 1440)}d ${Math.floor((mins % 1440) / 60)}h left`;
}
const levelColor = l => ({ CRITICAL: "var(--critical)", HIGH: "var(--high)", MEDIUM: "var(--medium)", LOW: "var(--low)" }[l] || "var(--expired)");

function lifeBar(item) {
  const total = item.type === "RAW" ? item.shelfLifeDays * 24 : item.consumeWithinHours;
  const pct = Math.max(0, Math.min(100, (item.urgencyHours / total) * 100));
  return `<div class="life"><span class="txt" style="color:${levelColor(item.urgencyLevel)}">${timeLeft(item.urgencyHours)}</span>
    <div class="bar"><i style="width:${pct}%;background:${levelColor(item.urgencyLevel)}"></i></div></div>`;
}
const badge = (status, label) => `<span class="badge b-${status}">${esc(label || titleCase(status))}</span>`;
const typePill = t => `<span class="type-pill t-${t}">${t === "RAW" ? "RAW" : "PREPARED"}</span>`;

function toast(msg, kind = "") {
  const el = document.createElement("div");
  el.className = `toast ${kind}`;
  el.textContent = msg;
  $("#toasts").appendChild(el);
  setTimeout(() => el.remove(), 4200);
}
const fail = e => toast(e.message, "error");

function setStatus(ok) {
  const s = $("#serverStatus");
  s.classList.toggle("ok", ok);
  s.classList.toggle("down", !ok);
  $(".status-text", s).textContent = ok ? "Engine online" : "Server offline";
}

function kpi(label, value, sub = "", accent = false) {
  return `<div class="kpi${accent ? " accent" : ""}"><div class="k-label">${label}</div><div class="k-value">${value}</div>${sub ? `<div class="k-sub">${sub}</div>` : ""}</div>`;
}

// ---------------- charts (plain SVG, no library) ----------------
function shortDate(iso) {
  const d = new Date(iso + "T00:00:00");
  return { day: d.getDate(), wk: d.toLocaleDateString("en-IN", { weekday: "short" }).slice(0, 2) };
}

function barChart(el, cat, today) {
  const hist = cat.history.filter(r => r.date !== today).slice(-10);
  const bars = hist.map(r => ({ v: r.qty, kind: "actual", label: shortDate(r.date) }));
  bars.push({ v: cat.todaySoFar, kind: "today", label: { day: "Today", wk: "" } });
  bars.push({ v: cat.predictedNextDay, kind: "pred", label: { day: "Tmrw", wk: "" } });

  const W = 520, H = 160, pad = { l: 28, r: 6, t: 18, b: 30 };
  const max = Math.max(5, ...bars.map(b => b.v)) * 1.12;
  const step = (W - pad.l - pad.r) / bars.length;
  const bw = Math.min(30, step * 0.62);
  const y = v => pad.t + (H - pad.t - pad.b) * (1 - v / max);
  const color = { actual: "var(--chart-actual)", today: "var(--chart-today)", pred: "var(--chart-pred)" };

  let grid = "";
  for (let i = 0; i <= 3; i++) {
    const v = (max / 3) * i;
    grid += `<line x1="${pad.l}" x2="${W - pad.r}" y1="${y(v)}" y2="${y(v)}" stroke="var(--line)" />
             <text x="${pad.l - 5}" y="${y(v) + 3}" text-anchor="end">${Math.round(v)}</text>`;
  }
  const rects = bars.map((b, i) => {
    const x = pad.l + step * i + (step - bw) / 2;
    const h = Math.max(1, H - pad.b - y(b.v));
    const dash = b.kind === "pred" ? `stroke="var(--chart-pred)" stroke-dasharray="3 2" fill-opacity=".85"` : "";
    const val = b.kind !== "actual" ? `<text class="val" x="${x + bw / 2}" y="${y(b.v) - 4}" text-anchor="middle">${b.v}</text>` : "";
    return `<rect x="${x}" y="${y(b.v)}" width="${bw}" height="${h}" rx="4" fill="${color[b.kind]}" ${dash}><title>${b.label.day} ${b.label.wk}: ${b.v} kg</title></rect>${val}
      <text x="${x + bw / 2}" y="${H - pad.b + 13}" text-anchor="middle">${b.label.day}</text>
      <text x="${x + bw / 2}" y="${H - pad.b + 24}" text-anchor="middle">${b.label.wk}</text>`;
  }).join("");
  el.innerHTML = `<svg viewBox="0 0 ${W} ${H}" role="img" aria-label="Leftover history and forecast">${grid}${rects}</svg>`;
}

function lineChart(el, rows) {
  if (!rows.length) { el.innerHTML = `<p class="empty">Not enough history for this window yet.</p>`; return; }
  const W = 520, H = 160, pad = { l: 28, r: 22, t: 12, b: 26 };
  const max = Math.max(5, ...rows.flatMap(r => [r.actual, r.predicted])) * 1.12;
  const x = i => pad.l + (rows.length === 1 ? 0 : (W - pad.l - pad.r) * i / (rows.length - 1));
  const y = v => pad.t + (H - pad.t - pad.b) * (1 - v / max);
  const path = key => rows.map((r, i) => `${i ? "L" : "M"}${x(i).toFixed(1)},${y(r[key]).toFixed(1)}`).join(" ");
  let grid = "";
  for (let i = 0; i <= 3; i++) {
    const v = (max / 3) * i;
    grid += `<line x1="${pad.l}" x2="${W - pad.r}" y1="${y(v)}" y2="${y(v)}" stroke="var(--line)"/><text x="${pad.l - 5}" y="${y(v) + 3}" text-anchor="end">${Math.round(v)}</text>`;
  }
  const labels = rows.map((r, i) => (i % Math.ceil(rows.length / 7) === 0 || i === rows.length - 1)
    ? `<text x="${x(i)}" y="${H - 8}" text-anchor="middle">${shortDate(r.date).day} ${shortDate(r.date).wk}</text>` : "").join("");
  const dots = rows.map((r, i) => `<circle cx="${x(i)}" cy="${y(r.actual)}" r="3.5" fill="var(--chart-actual)"><title>${r.date}: actual ${r.actual}, predicted ${r.predicted}</title></circle>`).join("");
  el.innerHTML = `<svg viewBox="0 0 ${W} ${H}" role="img" aria-label="Predicted vs actual">${grid}
    <path d="${path("actual")}" fill="none" stroke="var(--chart-actual)" stroke-width="2"/>
    <path d="${path("predicted")}" fill="none" stroke="var(--chart-pred)" stroke-width="2.5" stroke-dasharray="5 3"/>
    ${dots}${labels}</svg>`;
}

// ---------------- OVERVIEW ----------------
async function loadOverview() {
  const [stats, urgent, pred] = await Promise.all([api("/api/stats"), api("/api/foods/urgent?k=3"), api("/api/prediction")]);
  $("#ovKpis").innerHTML =
    kpi("Surplus available now", qty(stats.availableQty), `${stats.availableItems} items · ${inr(stats.availableValue)}`, true) +
    kpi("Needs claiming soon", stats.urgentItems, "within 24 hours") +
    kpi("Food saved so far", qty(stats.foodSavedKg), `≈ ${stats.mealsEquivalent.toLocaleString("en-IN")} meals (est. 400 g each)`) +
    kpi("Requests in queue", stats.pendingOrders, `${stats.ngoCount} registered NGOs`);

  $("#ovUrgent").innerHTML = urgent.items.length ? urgent.items.map(f => `
    <div class="urgent-item">
      <div><h4>${esc(f.name)} ${typePill(f.type)}</h4>
        <div class="meta-line">${qty(f.quantity, f.unit)} · ${inr(f.price)}/${esc(f.unit)}</div></div>
      ${badge(f.urgencyLevel)}
      <div style="grid-column:1/-1">${lifeBar(f)}</div>
    </div>`).join("") : `<p class="empty">Nothing waiting — the inventory is empty.</p>`;

  $("#ovPredRaw").innerHTML = `${pred.RAW.predictedNextDay} <small>kg</small>`;
  $("#ovPredPrep").innerHTML = `${pred.PREPARED.predictedNextDay} <small>kg</small>`;
  $("#ovPredNote").textContent = `Back-tested accuracy: raw ${pred.RAW.accuracy}%, prepared ${pred.PREPARED.accuracy}%. Cooking about this much less tomorrow avoids the waste altogether.`;
}

// ---------------- CANTEEN ----------------
async function loadCanteen() {
  const [canteen, foods, queue, tx, pred, nearest] = await Promise.all([
    api("/api/canteen"), api("/api/foods?sort=urgency"), api("/api/queue"),
    api("/api/transactions"), api("/api/prediction"), api("/api/nearest"),
  ]);
  $("#canteenName").textContent = `${canteen.name} · ${canteen.location}`;

  // inventory
  $("#invMeta").textContent = `Linked List · ${foods.items.length} nodes · ${foods.meta.algorithm}`;
  $("#invTable tbody").innerHTML = foods.items.length ? foods.items.map(f => `
    <tr class="${f.urgencyLevel === "EXPIRED" ? "expired" : ""}">
      <td class="id">${f.id}</td>
      <td><span class="item-name">${esc(f.name)}</span> ${typePill(f.type)}
        <span class="sub">${f.type === "RAW" ? esc(f.storageType) + " · added " + esc(f.dateAdded) : "cooked " + esc(f.cookedTime)}</span></td>
      <td class="num">${qty(f.quantity, f.unit)}</td>
      <td class="num">${inr(f.price)}</td>
      <td>${lifeBar(f)}</td>
      <td><button class="btn tiny danger" data-remove="${f.id}" title="Remove from inventory">Remove</button></td>
    </tr>`).join("") : `<tr><td colspan="6" class="empty">No surplus recorded yet. Add the first item on the left.</td></tr>`;

  // queue
  $("#queueList").innerHTML = queue.items.length ? queue.items.map(o => `
    <li><span class="pos">${o.position}</span>
      <div><div class="who">${esc(o.ngoName)}</div><div class="what">${o.orderId} · ${esc(o.foodName)} · ${qty(o.quantity, o.unit)} · ${o.timestamp}</div></div>
      <span class="num">${inr(o.amount)}</span></li>`).join("")
    : `<li class="empty" style="display:block;border:0">No requests waiting.</li>`;
  $("#processQueueBtn").disabled = !queue.items.length;

  // awaiting pickup
  const confirmed = tx.items.filter(o => o.status === "CONFIRMED");
  $("#pickupList").innerHTML = confirmed.length ? confirmed.map(o => `
    <div class="list-row"><div><div class="main">${esc(o.foodName)} · ${qty(o.quantity, o.unit)}</div>
      <div class="sub">${o.orderId} · ${esc(o.ngoName)} · ${inr(o.amount)}</div></div>
      <button class="btn tiny primary" data-advance="${o.orderId}" data-to="PICKED_UP">Mark picked up</button></div>`).join("")
    : `<p class="empty">No confirmed orders waiting for pick-up.</p>`;

  // forecast
  barChart($("#chartRaw"), pred.RAW, pred.today);
  barChart($("#chartPrep"), pred.PREPARED, pred.today);
  $("#fcRawLabel").textContent = `Tomorrow ≈ ${pred.RAW.predictedNextDay} kg`;
  $("#fcPrepLabel").textContent = `Tomorrow ≈ ${pred.PREPARED.predictedNextDay} kg`;

  // nearest NGOs
  $("#nearestList").innerHTML = nearest.items.map((n, i) => `
    <div class="list-row"><div style="display:flex;gap:10px;align-items:center">
      <span class="rank ${i === 0 && n.distanceKm != null ? "first" : ""}">${i + 1}</span>
      <div><div class="main">${esc(n.name)}</div>
      <div class="sub">${n.path.length ? n.path.map(p => p === "CANTEEN" ? "Canteen" : esc(p)).join(" → ") : "Not linked to the road map"}</div></div></div>
      <span class="dist">${n.distanceKm != null ? n.distanceKm + " km" : "—"}</span></div>`).join("");
}

function setupFoodForm() {
  const form = $("#foodForm");
  const sync = () => {
    const raw = form.type.value === "RAW";
    $(".raw-only", form).hidden = !raw;
    $(".prep-only", form).hidden = raw;
    $("#typeHint").textContent = raw ? "Unused ingredient stock — vegetables, grains, dairy." : "Cooked surplus left after serving hours.";
    $("#formula").textContent = raw ? "urgency = shelf life − time since added" : "urgency = safe window − hours since cooked";
    form.name.placeholder = raw ? "e.g. Tomatoes" : "e.g. Veg Biryani";
  };
  $$("input[name=type]", form).forEach(r => r.addEventListener("change", sync));
  sync();

  form.addEventListener("submit", async e => {
    e.preventDefault();
    const f = new FormData(form);
    const payload = Object.fromEntries(f.entries());
    ["quantity", "price", "shelfLifeDays", "cookedHoursAgo", "consumeWithinHours"].forEach(k => {
      if (payload[k] !== undefined && payload[k] !== "") payload[k] = Number(payload[k]);
    });
    try {
      const res = await api("/api/foods", { method: "POST", body: payload });
      toast(`${res.item.name} added as ${res.item.id} (${timeLeft(res.item.urgencyHours)})`, "success");
      form.name.value = ""; form.quantity.value = ""; form.price.value = "";
      loadCanteen();
    } catch (err) { fail(err); }
  });
}

async function processQueue() {
  try {
    const res = await api("/api/queue/process", { method: "POST" });
    $("#processResults").innerHTML = res.processed.map(o => o.status === "CONFIRMED"
      ? `<div class="result ok"><b>${o.orderId}</b> confirmed — ${qty(o.quantity, o.unit)} ${esc(o.foodName)} to ${esc(o.ngoName)} (${inr(o.amount)})</div>`
      : `<div class="result no"><b>${o.orderId}</b> rejected — ${esc(o.reason)}</div>`).join("");
    const ok = res.processed.filter(o => o.status === "CONFIRMED").length;
    toast(`Processed ${res.processed.length} request(s): ${ok} confirmed, ${res.processed.length - ok} rejected`, "success");
    loadCanteen();
  } catch (e) { fail(e); }
}

// ---------------- NGO ----------------
async function loadNGO() {
  const ngos = await api("/api/ngos");
  state.ngos = ngos.items;
  if (!state.ngos.find(n => n.ngoId === state.ngoId)) state.ngoId = state.ngos[0]?.ngoId || "";
  $("#ngoSelect").innerHTML = state.ngos.map(n => `<option value="${n.ngoId}" ${n.ngoId === state.ngoId ? "selected" : ""}>${esc(n.name)} (${n.ngoId})</option>`).join("");
  await Promise.all([loadNgoProfile(), loadFoodsForNgo()]);
}

async function loadNgoProfile() {
  if (!state.ngoId) { $("#ngoProfile").innerHTML = `<p class="empty">Register an NGO to start.</p>`; return; }
  const data = await api(`/api/ngos/${state.ngoId}`);
  const n = data.ngo;
  state.currentNgo = n;
  const pct = Math.min(100, (n.usedToday / n.capacityPerDay) * 100);
  $("#ngoProfile").innerHTML = `
    <div><div class="name">${esc(n.name)}</div><div class="muted small">${esc(n.address)} · ${n.contactNumber}</div></div>
    <div class="stat"><div class="k">Daily capacity</div><div class="v">${qty(n.capacityPerDay)}</div></div>
    <div class="stat"><div class="k">Collected today</div><div class="v">${qty(n.usedToday)}</div><div class="cap-bar"><i style="width:${pct}%"></i></div></div>
    <div class="stat"><div class="k">Record lookup</div><div class="v" style="font-size:14px">Hash bucket #${n.bucket}</div><div class="muted small">O(1) by NGO ID</div></div>`;

  const rows = [...data.pending.map(o => ({ ...o, inQueue: true })), ...data.history];
  $("#myOrders tbody").innerHTML = rows.length ? rows.map(o => `
    <tr><td class="id">${o.orderId}</td>
      <td><span class="item-name">${esc(o.foodName)}</span>${o.reason ? `<span class="reason">${esc(o.reason)}</span>` : ""}</td>
      <td class="num">${qty(o.quantity, o.unit)}</td><td class="num">${inr(o.amount)}</td>
      <td class="small">${o.timestamp}</td>
      <td>${o.inQueue ? badge("PENDING", "In queue") : badge(o.status)}</td>
      <td>${o.status === "PICKED_UP" ? `<button class="btn tiny primary" data-advance="${o.orderId}" data-to="DISTRIBUTED">Mark distributed</button>`
        : o.status === "CONFIRMED" ? `<span class="muted small">Collect from canteen</span>` : ""}</td></tr>`).join("")
    : `<tr><td colspan="7" class="empty">No requests yet. Pick something from the list above.</td></tr>`;
}

async function loadFoodsForNgo() {
  const type = $("input[name=tf]:checked").value;
  const sort = $("#sortSelect").value;
  const maxPrice = $("#maxPrice").value;
  const qs = new URLSearchParams({ sort, includeExpired: "0" });
  if (type) qs.set("type", type);
  if (maxPrice !== "") qs.set("maxPrice", maxPrice);
  const data = await api(`/api/foods?${qs}`);
  const m = data.meta;
  $("#foodsMeta").textContent = `${data.items.length} of ${m.total} items · ${m.algorithm}, ${m.comparisons} comparisons` +
    (m.binarySearch ? ` · binary search for price ≤ ₹${m.binarySearch.maxPrice}: ${m.binarySearch.steps} steps` : "");

  $("#foodGrid").innerHTML = data.items.length ? data.items.map(f => `
    <article class="food-card">
      <div class="top">${typePill(f.type)}${badge(f.urgencyLevel)}</div>
      <h4>${esc(f.name)}</h4>
      <div class="facts">
        <div><span class="k">Available</span><b>${qty(f.quantity, f.unit)}</b></div>
        <div style="text-align:right"><span class="k">Price</span><b>${inr(f.price)}</b><span class="k">per ${esc(f.unit)}</span></div>
      </div>
      ${lifeBar(f)}
      <div class="detail">${f.type === "RAW" ? `${esc(f.storageType)} storage` : `Cooked ${esc(f.cookedTime)}`} · ${f.id}</div>
      <button class="btn primary" data-order="${f.id}" ${state.ngoId ? "" : "disabled"}>Request</button>
    </article>`).join("") : `<p class="empty" style="grid-column:1/-1">No food matches these filters right now.</p>`;
  state.foods = data.items;
}

function openOrder(id) {
  const f = state.foods.find(x => x.id === id);
  const n = state.currentNgo;
  if (!f || !n) return;
  state.orderItem = f;
  const left = Math.max(0, n.capacityPerDay - n.usedToday);
  const start = Math.max(0.1, Math.min(f.quantity, left || f.quantity));
  $("#odTitle").textContent = `Request ${f.name}`;
  $("#odSub").textContent = `${qty(f.quantity, f.unit)} available at ${inr(f.price)}/${f.unit} · ${timeLeft(f.urgencyHours)}`;
  $("#odUnit").textContent = f.unit;
  for (const el of [$("#odQty"), $("#odRange")]) { el.max = f.quantity; el.value = +start.toFixed(1); }
  $("#odCap").textContent = `${n.name} has ${qty(left)} of daily capacity left today. Requests above it are rejected by the matching engine.`;
  updateAmount();
  $("#orderDialog").showModal();
}
function updateAmount() {
  const q = Number($("#odQty").value || 0);
  $("#odAmount").textContent = inr(q * (state.orderItem?.price || 0));
}

async function submitOrder() {
  const quantity = Number($("#odQty").value);
  try {
    const res = await api("/api/order", { method: "POST", body: { ngoId: state.ngoId, foodItemId: state.orderItem.id, quantity } });
    toast(`Request ${res.order.orderId} queued at position ${res.queuePosition}. The canteen confirms it when it processes the queue.`, "success");
    loadNGO();
  } catch (e) { fail(e); }
}

function setupNgo() {
  $("#ngoSelect").addEventListener("change", e => {
    state.ngoId = e.target.value;
    localStorage.setItem("fb_ngo", state.ngoId);
    loadNgoProfile().catch(fail);
  });
  $$("input[name=tf]").forEach(r => r.addEventListener("change", () => loadFoodsForNgo().catch(fail)));
  $("#sortSelect").addEventListener("change", () => loadFoodsForNgo().catch(fail));
  let t;
  $("#maxPrice").addEventListener("input", () => { clearTimeout(t); t = setTimeout(() => loadFoodsForNgo().catch(fail), 300); });

  $("#odQty").addEventListener("input", () => {
    const max = Number($("#odQty").max);
    if (Number($("#odQty").value) > max) $("#odQty").value = max;   // cannot ask for more than is available
    $("#odRange").value = $("#odQty").value;
    updateAmount();
  });
  $("#odRange").addEventListener("input", () => { $("#odQty").value = $("#odRange").value; updateAmount(); });
  $("#orderDialog").addEventListener("close", () => { if ($("#orderDialog").returnValue === "ok") submitOrder(); });

  $("#openRegister").addEventListener("click", async () => {
    try {
      const lm = await api("/api/landmarks");
      $("#landmarkSelect").innerHTML = `<option value="">— not mapped —</option>` + lm.items.map(l => `<option>${esc(l)}</option>`).join("");
    } catch (e) { fail(e); }
    $("#ngoForm").reset();
    $("#ngoDialog").showModal();
  });
  $("#ngoDialog").addEventListener("close", async () => {
    if ($("#ngoDialog").returnValue !== "ok") return;
    const payload = Object.fromEntries(new FormData($("#ngoForm")).entries());
    payload.capacityPerDay = Number(payload.capacityPerDay);
    payload.distanceKm = payload.distanceKm === "" ? 0 : Number(payload.distanceKm);
    try {
      const res = await api("/api/ngos", { method: "POST", body: payload });
      state.ngoId = res.ngo.ngoId;
      localStorage.setItem("fb_ngo", state.ngoId);
      toast(`${res.ngo.name} registered as ${res.ngo.ngoId} (hash bucket #${res.bucket})`, "success");
      loadNGO();
    } catch (e) { fail(e); $("#ngoDialog").showModal(); }
  });
}

// ---------------- ADMIN ----------------
async function loadAdmin() {
  const N = $("#windowSelect").value;
  const [stats, tx, pred, log] = await Promise.all([api("/api/stats"), api("/api/transactions"), api(`/api/prediction?window=${N}`), api("/api/log")]);
  $("#adKpis").innerHTML =
    kpi("Transactions", stats.transactions, `${stats.pendingOrders} still in queue`) +
    kpi("Confirmed hand-overs", stats.confirmed, `${stats.rejected} rejected`) +
    kpi("Distributed to people", stats.distributed, "final status recorded") +
    kpi("Food saved", qty(stats.foodSavedKg), `≈ ${stats.mealsEquivalent.toLocaleString("en-IN")} meals`, true) +
    kpi("Recovered from NGOs", inr(stats.revenue), "instead of ₹0 in the bin") +
    kpi("Forecast accuracy", `${stats.predictionAccuracy}%`, "7-day moving average");

  state.tx = tx.items;
  $("#stackMeta").textContent = `Stack · depth ${tx.stackDepth} · newest first`;
  renderTx();

  lineChart($("#btRaw"), pred.RAW.backtest);
  lineChart($("#btPrep"), pred.PREPARED.backtest);
  $("#accRaw").textContent = `${pred.RAW.accuracy}% accurate`;
  $("#accPrep").textContent = `${pred.PREPARED.accuracy}% accurate`;

  const s = stats.structures;
  const ds = [
    [s.linkedList, "Linked List", "food items in inventory"],
    [s.minHeap, "Min-Heap", "items ranked by urgency"],
    [s.queue, "Queue", "requests waiting (FIFO)"],
    [s.stack, "Stack", "logged transactions"],
    [s.hashTable, "Hash Table", `NGOs in ${s.hashBuckets} buckets`],
    [s.historyDays, "Dynamic Array", "daily waste records"],
    [s.graphNodes, "Graph", "nodes on the road map"],
  ];
  $("#dsGrid").innerHTML = ds.map(([n, l, u]) => `<div class="ds"><div class="n">${n}</div><div class="l">${l}</div><div class="u">${u}</div></div>`).join("");
  renderLog(log.items);
}

function renderTx() {
  const f = $("input[name=sf]:checked").value;
  const rows = (state.tx || []).filter(o => !f || o.status === f);
  $("#txTable tbody").innerHTML = rows.length ? rows.map(o => `
    <tr><td class="id">${o.orderId}</td><td>${esc(o.ngoName)}<span class="sub">${o.ngoId}</span></td>
      <td><span class="item-name">${esc(o.foodName)}</span> ${typePill(o.foodType)}${o.reason ? `<span class="reason">${esc(o.reason)}</span>` : ""}</td>
      <td class="num">${qty(o.quantity, o.unit)}</td><td class="num">${inr(o.amount)}</td>
      <td class="small">${o.timestamp}</td><td>${badge(o.status)}</td>
      <td>${o.status === "CONFIRMED" ? `<button class="btn tiny ghost" data-advance="${o.orderId}" data-to="PICKED_UP">Picked up</button>`
        : o.status === "PICKED_UP" ? `<button class="btn tiny ghost" data-advance="${o.orderId}" data-to="DISTRIBUTED">Distributed</button>` : ""}</td></tr>`).join("")
    : `<tr><td colspan="8" class="empty">No transactions with this status.</td></tr>`;
}

function renderLog(items) {
  $("#engineLog").innerHTML = items.length
    ? items.map(e => `<li><span class="t">${e.time}</span><span class="s">${esc(e.structure)}</span><span class="m">${esc(e.message)}</span></li>`).join("")
    : `<li><span class="m" style="grid-column:1/-1;color:var(--muted)">No activity since the server started. Add food or place a request to see the engine work.</span></li>`;
}

// ---------------- shared actions (event delegation) ----------------
document.addEventListener("click", async e => {
  const nav = e.target.closest("[data-nav]");
  if (nav) { e.preventDefault(); location.hash = nav.dataset.nav; return; }

  const rm = e.target.closest("[data-remove]");
  if (rm) {
    if (!confirm(`Remove ${rm.dataset.remove} from the inventory?`)) return;
    try { await api(`/api/foods/${rm.dataset.remove}`, { method: "DELETE" }); toast(`${rm.dataset.remove} removed`); refresh(); }
    catch (err) { fail(err); }
    return;
  }

  const adv = e.target.closest("[data-advance]");
  if (adv) {
    try {
      const o = await api(`/api/transactions/${adv.dataset.advance}/status`, { method: "POST", body: { status: adv.dataset.to } });
      toast(`${o.orderId} marked ${titleCase(o.status).toLowerCase()}`, "success");
      refresh();
    } catch (err) { fail(err); }
    return;
  }

  const ord = e.target.closest("[data-order]");
  if (ord) openOrder(ord.dataset.order);
});

// ---------------- routing ----------------
const loaders = { overview: loadOverview, canteen: loadCanteen, ngo: loadNGO, admin: loadAdmin };

function show(view) {
  if (!loaders[view]) view = "overview";
  state.view = view;
  $$(".view").forEach(v => v.classList.toggle("active", v.id === `view-${view}`));
  $$(".tab").forEach(t => t.classList.toggle("active", t.dataset.nav === view));
  refresh();
  window.scrollTo({ top: 0 });
}
function refresh() { loaders[state.view]().catch(fail); }

window.addEventListener("hashchange", () => show(location.hash.slice(1)));
document.addEventListener("DOMContentLoaded", () => {
  setupFoodForm();
  setupNgo();
  $("#processQueueBtn").addEventListener("click", processQueue);
  $$("input[name=sf]").forEach(r => r.addEventListener("change", renderTx));
  $("#windowSelect").addEventListener("change", refresh);
  $("#refreshLog").addEventListener("click", async () => { try { renderLog((await api("/api/log")).items); } catch (e) { fail(e); } });
  show(location.hash.slice(1) || "overview");
  // urgency changes with time, so refresh the open page every 30 s (skip while a dialog is open)
  setInterval(() => { if (!document.querySelector("dialog[open]")) refresh(); }, 30000);
});
