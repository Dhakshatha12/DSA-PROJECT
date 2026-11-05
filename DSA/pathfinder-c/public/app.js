/* PathFinder-C front end.
 * All graph algorithms run in the C server; this file only draws the graph,
 * sends edits to /api/*, and replays the step trace that C returns.
 */
'use strict';

const $ = (sel) => document.querySelector(sel);
const SVGNS = 'http://www.w3.org/2000/svg';
const ICONS = { dijkstra: 'DJ', bfs: 'BFS', dfs: 'DFS', prim: 'PRM', kruskal: 'KRK', floyd: 'FW' };
const DS_TITLES = {
  dijkstra: 'Min-heap (priority queue)', prim: 'Min-heap (priority queue)',
  bfs: 'Queue (front → back)', dfs: 'Stack (top → bottom)',
  kruskal: 'Union-find sets', floyd: 'Distance matrix',
};
const DELAYS = [1700, 1250, 950, 720, 540, 410, 300, 210, 140, 80];
const MODE_HINTS = {
  select: 'Drag locations to move them. Click one to set source / target.',
  addNode: 'Click an empty spot on the map to add a location.',
  addEdge: 'Click two locations to connect them with a road.',
  delete: 'Click a location or a road to delete it.',
};

const state = {
  graph: { nodes: [], edges: [] },
  byId: new Map(),
  algos: [],
  algo: 'dijkstra',
  src: null,
  dst: null,
  pickNext: 'src',
  result: null,
  step: -1,
  timer: null,
  mode: 'select',
  pendingEdge: null,
  drag: null,
  addAt: null,
};

/* ------------------------------------------------------------------ API */

async function api(path, opts = {}) {
  let res;
  try {
    res = await fetch(path, opts);
  } catch (e) {
    setEngine(false);
    throw new Error('Cannot reach the C server. Is ./pathfinder running?');
  }
  const ct = res.headers.get('content-type') || '';
  const data = ct.includes('json') ? await res.json() : await res.text();
  if (!res.ok || (data && data.ok === false)) throw new Error((data && data.error) || res.statusText);
  setEngine(true);
  return data;
}

function post(path, params = {}, body) {
  const q = new URLSearchParams(params).toString();
  return api(path + (q ? '?' + q : ''), { method: 'POST', body });
}

function setEngine(ok) {
  const el = $('#engine');
  el.className = 'pill ' + (ok ? 'pill-ok' : 'pill-bad');
  el.textContent = ok ? `C engine online · ${location.host}` : 'C engine offline';
}

/* ---------------------------------------------------------------- toast */

let toastTimer = null;
function toast(msg, isErr = false) {
  const t = $('#toast');
  t.textContent = msg;
  t.className = 'toast' + (isErr ? ' err' : '');
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, isErr ? 4200 : 2400);
}

const name = (id) => (state.byId.get(id) || { name: '?' }).name;
const ekey = (u, v) => (u < v ? `${u}-${v}` : `${v}-${u}`);
const algoInfo = () => state.algos.find((a) => a.key === state.algo) || {};

/* ---------------------------------------------------------- graph state */

function setGraph(g) {
  state.graph = g;
  state.byId = new Map(g.nodes.map((n) => [n.id, n]));
  if (!state.byId.has(state.src)) state.src = null;
  if (!state.byId.has(state.dst)) state.dst = null;
  if (state.pendingEdge !== null && !state.byId.has(state.pendingEdge)) state.pendingEdge = null;
  $('#graphStats').textContent = `${g.nodeCount} locations · ${g.edgeCount} roads`;
  fillSelects();
  clearResult();
}

function fillSelects() {
  const opts = state.graph.nodes
    .slice()
    .sort((a, b) => a.name.localeCompare(b.name))
    .map((n) => `<option value="${n.id}">${escapeHtml(n.name)}</option>`)
    .join('');
  for (const [sel, key] of [['#srcSel', 'src'], ['#dstSel', 'dst']]) {
    const el = $(sel);
    el.innerHTML = `<option value="">— choose —</option>` + opts;
    el.value = state[key] === null ? '' : String(state[key]);
  }
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

/* ------------------------------------------------------- algorithm list */

function renderAlgoList() {
  const list = $('#algoList');
  list.innerHTML = '';
  for (const a of state.algos) {
    const b = document.createElement('button');
    b.className = 'algo';
    b.type = 'button';
    b.setAttribute('role', 'radio');
    b.setAttribute('aria-checked', String(a.key === state.algo));
    b.innerHTML = `<span class="ico">${ICONS[a.key] || '?'}</span><b>${escapeHtml(a.title)}</b><small>${escapeHtml(a.goal)}</small>`;
    b.addEventListener('click', () => selectAlgo(a.key));
    list.appendChild(b);
  }
}

function selectAlgo(key) {
  state.algo = key;
  for (const b of document.querySelectorAll('.algo')) {
    const a = state.algos[[...b.parentNode.children].indexOf(b)];
    b.setAttribute('aria-checked', String(a.key === key));
  }
  const info = algoInfo();
  $('#dstField').style.opacity = info.needsTarget ? '1' : '.45';
  $('#dstSel').disabled = !info.needsTarget;
  $('#srcSel').disabled = key === 'kruskal';
  $('#cxTime').textContent = info.time || '–';
  $('#cxSpace').textContent = info.space || '–';
  $('#cxDs').textContent = info.structures || '–';
  clearResult();
}

/* ------------------------------------------------------------- running */

async function run() {
  const info = algoInfo();
  if (state.algo !== 'kruskal' && state.src === null) return toast('Choose a source location first', true);
  if (info.needsTarget && state.dst === null) return toast('Choose a target location', true);
  const q = new URLSearchParams({ algo: state.algo, src: state.src ?? -1, dst: info.needsTarget ? state.dst : -1 });
  const btn = $('#runBtn');
  btn.disabled = true;
  try {
    const r = await api('/api/run?' + q);
    state.result = r;
    renderResult();
    setStep(0);
    play();
  } catch (e) {
    toast(e.message, true);
  } finally {
    btn.disabled = false;
  }
}

function clearResult() {
  pause();
  state.result = null;
  state.step = -1;
  $('#log').innerHTML = '';
  $('#stepCounter').textContent = 'No run yet';
  $('#progressBar').style.width = '0';
  $('#stepBanner').innerHTML = '<b>–</b>Press <em>Run in C</em> to execute the algorithm on the server and replay it here.';
  $('#matrixCard').hidden = true;
  $('#resTitle').textContent = 'Result';
  $('#resValue').textContent = '–';
  $('#resUnit').textContent = '';
  $('#resPath').innerHTML = '<span class="muted">Run an algorithm to see the route.</span>';
  for (const id of ['#stVisited', '#stEdges', '#stOps', '#stTime']) $(id).textContent = '–';
  $('#dsTitle').textContent = DS_TITLES[state.algo] || 'Data structure';
  $('#dsSize').textContent = '';
  $('#dsView').innerHTML = '<span class="muted">The heap, queue or stack appears here during playback.</span>';
  renderMap();
}

function renderResult() {
  const r = state.result;
  $('#resTitle').textContent = r.title;
  const chips = (ids) => ids.map((id) => `<span class="chip">${escapeHtml(name(id))}</span>`).join('<span class="arrow">→</span>');
  let value = '–', unit = '', path = '';
  if (r.algo === 'prim' || r.algo === 'kruskal') {
    value = r.mstWeight;
    unit = `km total · ${r.mst.length} roads`;
    path = r.mst.map(([u, v, w]) => `<span class="chip">${escapeHtml(name(u))} – ${escapeHtml(name(v))} · ${w}</span>`).join(' ');
  } else if (!r.reachable) {
    value = '∞';
    unit = 'not reachable';
    path = '<span class="muted">No route exists between these locations.</span>';
  } else if (r.algo === 'bfs' || r.algo === 'dfs') {
    value = r.distance;
    unit = `hop${r.distance === 1 ? '' : 's'} · ${r.pathWeight} km along this path`;
    path = chips(r.path) + (r.algo === 'dfs' ? '<div class="small muted">DFS finds a path, not necessarily the shortest.</div>' : '');
  } else {
    value = r.distance;
    unit = 'km shortest distance';
    path = chips(r.path);
  }
  $('#resValue').textContent = value;
  $('#resUnit').textContent = unit;
  $('#resPath').innerHTML = path;
  $('#stVisited').textContent = r.stats.visited;
  $('#stEdges').textContent = r.stats.edgesExamined;
  $('#stOps').textContent = r.stats.dsOps;
  $('#stTime').textContent = `${r.stats.micros.toFixed(1)} µs`;
  $('#cxTime').textContent = r.complexity.time;
  $('#cxSpace').textContent = r.complexity.space;
  $('#cxDs').textContent = r.complexity.structures;

  const log = $('#log');
  log.innerHTML = '';
  r.steps.forEach((s, i) => {
    const li = document.createElement('li');
    li.className = `t-${s.t}`;
    li.innerHTML = `<span class="n">${i + 1}</span><span>${escapeHtml(s.msg)}</span>`;
    li.addEventListener('click', () => { pause(); setStep(i); });
    log.appendChild(li);
  });
  if (r.stats.truncated) toast('Trace was long and has been shortened', false);
  $('#matrixCard').hidden = r.algo !== 'floyd';
}

/* ------------------------------------------------------------- playback */

function setStep(i) {
  const r = state.result;
  if (!r) return;
  state.step = Math.max(0, Math.min(i, r.steps.length - 1));
  const n = r.steps.length;
  $('#stepCounter').textContent = `Step ${state.step + 1} / ${n}`;
  $('#progressBar').style.width = `${((state.step + 1) / n) * 100}%`;
  const banner = $('#stepBanner');
  banner.innerHTML = `<b>${state.step + 1}/${n}</b>${escapeHtml(r.steps[state.step].msg)}`;
  const log = $('#log');
  [...log.children].forEach((li, k) => {
    li.classList.toggle('now', k === state.step);
    li.classList.toggle('future', k > state.step);
  });
  const cur = log.children[state.step];
  if (cur) log.scrollTop = cur.offsetTop - log.offsetTop - log.clientHeight / 2 + cur.clientHeight / 2;
  renderMap();
}

function play() {
  if (!state.result) return;
  if (state.step >= state.result.steps.length - 1) setStep(0);
  pause();
  $('#playBtn').textContent = '❚❚';
  $('#playBtn').setAttribute('aria-label', 'Pause');
  const tick = () => {
    if (!state.result || state.step >= state.result.steps.length - 1) return pause();
    setStep(state.step + 1);
    state.timer = setTimeout(tick, DELAYS[$('#speed').value - 1]);
  };
  state.timer = setTimeout(tick, DELAYS[$('#speed').value - 1]);
}

function pause() {
  clearTimeout(state.timer);
  state.timer = null;
  $('#playBtn').textContent = '▶';
  $('#playBtn').setAttribute('aria-label', 'Play');
}

const togglePlay = () => (state.timer ? pause() : play());

/* ---------------------------------------------- replay trace -> view state */

function computeView() {
  const view = { node: new Map(), edge: new Map(), label: new Map(), ds: null, dsTotal: 0, fw: null };
  const r = state.result;
  if (!r || state.step < 0) return view;
  const algo = r.algo;
  let current = null;
  const finish = () => { if (current !== null) view.node.set(current, 'visited'); current = null; };
  const setCurrent = (u) => { finish(); current = u; view.node.set(u, 'current'); };
  const frontier = (u) => { if (!view.node.has(u)) view.node.set(u, 'frontier'); };
  const fmt = (v) => (algo === 'bfs' ? `L${v}` : algo === 'dfs' ? `D${v}` : String(v));

  if (algo === 'dijkstra' || algo === 'prim') for (const n of state.graph.nodes) view.label.set(n.id, '∞');

  let fw = null;
  if (algo === 'floyd') {
    const ids = r.matrix.ids;
    const idx = new Map(ids.map((id, i) => [id, i]));
    fw = { ids, idx, d: ids.map((a) => ids.map((b) => (a === b ? 0 : null))), upd: new Set(), now: null, k: null };
    for (const e of state.graph.edges) {
      fw.d[idx.get(e.u)][idx.get(e.v)] = e.w;
      fw.d[idx.get(e.v)][idx.get(e.u)] = e.w;
    }
  }
  const dsu = new Map();
  const find = (x) => { while (dsu.has(x) && dsu.get(x) !== x) x = dsu.get(x); return x; };

  for (let i = 0; i <= state.step; i++) {
    const s = r.steps[i];
    const last = i === state.step;
    const k = ekey(s.u, s.v);
    switch (s.t) {
      case 'push':
        frontier(s.u);
        if (algo !== 'dfs') view.label.set(s.u, fmt(s.val));
        break;
      case 'visit':
        setCurrent(s.u);
        if (algo !== 'kruskal') view.label.set(s.u, fmt(s.val));
        break;
      case 'relax':
        if (view.node.get(s.v) !== 'visited') frontier(s.v);
        view.label.set(s.v, fmt(s.val));
        if (last) view.edge.set(k, 'relax');
        break;
      case 'skip':
        if (last) view.edge.set(k, 'check');
        break;
      case 'stale':
        break;
      case 'tree':
        view.edge.set(k, 'tree');
        if (algo === 'bfs') { frontier(s.v); view.label.set(s.v, fmt(s.val)); }
        else if (algo === 'dfs' || algo === 'prim') { setCurrent(s.v); if (algo === 'dfs') view.label.set(s.v, fmt(s.val)); }
        else if (algo === 'kruskal') {
          view.node.set(s.u, 'visited'); view.node.set(s.v, 'visited');
          dsu.set(s.u, find(s.u)); dsu.set(s.v, find(s.v)); dsu.set(find(s.v), find(s.u));
        }
        break;
      case 'reject':
        view.edge.set(k, 'reject');
        break;
      case 'consider':
        if (last) view.edge.set(k, 'consider');
        break;
      case 'phase':
        if (algo === 'floyd' && s.u >= 0) { setCurrent(s.u); fw.k = s.u; }
        break;
      case 'update':
        if (fw) {
          const a = fw.idx.get(s.u), b = fw.idx.get(s.v);
          fw.d[a][b] = fw.d[b][a] = s.val;
          fw.upd.add(`${a},${b}`); fw.upd.add(`${b},${a}`);
          if (last) { fw.now = [a, b]; frontier(s.u); frontier(s.v); }
        }
        break;
      case 'done':
        finish();
        break;
      default:
        break;
    }
    if (s.ds) { view.ds = s.ds; view.dsTotal = s.dsTotal; }
  }
  if (fw && state.src !== null && fw.idx.has(r.source)) {
    const row = fw.d[fw.idx.get(r.source)];
    fw.ids.forEach((id, j) => view.label.set(id, row[j] === null ? '∞' : String(row[j])));
  }
  if (state.step === r.steps.length - 1) {
    for (let i = 0; i + 1 < r.path.length; i++) view.edge.set(ekey(r.path[i], r.path[i + 1]), 'path');
    for (const id of r.path) view.node.set(id, 'path');
  }
  view.fw = fw;
  view.sets = algo === 'kruskal' ? groupSets(find) : null;
  return view;
}

function groupSets(find) {
  const groups = new Map();
  for (const n of state.graph.nodes) {
    const root = find(n.id);
    if (!groups.has(root)) groups.set(root, []);
    groups.get(root).push(n.id);
  }
  return [...groups.values()].sort((a, b) => b.length - a.length);
}

/* ------------------------------------------------------------- drawing */

function el(tag, attrs = {}, parent) {
  const e = document.createElementNS(SVGNS, tag);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  if (parent) parent.appendChild(e);
  return e;
}

function renderMap() {
  const view = computeView();
  const edgeLayer = $('#edgeLayer'), nodeLayer = $('#nodeLayer');
  edgeLayer.innerHTML = '';
  nodeLayer.innerHTML = '';

  for (const e of state.graph.edges) {
    const a = state.byId.get(e.u), b = state.byId.get(e.v);
    if (!a || !b) continue;
    const st = view.edge.get(ekey(e.u, e.v));
    const g = el('g', { class: 'edge' + (st ? ` e-${st}` : ''), 'data-u': e.u, 'data-v': e.v }, edgeLayer);
    el('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y }, g);
    el('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y, class: 'hit' }, g);
    const mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2, label = String(e.w), w = 12 + label.length * 7.5;
    el('rect', { x: mx - w / 2, y: my - 10, width: w, height: 20, rx: 10, class: 'wpill' }, g);
    el('text', { x: mx, y: my + 0.5 }, g).textContent = label;
    const title = el('title', {}, g);
    title.textContent = `${a.name} – ${b.name}: ${e.w} km`;
  }

  for (const n of state.graph.nodes) {
    const st = view.node.get(n.id);
    const cls = ['node'];
    if (st) cls.push(`s-${st}`);
    if (n.id === state.src) cls.push('is-src');
    if (n.id === state.dst && algoInfo().needsTarget) cls.push('is-dst');
    if (n.id === state.pendingEdge) cls.push('pending');
    const g = el('g', { class: cls.join(' '), 'data-id': n.id, transform: `translate(${n.x},${n.y})` }, nodeLayer);
    el('circle', { r: 24, class: 'ring' }, g);
    el('circle', { r: 13, class: 'core' }, g);
    const t = el('text', { y: 34, class: 'name' }, g);
    t.textContent = n.name;
    if (n.id === state.src || (n.id === state.dst && algoInfo().needsTarget)) {
      const isSrc = n.id === state.src;
      const tag = el('text', { y: -34, class: 'tag', fill: isSrc ? '#4338ca' : '#be123c' }, g);
      tag.textContent = isSrc ? 'START' : 'TARGET';
    }
    const lab = view.label.get(n.id);
    if (lab !== undefined) {
      const w = 14 + String(lab).length * 7.5;
      const badge = el('g', { class: 'badge', transform: `translate(${17},${-20})` }, g);
      el('rect', { x: 0, y: -9, width: w, height: 18, rx: 9 }, badge);
      el('text', { x: w / 2, y: 0.5 }, badge).textContent = lab;
    }
  }
  renderDs(view);
  renderMatrix(view);
}

function renderDs(view) {
  const r = state.result;
  const box = $('#dsView');
  $('#dsTitle').textContent = DS_TITLES[(r && r.algo) || state.algo];
  if (!r) return;
  if (r.algo === 'kruskal') {
    const sets = view.sets || [];
    const joined = sets.filter((s) => s.length > 1);
    const single = sets.length - joined.length;
    $('#dsSize').textContent = `${sets.length} set${sets.length === 1 ? '' : 's'}`;
    box.innerHTML = joined.map((s) => `<span class="ds-set">{ ${s.map((id) => escapeHtml(name(id))).join(', ')} }</span>`).join('') +
      (single ? `<span class="small muted">+ ${single} single-node set${single === 1 ? '' : 's'}</span>` : '');
    return;
  }
  if (r.algo === 'floyd') {
    const fw = view.fw;
    $('#dsSize').textContent = `${fw.ids.length} × ${fw.ids.length}`;
    box.innerHTML = fw.k !== null
      ? `<span class="small">Round through <b>${escapeHtml(name(fw.k))}</b> · ${fw.upd.size / 2} cells improved so far</span>`
      : '<span class="small">Matrix starts with direct road lengths.</span>';
    return;
  }
  if (!view.ds) { box.innerHTML = '<span class="muted">empty</span>'; $('#dsSize').textContent = ''; return; }
  $('#dsSize').textContent = `size ${view.dsTotal}`;
  if (view.ds.length === 0) { box.innerHTML = '<span class="muted">empty</span>'; return; }
  const label = r.algo === 'bfs' ? 'front' : r.algo === 'dfs' ? 'top' : 'next pop';
  const keyText = (key) => (r.algo === 'bfs' ? `level ${key}` : r.algo === 'dfs' ? (key >= 0 ? `from ${name(key)}` : 'start') : `key ${key}`);
  box.innerHTML = `<span class="ds-label">${label} →</span>` + view.ds.map(([node, key], i) =>
    `<span class="ds-item${i === 0 ? ' first' : ''}"><b>${escapeHtml(name(node))}</b><i>${escapeHtml(keyText(key))}</i></span>`).join('') +
    (view.dsTotal > view.ds.length ? `<span class="small muted">+${view.dsTotal - view.ds.length} more</span>` : '');
}

function renderMatrix(view) {
  const fw = view.fw;
  if (!fw) return;
  const short = (id) => escapeHtml(name(id).length > 8 ? name(id).slice(0, 7) + '…' : name(id));
  const kIdx = fw.k !== null ? fw.idx.get(fw.k) : -1;
  let html = '<tr><th></th>' + fw.ids.map((id, j) => `<th class="${j === kIdx ? 'k' : ''}" title="${escapeHtml(name(id))}">${short(id)}</th>`).join('') + '</tr>';
  fw.ids.forEach((id, i) => {
    html += `<tr><th class="${i === kIdx ? 'k' : ''}" title="${escapeHtml(name(id))}">${short(id)}</th>`;
    fw.ids.forEach((_, j) => {
      const v = fw.d[i][j];
      const cls = [];
      if (i === j) cls.push('diag');
      if (fw.upd.has(`${i},${j}`)) cls.push('upd');
      if (fw.now && ((fw.now[0] === i && fw.now[1] === j) || (fw.now[0] === j && fw.now[1] === i))) cls.push('now');
      if (i === kIdx || j === kIdx) cls.push('k');
      html += `<td class="${cls.join(' ')}">${v === null ? '∞' : v}</td>`;
    });
    html += '</tr>';
  });
  $('#matrix').innerHTML = html;
  $('#matrixNote').textContent = 'teal = improved · amber = updated this step · outlined = via node';
}

/* ------------------------------------------------------- map interaction */

function svgPoint(evt) {
  const svg = $('#map');
  const pt = svg.createSVGPoint();
  pt.x = evt.clientX;
  pt.y = evt.clientY;
  const p = pt.matrixTransform(svg.getScreenCTM().inverse());
  return { x: Math.round(Math.max(30, Math.min(970, p.x))), y: Math.round(Math.max(40, Math.min(600, p.y))) };
}

function setMode(mode) {
  state.mode = mode;
  state.pendingEdge = null;
  document.body.className = `mode-${mode}`;
  for (const b of document.querySelectorAll('.mode')) b.classList.toggle('active', b.dataset.mode === mode);
  $('#modeHint').textContent = MODE_HINTS[mode];
  renderMap();
}

function pickNode(id) {
  const needsTarget = algoInfo().needsTarget;
  if (!needsTarget || state.pickNext === 'src') {
    state.src = id;
    if (state.dst === id) state.dst = null;
    state.pickNext = needsTarget ? 'dst' : 'src';
  } else {
    if (id === state.src) return toast('Target must differ from the source', true);
    state.dst = id;
    state.pickNext = 'src';
  }
  fillSelects();
  clearResult();
  toast(state.pickNext === 'dst' ? `Start: ${name(id)} - now click the target` : `Route: ${name(state.src)} → ${name(state.dst ?? state.src)}`);
}

async function onNodeClick(id) {
  try {
    if (state.mode === 'select') return pickNode(id);
    if (state.mode === 'delete') {
      const nm = name(id);
      setGraph((await post('/api/node/remove', { id })).graph);
      return toast(`Deleted ${nm}`);
    }
    if (state.mode === 'addEdge') {
      if (state.pendingEdge === null) {
        state.pendingEdge = id;
        renderMap();
        return toast(`From ${name(id)} - click the second location`);
      }
      if (state.pendingEdge === id) { state.pendingEdge = null; return renderMap(); }
      const a = state.byId.get(state.pendingEdge), b = state.byId.get(id);
      const auto = Math.max(1, Math.round(Math.hypot(a.x - b.x, a.y - b.y) / 22));
      const w = parseInt($('#weightIn').value, 10) || auto;
      const g = (await post('/api/edge/add', { u: a.id, v: b.id, w })).graph;
      state.pendingEdge = null;
      setGraph(g);
      toast(`Road ${a.name} – ${b.name} (${w}) saved`);
    }
  } catch (e) {
    toast(e.message, true);
  }
}

async function onEdgeClick(u, v) {
  if (state.mode !== 'delete') return;
  try {
    setGraph((await post('/api/edge/remove', { u, v })).graph);
    toast(`Deleted road ${name(u)} – ${name(v)}`);
  } catch (e) {
    toast(e.message, true);
  }
}

function bindMap() {
  const svg = $('#map');
  svg.addEventListener('pointerdown', (evt) => {
    const nodeEl = evt.target.closest('.node');
    if (nodeEl && state.mode === 'select') {
      const id = Number(nodeEl.dataset.id);
      const n = state.byId.get(id);
      state.drag = { id, start: svgPoint(evt), orig: { x: n.x, y: n.y }, moved: false };
      svg.setPointerCapture(evt.pointerId);
    }
  });
  svg.addEventListener('pointermove', (evt) => {
    const d = state.drag;
    if (!d) return;
    const p = svgPoint(evt);
    if (!d.moved && Math.hypot(p.x - d.start.x, p.y - d.start.y) < 4) return;
    d.moved = true;
    const n = state.byId.get(d.id);
    n.x = Math.max(30, Math.min(970, d.orig.x + p.x - d.start.x));
    n.y = Math.max(40, Math.min(600, d.orig.y + p.y - d.start.y));
    renderMap();
  });
  svg.addEventListener('pointerup', async (evt) => {
    const d = state.drag;
    state.drag = null;
    if (d) {
      if (!d.moved) return onNodeClick(d.id);
      const n = state.byId.get(d.id);
      try { await post('/api/node/move', { id: d.id, x: Math.round(n.x), y: Math.round(n.y) }); }
      catch (e) { toast(e.message, true); }
      return;
    }
    const nodeEl = evt.target.closest('.node');
    if (nodeEl) return onNodeClick(Number(nodeEl.dataset.id));
    const edgeEl = evt.target.closest('.edge');
    if (edgeEl) return onEdgeClick(Number(edgeEl.dataset.u), Number(edgeEl.dataset.v));
    if (state.mode === 'addNode') {
      state.addAt = svgPoint(evt);
      $('#nodeName').value = '';
      $('#modal').hidden = false;
      $('#nodeName').focus();
    }
  });
}

/* --------------------------------------------------------------- wiring */

function bindControls() {
  $('#runBtn').addEventListener('click', run);
  $('#playBtn').addEventListener('click', togglePlay);
  $('#prevBtn').addEventListener('click', () => { pause(); setStep(state.step - 1); });
  $('#nextBtn').addEventListener('click', () => { pause(); setStep(state.step + 1); });
  $('#firstBtn').addEventListener('click', () => { pause(); setStep(0); });
  $('#lastBtn').addEventListener('click', () => { pause(); if (state.result) setStep(state.result.steps.length - 1); });
  $('#speed').addEventListener('input', () => { $('#speedLabel').textContent = `Speed ${$('#speed').value}`; });

  $('#srcSel').addEventListener('change', (e) => { state.src = e.target.value === '' ? null : Number(e.target.value); clearResult(); });
  $('#dstSel').addEventListener('change', (e) => { state.dst = e.target.value === '' ? null : Number(e.target.value); clearResult(); });
  $('#swapBtn').addEventListener('click', () => {
    [state.src, state.dst] = [state.dst, state.src];
    fillSelects();
    clearResult();
  });

  for (const b of document.querySelectorAll('.mode')) b.addEventListener('click', () => setMode(b.dataset.mode));

  $('#resetBtn').addEventListener('click', async () => {
    try { setGraph((await post('/api/reset')).graph); setDefaults(); toast('Demo map restored'); }
    catch (e) { toast(e.message, true); }
  });
  $('#clearBtn').addEventListener('click', async () => {
    if (!confirm('Remove every location and road?')) return;
    try { setGraph((await post('/api/clear')).graph); setMode('addNode'); toast('Graph cleared - click the map to add locations'); }
    catch (e) { toast(e.message, true); }
  });
  $('#importIn').addEventListener('change', async (e) => {
    const file = e.target.files[0];
    if (!file) return;
    try { setGraph((await post('/api/import', {}, await file.text())).graph); setDefaults(); toast(`Imported ${file.name}`); }
    catch (err) { toast(err.message, true); }
    e.target.value = '';
  });

  $('#modalCancel').addEventListener('click', () => { $('#modal').hidden = true; });
  $('#modalForm').addEventListener('submit', async (e) => {
    e.preventDefault();
    const nm = $('#nodeName').value.trim();
    if (!nm) return;
    $('#modal').hidden = true;
    try {
      setGraph((await post('/api/node/add', { name: nm, x: state.addAt.x, y: state.addAt.y })).graph);
      toast(`Added ${nm}`);
    } catch (err) { toast(err.message, true); }
  });

  document.addEventListener('keydown', (e) => {
    if (e.target.closest('input, select, textarea') || !$('#modal').hidden) {
      if (e.key === 'Escape') $('#modal').hidden = true;
      return;
    }
    if (e.key === ' ') { e.preventDefault(); togglePlay(); }
    else if (e.key === 'ArrowRight') { pause(); setStep(state.step + 1); }
    else if (e.key === 'ArrowLeft') { pause(); setStep(state.step - 1); }
    else if (e.key === 'Home') { pause(); setStep(0); }
    else if (e.key === 'End' && state.result) { pause(); setStep(state.result.steps.length - 1); }
    else if (e.key === 'Escape') { state.pendingEdge = null; renderMap(); }
  });
}

function setDefaults() {
  const byName = (n) => (state.graph.nodes.find((x) => x.name === n) || {}).id;
  const ids = state.graph.nodes.map((n) => n.id);
  state.src = byName('Yeshwanthpur') ?? ids[0] ?? null;
  state.dst = byName('Electronic City') ?? ids[ids.length - 1] ?? null;
  if (state.dst === state.src) state.dst = null;
  state.pickNext = 'src';
  fillSelects();
  renderMap();
}

function showOffline() {
  $('#offline').hidden = false;
  if (location.protocol === 'file:') {
    $('#offlineWhy').innerHTML = 'You opened <code>index.html</code> directly from the folder. ' +
      'This page only draws the map; every algorithm runs inside the C program, so start it first:';
    $('#retryBtn').textContent = 'Open http://localhost:8080';
  }
  $('#stepBanner').innerHTML = '<b>!</b>C engine offline: start <em>pathfinder</em>, then open http://localhost:8080';
}

async function init() {
  bindControls();
  bindMap();
  setMode('select');
  $('#retryBtn').addEventListener('click', () => {
    if (location.protocol === 'file:') location.href = 'http://localhost:8080';
    else location.reload();
  });
  if (location.protocol === 'file:') { setEngine(false); return showOffline(); }
  try {
    const [g, a] = await Promise.all([api('/api/graph'), api('/api/algorithms')]);
    state.algos = a.algorithms;
    renderAlgoList();
    setGraph(g.graph);
    setDefaults();
    selectAlgo('dijkstra');
    $('#offline').hidden = true;
  } catch (e) {
    showOffline();
  }
}

init();
