import * as THREE from "./vendor/three/build/three.module.js";
import { OrbitControls } from "./vendor/three/examples/jsm/controls/OrbitControls.js";
import { Line2 } from "./vendor/three/examples/jsm/lines/Line2.js";
import { LineGeometry } from "./vendor/three/examples/jsm/lines/LineGeometry.js";
import { LineMaterial } from "./vendor/three/examples/jsm/lines/LineMaterial.js";
import { LineSegments2 } from "./vendor/three/examples/jsm/lines/LineSegments2.js";
import { LineSegmentsGeometry } from "./vendor/three/examples/jsm/lines/LineSegmentsGeometry.js";

const THREE_LOADED = !!THREE;
const ORBIT_LOADED = !!OrbitControls;
const WIDE_LINES_LOADED = !!Line2 && !!LineGeometry && !!LineMaterial && !!LineSegments2 && !!LineSegmentsGeometry;

const EARTH_R = 6371000, D2R = Math.PI/180, R2D = 180/Math.PI;
let map = null, bridge = null;
let pathLine = null, trailLine = null, launchMarker = null, rocketMarker = null;
let allPos = [], allAlt = [], allTs = [], allIdx = [], trailLen = 0, following = false;
let allPos3d = [], allAlt3d = [], allIdx3d = [], totalRawSamples = 0, currentExactPoint = null;
let cumulativeLiveTotal = "", cumulativeLiveValid = "", cumulativeLivePathLength = NaN;
let activeView = "2d";
let activeLayer = "terrain";
let tileLayerMap = null, tileLayerLight = null, tileLayerTerrain = null, tileLayerSat = null;
let currentThemeDark = true;
let hostVisible = true;
let imperialUnits = false;
let pathDataDirty = true, path3dDataDirty = true;

// ── 3D state ───────────────────────────────────────────────────────
let scene3d, camera3d, renderer3d, controls3d, animId3d = null;
let renderFramesRemaining3d = 0;
let pathLine3d, trailLine3d, ghostLine3d;
let launchSphere3d, rocketSphere3d, rocketGlow3d;
let groundGrid3d, groundMesh3d;
let currentRocketWorld3d = null;
let currentThemePalette = null;
let needsUpdate3d = true;
const ALT_EXAGGERATION = 3.0;
let ground3dMode = "box";
let groundTexCanvas = null, groundTexCtx = null, groundTexture = null;
let groundTexBounds = null;
let groundElevGrid = null;
let groundRefAlt = 0;
let groundLoadGeneration = 0;

const MAX_ABS_ALTITUDE = 10000000;
const MAX_ABS_TIMESTAMP = 1e15;
const MAX_MAP_DISTANCE = 1e12;
const MAX_MAP_SAMPLE_COUNT = 10000000;
const MAX_2D_POINTS = 10000;
const MAX_3D_POINTS = 5000;

function hDist(a1,o1,a2,o2) {
  const dl=(a2-a1)*D2R*0.5, doo=(o2-o1)*D2R*0.5;
  const a=Math.sin(dl)*Math.sin(dl)+Math.cos(a1*D2R)*Math.cos(a2*D2R)*Math.sin(doo)*Math.sin(doo);
  return 2*EARTH_R*Math.asin(Math.sqrt(a));
}
function fBearing(a1,o1,a2,o2) {
  const la1=a1*D2R,la2=a2*D2R,dL=(o2-o1)*D2R;
  let b=Math.atan2(Math.sin(dL)*Math.cos(la2),Math.cos(la1)*Math.sin(la2)-Math.sin(la1)*Math.cos(la2)*Math.cos(dL))*R2D;
  return b<0?b+360:b;
}
function validC(lat,lon){
  return Number.isFinite(lat) && Number.isFinite(lon)
    && Math.abs(lat) <= 90 && Math.abs(lon) <= 180
    && !(Math.abs(lat)<1e-9&&Math.abs(lon)<1e-9);
}
function validAltitude(alt){return Number.isFinite(alt)&&Math.abs(alt)<=MAX_ABS_ALTITUDE;}
function fmtAlt(m){
  if(imperialUnits)return (m*3.280839895013123).toFixed(0)+" ft";
  return Math.abs(m)>=1000?(m/1000).toFixed(1)+" km":m.toFixed(0)+" m";
}
function fmtDist(m){
  if(imperialUnits){
    const feet=m*3.280839895013123;
    return Math.abs(feet)>=5280?(feet/5280).toFixed(2)+" mi":feet.toFixed(0)+" ft";
  }
  return Math.abs(m)>=1000?(m/1000).toFixed(2)+" km":m.toFixed(0)+" m";
}
function sampleIdx(indices, i){return Number.isFinite(indices[i])?indices[i]:i;}

function parseHexColor(color) {
  const match = /^#([0-9a-f]{6})$/i.exec(String(color || ""));
  if (!match) return null;
  const value = Number.parseInt(match[1], 16);
  return {r:(value>>16)&255,g:(value>>8)&255,b:value&255};
}

function colorWithAlpha(color, alpha, fallback) {
  const rgb = parseHexColor(color);
  return rgb ? `rgba(${rgb.r},${rgb.g},${rgb.b},${alpha})` : fallback;
}

function relativeLuminance(color) {
  const rgb = parseHexColor(color);
  if (!rgb) return null;
  const linear = value => {
    const channel = value / 255;
    return channel <= 0.04045 ? channel / 12.92 : ((channel + 0.055) / 1.055) ** 2.4;
  };
  return 0.2126*linear(rgb.r)+0.7152*linear(rgb.g)+0.0722*linear(rgb.b);
}

function higherContrastText(background, first, second) {
  const bg = relativeLuminance(background);
  const a = relativeLuminance(first);
  const b = relativeLuminance(second);
  if (bg === null || a === null || b === null) return first || second;
  const ratio = value => (Math.max(bg,value)+0.05)/(Math.min(bg,value)+0.05);
  return ratio(a) >= ratio(b) ? first : second;
}

function cappedPositions(length, budget) {
  if (length <= 0 || budget <= 0) return [];
  const count = Math.min(length, budget);
  if (count === length) return Array.from({length: count}, (_, index) => index);
  if (count === 1) return [0];
  const last = length - 1, denominator = count - 1;
  const result = [];
  for (let index = 0; index < count; index++) {
    result.push(Math.floor(index * last / denominator));
  }
  return result;
}

function cappedRawPoints(points, budget) {
  const source = Array.isArray(points) ? points : [];
  return cappedPositions(source.length, budget).map(index => source[index]);
}

function cap2DSeries() {
  if (allPos.length <= MAX_2D_POINTS) return;
  const selected = cappedPositions(allPos.length, MAX_2D_POINTS);
  allPos = selected.map(index => allPos[index]);
  allAlt = selected.map(index => allAlt[index]);
  allTs = selected.map(index => allTs[index]);
  allIdx = selected.map(index => allIdx[index]);
}

function cap3DSeries() {
  if (allPos3d.length <= MAX_3D_POINTS) return;
  const selected = cappedPositions(allPos3d.length, MAX_3D_POINTS);
  allPos3d = selected.map(index => allPos3d[index]);
  allAlt3d = selected.map(index => allAlt3d[index]);
  allIdx3d = selected.map(index => allIdx3d[index]);
}

function setFollowing(enabled, notifyBridge) {
  const nextFollowing = !!enabled;
  const changed = following !== nextFollowing;
  following = nextFollowing;
  const btn = document.getElementById("btnFollow");
  if (btn) {
    btn.classList.toggle("active", following);
    btn.setAttribute("aria-pressed", String(following));
  }
  if (following) window.centerOnCurrent();
  if (changed && bridge && notifyBridge && typeof bridge.onFollowChanged === "function") {
    bridge.onFollowChanged(following);
  }
}

function firstValid3DReference() {
  const source = allPos3d.length ? allPos3d : allPos;
  for (const p of source) {
    if (validC(p.lat, p.lon)) return p;
  }
  return null;
}

function toLocal3DPoint(p, alt, refLat, refLon) {
  const cosRef = Math.cos(refLat * D2R);
  const x = (p.lon - refLon) * D2R * EARTH_R * cosRef;
  const z = -(p.lat - refLat) * D2R * EARTH_R;
  const relAlt = alt - groundRefAlt;
  const terrainY = getTerrainY(p.lat, p.lon, refLat, refLon);
  return new THREE.Vector3(x, terrainY + relAlt * ALT_EXAGGERATION, z);
}

/* ── Convert lat/lon/alt to local XYZ for the 3D scene ───────────── */
function toLocal3D(positions, altitudes, refLat, refLon) {
  const pts = [];
  for (let i = 0; i < positions.length; i++) {
    const p = positions[i];
    if (!validC(p.lat, p.lon)) continue;
    pts.push(toLocal3DPoint(p, altitudes[i], refLat, refLon));
  }
  return pts;
}

function getTerrainY(lat, lon, refLat, refLon) {
  if (!groundElevGrid || ground3dMode === "box") return 0;
  const g = groundElevGrid;
  const u = (lon - g.lonL) / (g.lonR - g.lonL);
  const v = (g.latT - lat) / (g.latT - g.latB);
  if (u < 0 || u > 1 || v < 0 || v > 1) return 0;
  const gx = u * (g.cols - 1), gy = v * (g.rows - 1);
  const ix = Math.min(Math.floor(gx), g.cols - 2), iy = Math.min(Math.floor(gy), g.rows - 2);
  const fx = gx - ix, fy = gy - iy;
  const e00 = g.data[iy * g.cols + ix], e10 = g.data[iy * g.cols + ix + 1];
  const e01 = g.data[(iy+1) * g.cols + ix], e11 = g.data[(iy+1) * g.cols + ix + 1];
  const elev = (e00*(1-fx)*(1-fy) + e10*fx*(1-fy) + e01*(1-fx)*fy + e11*fx*fy);
  return (elev - groundRefAlt) * ALT_EXAGGERATION;
}

function createWideLine(color, width, opacity, isSegments=false) {
  const geometry = isSegments ? new LineSegmentsGeometry() : new LineGeometry();
  const material = new LineMaterial({
    color,
    linewidth: width,
    transparent: opacity < 1,
    opacity,
    depthTest: true,
    depthWrite: false
  });
  const line = isSegments ? new LineSegments2(geometry, material) : new Line2(geometry, material);
  line.userData.isSegments = isSegments;
  line.frustumCulled = false;
  line.renderOrder = isSegments ? 1 : 2;
  return line;
}

function updateLineMaterialResolution() {
  if (!renderer3d) return;
  const size = renderer3d.getSize(new THREE.Vector2());
  for (const line of [ghostLine3d, trailLine3d, pathLine3d]) {
    if (line?.material?.resolution) line.material.resolution.set(size.x, size.y);
  }
}

function updateWideLine(line, points) {
  if (!line) return;
  const minPoints = line.userData.isSegments ? 2 : 2;
  if (!points || points.length < minPoints) {
    line.visible = false;
    return;
  }
  const flat = [];
  for (const p of points) flat.push(p.x, p.y, p.z);
  line.geometry.dispose();
  line.geometry = line.userData.isSegments ? new LineSegmentsGeometry() : new LineGeometry();
  line.geometry.setPositions(flat);
  line.visible = true;
  updateLineMaterialResolution();
}

function setLineStyle(line, color, width, opacity) {
  if (!line?.material) return;
  line.material.color.set(color);
  line.material.linewidth = width;
  line.material.opacity = opacity;
  line.material.transparent = opacity < 1;
  line.material.needsUpdate = true;
}

function setGridColors(primary, secondary) {
  if (!groundGrid3d) return;
  const materials = Array.isArray(groundGrid3d.material) ? groundGrid3d.material : [groundGrid3d.material];
  if (materials[0]) materials[0].color.set(primary);
  if (materials[1]) materials[1].color.set(secondary);
}

function apply3DTheme() {
  if (!scene3d) return;
  const isDark = currentThemeDark;
  const bg = currentThemePalette?.bg_dark || currentThemePalette?.bg_base || (isDark ? "#080a10" : "#f4f7fb");
  const fog = currentThemePalette?.bg_dark || currentThemePalette?.bg_base || (isDark ? "#080a10" : "#f4f7fb");
  const trail = currentThemePalette?.accent_link || (isDark ? 0x64c8ff : 0x0057b8);
  const ghost = currentThemePalette?.border_light || (isDark ? 0x3a5a78 : 0x4a6478);
  const drop = currentThemePalette?.info || currentThemePalette?.accent_link || (isDark ? 0x64c8ff : 0x0b63ce);
  const rocket = currentThemePalette?.warning || currentThemePalette?.danger || (isDark ? 0xff8a3d : 0xd44818);
  const rocketEmissive = rocket;
  const launch = currentThemePalette?.success || (isDark ? 0x44cc77 : 0x12864a);
  const launchEmissive = launch;

  scene3d.background = new THREE.Color(bg);
  if (scene3d.fog) scene3d.fog.color.set(fog);
  setGridColors(
    currentThemePalette?.border_default || (isDark ? 0x223044 : 0x75889a),
    currentThemePalette?.border_subtle || (isDark ? 0x121a28 : 0xc2ccd6));
  if (groundMesh3d && ground3dMode === "box") {
    groundMesh3d.material.color.set(currentThemePalette?.bg_panel || (isDark ? 0x0a0e16 : 0xe8edf2));
  } else if (groundMesh3d?.material?.color) {
    groundMesh3d.material.color.set(0xffffff);
  }

  setLineStyle(ghostLine3d, ghost, isDark ? 2.0 : 2.25, isDark ? 0.48 : 0.44);
  setLineStyle(trailLine3d, trail, isDark ? 4.5 : 5.0, 1.0);
  setLineStyle(pathLine3d, drop, isDark ? 1.25 : 1.5, isDark ? 0.18 : 0.28);

  if (launchSphere3d?.material) {
    launchSphere3d.material.color.set(launch);
    launchSphere3d.material.emissive.set(launchEmissive);
  }
  if (rocketSphere3d?.material) {
    rocketSphere3d.material.color.set(rocket);
    rocketSphere3d.material.emissive.set(rocketEmissive);
  }
  if (rocketGlow3d?.material) {
    rocketGlow3d.material.color.set(rocket);
  }
  const info = document.getElementById("info3d");
  if (info) info.style.color = currentThemePalette?.text_dim || (isDark ? "#9fb4c8" : "#1f3142");
  updateLineMaterialResolution();
  request3DRender(2);
}

/* ── Leaflet 2D ──────────────────────────────────────────────────── */
function initMap() {
  map = L.map("map", {
    center: [51.049,-1.398], zoom: 15,
    zoomControl: false,
    attributionControl: true,
    preferCanvas: true
  });

  tileLayerMap = L.tileLayer("https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png",{
    maxZoom:19, subdomains:"abcd",
    attribution:'&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors &copy; <a href="https://carto.com/attributions">CARTO</a>'
  });
  tileLayerLight = L.tileLayer("https://{s}.basemaps.cartocdn.com/voyager/{z}/{x}/{y}{r}.png",{
    maxZoom:19, subdomains:"abcd",
    attribution:'&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors &copy; <a href="https://carto.com/attributions">CARTO</a>'
  });
  tileLayerTerrain = L.tileLayer("https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png",{
    maxZoom:17, subdomains:"abc",
    attribution:'Map data &copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors | Map style &copy; <a href="https://opentopomap.org">OpenTopoMap</a> (CC-BY-SA)'
  });
  tileLayerSat = L.tileLayer("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",{
    maxZoom:19,
    attribution:'Tiles &copy; Esri &mdash; Source: Esri, Maxar, Earthstar Geographics, and the GIS User Community'
  });
  tileLayerTerrain.addTo(map);

  pathLine = L.polyline([],{color:"rgba(60,100,140,0.35)",weight:5,lineCap:"round",lineJoin:"round",interactive:false}).addTo(map);
  trailLine = L.polyline([],{color:"#4fa5de",weight:4,lineCap:"round",lineJoin:"round",interactive:false}).addTo(map);

  const pulseIcon = L.divIcon({className:"",
    html:'<div class="rocket-marker"><div class="rocket-pulse"></div><div class="rocket-core"></div></div>',
    iconSize:[32,32],iconAnchor:[16,16]});
  const launchIcon = L.divIcon({className:"",
    html:'<div class="launch-marker"></div>',
    iconSize:[14,14],iconAnchor:[7,7]});

  launchMarker = L.marker([0,0],{icon:launchIcon,interactive:false}).addTo(map);
  rocketMarker = L.marker([0,0],{icon:pulseIcon,interactive:false,zIndexOffset:1000}).addTo(map);
  launchMarker.setOpacity(0);
  rocketMarker.setOpacity(0);

  map.on("dragstart",()=>{if(following)setFollowing(false,true);});
}

/* ── Layer switching ─────────────────────────────────────────────── */
function switchLayer(name, force = false) {
  if (activeLayer === name && !force) return;
  activeLayer = name;
  const selected = name === "map"
    ? (currentThemeDark ? tileLayerMap : tileLayerLight)
    : name === "terrain" ? tileLayerTerrain : tileLayerSat;
  for (const layer of [tileLayerMap, tileLayerLight, tileLayerTerrain, tileLayerSat]) {
    if (!layer) continue;
    if (layer === selected) {
      if (!map.hasLayer(layer)) map.addLayer(layer);
    } else if (map.hasLayer(layer)) {
      map.removeLayer(layer);
    }
  }

  const tilePane = document.querySelector(".leaflet-tile-pane");
  if (tilePane) {
    tilePane.style.filter = (name === "map" && currentThemeDark)
      ? "brightness(0.7) contrast(1.1) saturate(0.5)" : "none";
  }

  const pathColor = colorWithAlpha(
    currentThemePalette?.info || currentThemePalette?.border_light,
    name === "map" ? 0.42 : 0.62,
    name === "map" ? "rgba(60,100,140,0.42)" : "rgba(20,60,100,0.62)");
  const trailColor = name === "sat"
    ? (currentThemePalette?.warning || "#ff9944")
    : (currentThemePalette?.accent_link || "#4fa5de");
  if (pathLine) pathLine.setStyle({ color: pathColor });
  if (trailLine) trailLine.setStyle({ color: trailColor });

  const activeButtonId = "ls" + name.charAt(0).toUpperCase() + name.slice(1);
  document.querySelectorAll("#layerSwitcher .ls-btn").forEach(button => {
    const active = button.id === activeButtonId;
    button.classList.toggle("active", active);
    button.setAttribute("aria-pressed", String(active));
  });
}

/* ── Three.js 3D ─────────────────────────────────────────────────── */
function init3D() {
  if (!THREE_LOADED || !ORBIT_LOADED || !WIDE_LINES_LOADED) {
    document.getElementById("info3d").textContent = "3D unavailable: Three.js libraries failed to load";
    return;
  }
  const container = document.getElementById("view3d");
  scene3d = new THREE.Scene();
  scene3d.background = new THREE.Color(0x080a10);
  scene3d.fog = new THREE.FogExp2(0x080a10, 0.00015);

  camera3d = new THREE.PerspectiveCamera(55, 1, 1, 100000);
  camera3d.position.set(200, 400, 500);

  renderer3d = new THREE.WebGLRenderer({ antialias: true, alpha: false });
  renderer3d.setPixelRatio(window.devicePixelRatio);
  container.appendChild(renderer3d.domElement);

  controls3d = new OrbitControls(camera3d, renderer3d.domElement);
  controls3d.enableDamping = true;
  controls3d.dampingFactor = 0.08;
  controls3d.minDistance = 20;
  controls3d.maxDistance = 8000;
  controls3d.maxPolarAngle = Math.PI * 0.48;
  controls3d.addEventListener("start", () => {
    if (following) setFollowing(false, true);
    request3DRender(12);
  });
  controls3d.addEventListener("change", () => request3DRender(2));
  controls3d.addEventListener("end", () => request3DRender(12));

  // ground plane grid
  groundGrid3d = new THREE.GridHelper(4000, 80, 0x1a2030, 0x111822);
  scene3d.add(groundGrid3d);

  groundMesh3d = new THREE.Mesh(
    new THREE.PlaneGeometry(6000, 6000),
    new THREE.MeshBasicMaterial({ color: 0x0a0e16, side: THREE.DoubleSide })
  );
  groundMesh3d.rotation.x = -Math.PI / 2;
  groundMesh3d.position.y = -0.5;
  scene3d.add(groundMesh3d);

  scene3d.add(new THREE.AmbientLight(0x667788, 2.0));
  const dLight = new THREE.DirectionalLight(0xddeeff, 1.0);
  dLight.position.set(200, 600, 300);
  scene3d.add(dLight);

  // ghost line (full path, dimmed)
  ghostLine3d = createWideLine(0x3a5a78, 2.0, 0.48);
  scene3d.add(ghostLine3d);

  // trail line (active, bright)
  trailLine3d = createWideLine(0x64c8ff, 4.5, 1.0);
  scene3d.add(trailLine3d);

  // vertical drop lines from trail to ground
  pathLine3d = createWideLine(0x64c8ff, 1.25, 0.18, true);
  scene3d.add(pathLine3d);

  // launch sphere
  launchSphere3d = new THREE.Mesh(
    new THREE.SphereGeometry(4, 16, 16),
    new THREE.MeshPhongMaterial({ color: 0x44cc77, emissive: 0x226633 })
  );
  launchSphere3d.visible = false;
  scene3d.add(launchSphere3d);

  // rocket sphere + glow
  rocketSphere3d = new THREE.Mesh(
    new THREE.SphereGeometry(6, 16, 16),
    new THREE.MeshPhongMaterial({ color: 0xff7a30, emissive: 0x993300 })
  );
  rocketSphere3d.visible = false;
  scene3d.add(rocketSphere3d);

  rocketGlow3d = new THREE.Mesh(
    new THREE.SphereGeometry(14, 16, 16),
    new THREE.MeshBasicMaterial({ color: 0xff7a30, transparent: true, opacity: 0.12 })
  );
  rocketGlow3d.visible = false;
  scene3d.add(rocketGlow3d);

  resize3D();
  apply3DTheme();
  request3DRender(2);
}

function resize3D() {
  const c = document.getElementById("view3d");
  const w = c.clientWidth, h = c.clientHeight;
  if (w === 0 || h === 0) return;
  camera3d.aspect = w / h;
  camera3d.updateProjectionMatrix();
  renderer3d.setSize(w, h);
  updateLineMaterialResolution();
  needsUpdate3d = true;
  request3DRender(2);
}

function request3DRender(frames = 1) {
  needsUpdate3d = true;
  renderFramesRemaining3d = Math.max(renderFramesRemaining3d, Math.max(1, frames));
  if (!hostVisible || document.hidden || activeView !== "3d" || !renderer3d
      || animId3d !== null) return;
  animId3d = requestAnimationFrame(animate3D);
}

function animate3D() {
  animId3d = null;
  if (!hostVisible || document.hidden || activeView !== "3d" || !renderer3d) return;

  const followChanged = update3DFollowCamera();
  const controlsChanged = controls3d.update();

  // Pulse briefly after data/camera changes, then leave the renderer idle.
  if (rocketGlow3d && rocketGlow3d.visible) {
    const t = performance.now() * 0.002;
    const s = 1.0 + 0.3 * Math.sin(t);
    rocketGlow3d.scale.setScalar(s);
    rocketGlow3d.material.opacity = 0.08 + 0.06 * Math.sin(t);
  }

  if (needsUpdate3d || followChanged || controlsChanged || renderFramesRemaining3d > 0) {
    renderer3d.render(scene3d, camera3d);
  }
  needsUpdate3d = false;
  renderFramesRemaining3d = Math.max(0, renderFramesRemaining3d - 1);
  if (animId3d === null && (renderFramesRemaining3d > 0 || followChanged || controlsChanged)) {
    animId3d = requestAnimationFrame(animate3D);
  }
}

function update3DEntities() {
  if (!scene3d) return;

  const allValid = [], allValidAlt = [];
  const ghostPos = allPos3d.length ? allPos3d : allPos;
  const ghostAlt = allPos3d.length ? allAlt3d : allAlt;
  for (let i = 0; i < ghostPos.length; i++) {
    if (validC(ghostPos[i].lat, ghostPos[i].lon)) { allValid.push(ghostPos[i]); allValidAlt.push(ghostAlt[i]); }
  }
  if (allValid.length === 0) {
    updateWideLine(ghostLine3d, []);
    updateWideLine(trailLine3d, []);
    updateWideLine(pathLine3d, []);
    currentRocketWorld3d = null;
    launchSphere3d.visible = false;
    rocketSphere3d.visible = false;
    rocketGlow3d.visible = false;
    document.getElementById("info3d").textContent = "";
    path3dDataDirty = false;
    request3DRender(2);
    return;
  }

  const refLat = allValid[0].lat, refLon = allValid[0].lon;

  // ghost (full path)
  if (path3dDataDirty) {
    const ghostPts = toLocal3D(allValid, allValidAlt, refLat, refLon);
    updateWideLine(ghostLine3d, ghostPts);
    path3dDataDirty = false;
  }

  // trail (active)
  const trailValid = [], trailAlt = [];
  const trailPos = allPos3d.length ? allPos3d : allPos;
  const trailAltSrc = allPos3d.length ? allAlt3d : allAlt;
  const trailIdxSrc = allPos3d.length ? allIdx3d : allIdx;
  for (let i = 0; i < trailPos.length; i++) {
    if (sampleIdx(trailIdxSrc, i) < trailLen && validC(trailPos[i].lat, trailPos[i].lon)) {
      trailValid.push(trailPos[i]); trailAlt.push(trailAltSrc[i]);
    }
  }
  if (currentExactPoint && validC(currentExactPoint.lat, currentExactPoint.lon)) {
    const last = trailValid.length ? trailValid[trailValid.length - 1] : null;
    if (!last || Math.abs(last.lat - currentExactPoint.lat) > 1e-10 || Math.abs(last.lon - currentExactPoint.lon) > 1e-10) {
      trailValid.push({lat:currentExactPoint.lat, lon:currentExactPoint.lon});
      trailAlt.push(currentExactPoint.alt);
    }
  }

  if (trailValid.length > 0) {
    const trailPts = toLocal3D(trailValid, trailAlt, refLat, refLon);
    updateWideLine(trailLine3d, trailPts);

    // vertical drop lines (every 3rd point) — to terrain surface
    const dropVerts = [];
    for (let i = 0; i < trailPts.length; i += 3) {
      dropVerts.push(trailPts[i].clone());
      const tY = (groundElevGrid && ground3dMode !== "box")
        ? getTerrainY(trailValid[Math.min(i, trailValid.length-1)].lat, trailValid[Math.min(i, trailValid.length-1)].lon, refLat, refLon)
        : 0;
      dropVerts.push(new THREE.Vector3(trailPts[i].x, tY, trailPts[i].z));
    }
    updateWideLine(pathLine3d, dropVerts);

    // launch
    launchSphere3d.position.copy(trailPts[0]);
    launchSphere3d.visible = true;

    // rocket
    const rp = trailPts[trailPts.length - 1];
    currentRocketWorld3d = rp.clone();
    rocketSphere3d.position.copy(rp);
    rocketSphere3d.visible = true;
    rocketGlow3d.position.copy(rp);
    rocketGlow3d.visible = true;

    // info label
    const lastA = trailAlt[trailAlt.length - 1];
    const agl = lastA - groundRefAlt;
    const altLabel = ground3dMode !== "box"
      ? "AGL " + fmtAlt(agl) + " (MSL " + fmtAlt(lastA) + ")"
      : "Alt " + fmtAlt(lastA);
    document.getElementById("info3d").textContent =
      altLabel + "  ·  " + ALT_EXAGGERATION + "x vert" +
      "  ·  Locate/Fit/Follow  ·  Drag to orbit";
  } else {
    updateWideLine(trailLine3d, []);
    updateWideLine(pathLine3d, []);
    currentRocketWorld3d = null;
    launchSphere3d.visible = false;
    rocketSphere3d.visible = false;
    rocketGlow3d.visible = false;
  }
  request3DRender(following ? 12 : 2);
}

function currentRocket3DPoint() {
  if (currentRocketWorld3d) return currentRocketWorld3d.clone();
  const ref = firstValid3DReference();
  if (!ref) return null;

  if (currentExactPoint && validC(currentExactPoint.lat, currentExactPoint.lon)) {
    return toLocal3DPoint(currentExactPoint, currentExactPoint.alt, ref.lat, ref.lon);
  }

  const trailPos = allPos3d.length ? allPos3d : allPos;
  const trailAltSrc = allPos3d.length ? allAlt3d : allAlt;
  const trailIdxSrc = allPos3d.length ? allIdx3d : allIdx;
  for (let i = trailPos.length - 1; i >= 0; i--) {
    if (sampleIdx(trailIdxSrc, i) < trailLen && validC(trailPos[i].lat, trailPos[i].lon)) {
      return toLocal3DPoint(trailPos[i], trailAltSrc[i], ref.lat, ref.lon);
    }
  }
  return null;
}

function cameraViewDirection() {
  if (!camera3d || !controls3d) return new THREE.Vector3(0.65, 0.55, 0.50).normalize();
  const dir = camera3d.position.clone().sub(controls3d.target);
  if (dir.lengthSq() < 1e-6) return new THREE.Vector3(0.65, 0.55, 0.50).normalize();
  return dir.normalize();
}

function currentPath3DSize() {
  const ref = firstValid3DReference();
  if (!ref) return 80;
  const source = allPos3d.length ? allPos3d : allPos;
  const altSource = allPos3d.length ? allAlt3d : allAlt;
  const pts = toLocal3D(source, altSource, ref.lat, ref.lon);
  if (pts.length === 0) return 80;
  const box = new THREE.Box3();
  for (const p of pts) box.expandByPoint(p);
  const size = box.getSize(new THREE.Vector3());
  return Math.max(size.x, size.y, size.z, 20);
}

function center3DCameraOnRocket() {
  if (!scene3d || !controls3d || !camera3d) return false;
  const target = currentRocket3DPoint();
  if (!target) return false;
  const pathSize = currentPath3DSize();
  const distance = Math.max(35, Math.min(220, pathSize * 0.35));
  const dir = cameraViewDirection();
  controls3d.target.copy(target);
  camera3d.position.copy(target).add(dir.multiplyScalar(distance));
  controls3d.minDistance = Math.max(4, distance * 0.08);
  controls3d.maxDistance = Math.max(8000, pathSize * 8);
  controls3d.update();
  request3DRender(12);
  return true;
}

function fit3DPath() {
  if (!scene3d) return;
  const allValid = [], allValidAlt = [];
  const cameraPos = allPos3d.length ? allPos3d : allPos;
  const cameraAlt = allPos3d.length ? allAlt3d : allAlt;
  for (let i = 0; i < cameraPos.length; i++) {
    if (validC(cameraPos[i].lat, cameraPos[i].lon)) { allValid.push(cameraPos[i]); allValidAlt.push(cameraAlt[i]); }
  }
  if (allValid.length === 0) return;
  const refLat = allValid[0].lat, refLon = allValid[0].lon;
  const pts = toLocal3D(allValid, allValidAlt, refLat, refLon);

  const box = new THREE.Box3();
  for (const p of pts) box.expandByPoint(p);
  const center = new THREE.Vector3();
  box.getCenter(center);
  const size = box.getSize(new THREE.Vector3());
  const maxDim = Math.max(size.x, size.y, size.z, 20);
  const distance = Math.max(50, maxDim * 1.7);

  controls3d.target.copy(center);
  controls3d.minDistance = Math.max(4, maxDim * 0.03);
  controls3d.maxDistance = Math.max(8000, maxDim * 10);
  camera3d.position.copy(center).add(new THREE.Vector3(0.65, 0.55, 0.50).normalize().multiplyScalar(distance));
  controls3d.update();
  request3DRender(12);
}

function fit3DCamera() {
  fit3DPath();
}

function set3DCameraDistance(scale) {
  if (!camera3d || !controls3d) return;
  const offset = camera3d.position.clone().sub(controls3d.target);
  const current = Math.max(offset.length(), controls3d.minDistance);
  const next = Math.max(controls3d.minDistance, Math.min(controls3d.maxDistance, current * scale));
  const dir = offset.lengthSq() < 1e-6 ? cameraViewDirection() : offset.normalize();
  camera3d.position.copy(controls3d.target).add(dir.multiplyScalar(next));
  controls3d.update();
  request3DRender(12);
}

function update3DFollowCamera() {
  if (!following || activeView !== "3d" || !camera3d || !controls3d) return false;
  const target = currentRocket3DPoint();
  if (!target) return false;
  if (controls3d.target.distanceToSquared(target) < 1e-4) return false;
  const offset = camera3d.position.clone().sub(controls3d.target);
  controls3d.target.lerp(target, 0.18);
  camera3d.position.copy(controls3d.target).add(offset);
  return true;
}

/* ── 3D ground tile rendering ────────────────────────────────────── */
const TILE_URLS = {
  map:     (z,x,y) => `https://a.basemaps.cartocdn.com/dark_all/${z}/${x}/${y}.png`,
  terrain: (z,x,y) => `https://a.tile.opentopomap.org/${z}/${x}/${y}.png`,
  sat:     (z,x,y) => `https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/${z}/${y}/${x}`
};

const WEB_MERCATOR_MAX_LAT = 85.05112878;
const MAX_GROUND_TILE_SIDE = 6;
const MAX_GROUND_TILES = 36;

function clamp(value, minimum, maximum) {
  return Math.max(minimum, Math.min(maximum, value));
}

function lonToTile(lon, z) {
  const n = 1 << z;
  const boundedLon = clamp(lon, -180, 180 - Number.EPSILON);
  return clamp(Math.floor((boundedLon + 180) / 360 * n), 0, n - 1);
}
function latToTile(lat, z) {
  const n = 1 << z;
  const r = Math.PI / 180 * clamp(lat, -WEB_MERCATOR_MAX_LAT, WEB_MERCATOR_MAX_LAT);
  return clamp(Math.floor((1 - Math.log(Math.tan(r) + 1 / Math.cos(r)) / Math.PI) / 2 * n), 0, n - 1);
}
function tileToBounds(x, y, z) {
  const n = 1 << z;
  const lonL = x / n * 360 - 180;
  const lonR = (x + 1) / n * 360 - 180;
  const latT = Math.atan(Math.sinh(Math.PI * (1 - 2 * y / n))) * 180 / Math.PI;
  const latB = Math.atan(Math.sinh(Math.PI * (1 - 2 * (y + 1) / n))) * 180 / Math.PI;
  return { lonL, lonR, latT, latB };
}

function boundedTileRange(bounds, preferredZoom) {
  for (let z = preferredZoom; z >= 0; z--) {
    const xMin = lonToTile(bounds.minLon, z);
    const xMax = lonToTile(bounds.maxLon, z);
    const yMin = latToTile(bounds.maxLat, z);
    const yMax = latToTile(bounds.minLat, z);
    const cols = xMax - xMin + 1;
    const rows = yMax - yMin + 1;
    if (cols > 0 && rows > 0
        && cols <= MAX_GROUND_TILE_SIDE && rows <= MAX_GROUND_TILE_SIDE
        && cols * rows <= MAX_GROUND_TILES) {
      return { z, xMin, xMax, yMin, yMax, cols, rows };
    }
  }
  return null;
}

async function buildGroundTexture(tileType, generation) {
  const groundPos = allPos3d.length ? allPos3d : allPos;
  if (groundPos.length === 0) return false;
  const valid = groundPos.filter(p => validC(p.lat, p.lon));
  if (valid.length === 0) return false;

  let minLat = Infinity, maxLat = -Infinity, minLon = Infinity, maxLon = -Infinity;
  for (const p of valid) {
    minLat = Math.min(minLat, p.lat); maxLat = Math.max(maxLat, p.lat);
    minLon = Math.min(minLon, p.lon); maxLon = Math.max(maxLon, p.lon);
  }
  const padLat = (maxLat - minLat) * 0.5 + 0.002;
  const padLon = (maxLon - minLon) * 0.5 + 0.002;
  minLat -= padLat; maxLat += padLat;
  minLon -= padLon; maxLon += padLon;

  const tileRange = boundedTileRange({ minLat, maxLat, minLon, maxLon }, 15);
  if (!tileRange) return false;
  const { z, xMin, xMax, yMin, yMax, cols, rows } = tileRange;

  const TILE_PX = 256;
  const cw = cols * TILE_PX, ch = rows * TILE_PX;
  const canvas = document.createElement("canvas");
  canvas.width = cw;
  canvas.height = ch;
  const context = canvas.getContext("2d");
  if (!context) return false;
  context.fillStyle = currentThemePalette?.bg_dark || currentThemePalette?.bg_base || "#0a0e16";
  context.fillRect(0, 0, cw, ch);

  const urlFn = TILE_URLS[tileType] || TILE_URLS.sat;
  const promises = [];
  for (let ty = yMin; ty <= yMax; ty++) {
    for (let tx = xMin; tx <= xMax; tx++) {
      const dx = (tx - xMin) * TILE_PX, dy = (ty - yMin) * TILE_PX;
      const url = urlFn(z, tx, ty);
      promises.push(
        new Promise(resolve => {
          const img = new Image();
          img.crossOrigin = "anonymous";
          img.onload = () => { context.drawImage(img, dx, dy, TILE_PX, TILE_PX); resolve(); };
          img.onerror = () => resolve();
          img.src = url;
        })
      );
    }
  }
  await Promise.all(promises);
  if (generation !== groundLoadGeneration || tileType !== ground3dMode) return false;

  const cornerTL = tileToBounds(xMin, yMin, z);
  const cornerBR = tileToBounds(xMax, yMax, z);
  const bounds = {
    lonL: cornerTL.lonL, lonR: cornerBR.lonR,
    latT: cornerTL.latT, latB: cornerBR.latB
  };

  if (groundTexture) groundTexture.dispose();
  groundTexCanvas = canvas;
  groundTexCtx = context;
  groundTexBounds = bounds;
  groundTexture = new THREE.CanvasTexture(canvas);
  groundTexture.minFilter = THREE.LinearFilter;
  groundTexture.magFilter = THREE.LinearFilter;
  groundTexture.colorSpace = THREE.SRGBColorSpace;
  return true;
}

async function fetchElevationGrid(bounds, generation) {
  const tileRange = boundedTileRange({
    minLat: bounds.latB,
    maxLat: bounds.latT,
    minLon: bounds.lonL,
    maxLon: bounds.lonR
  }, 12);
  if (!tileRange) return false;
  const { z, xMin, xMax, yMin, yMax, cols, rows } = tileRange;
  const TILE_PX = 256;
  const cw = cols * TILE_PX, ch = rows * TILE_PX;

  const eCanvas = document.createElement("canvas");
  eCanvas.width = cw; eCanvas.height = ch;
  const eCtx = eCanvas.getContext("2d", { willReadFrequently: true });
  if (!eCtx) return false;
  eCtx.fillStyle = "#808000"; eCtx.fillRect(0, 0, cw, ch);

  const promises = [];
  for (let ty = yMin; ty <= yMax; ty++) {
    for (let tx = xMin; tx <= xMax; tx++) {
      const dx = (tx - xMin) * TILE_PX, dy = (ty - yMin) * TILE_PX;
      const url = `https://s3.amazonaws.com/elevation-tiles-prod/terrarium/${z}/${tx}/${ty}.png`;
      promises.push(new Promise(resolve => {
        const img = new Image(); img.crossOrigin = "anonymous";
        img.onload = () => { eCtx.drawImage(img, dx, dy, TILE_PX, TILE_PX); resolve(); };
        img.onerror = () => resolve();
        img.src = url;
      }));
    }
  }
  await Promise.all(promises);
  if (generation !== groundLoadGeneration) return false;

  const GRID = 128;
  const data = new Float32Array(GRID * GRID);
  const cornerTL = tileToBounds(xMin, yMin, z);
  const cornerBR = tileToBounds(xMax, yMax, z);

  for (let gy = 0; gy < GRID; gy++) {
    for (let gx = 0; gx < GRID; gx++) {
      const px = Math.floor((gx / (GRID - 1)) * (cw - 1));
      const py = Math.floor((gy / (GRID - 1)) * (ch - 1));
      const d = eCtx.getImageData(px, py, 1, 1).data;
      data[gy * GRID + gx] = (d[0] * 256 + d[1] + d[2] / 256) - 32768;
    }
  }

  groundElevGrid = {
    data, cols: GRID, rows: GRID,
    lonL: cornerTL.lonL, lonR: cornerBR.lonR,
    latT: cornerTL.latT, latB: cornerBR.latB
  };

  const groundAlt = allAlt3d.length ? allAlt3d : allAlt;
  if (groundAlt.length > 0) {
    groundRefAlt = validAltitude(groundAlt[0]) ? groundAlt[0] : 0;
  }
  return true;
}

function applyGroundTexture() {
  if (!groundMesh3d || !groundTexture || !groundTexBounds) return;
  const groundPos = allPos3d.length ? allPos3d : allPos;
  const refLat = groundPos.find(p => validC(p.lat, p.lon))?.lat ?? 0;
  const refLon = groundPos.find(p => validC(p.lat, p.lon))?.lon ?? 0;
  const cosRef = Math.cos(refLat * D2R);

  const xL = (groundTexBounds.lonL - refLon) * D2R * EARTH_R * cosRef;
  const xR = (groundTexBounds.lonR - refLon) * D2R * EARTH_R * cosRef;
  const zT = -(groundTexBounds.latT - refLat) * D2R * EARTH_R;
  const zB = -(groundTexBounds.latB - refLat) * D2R * EARTH_R;
  const w = Math.abs(xR - xL), h = Math.abs(zB - zT);
  const cx = (xL + xR) / 2, cz = (zT + zB) / 2;

  const segs = 127;
  const geo = new THREE.PlaneGeometry(w, h, segs, segs);
  geo.rotateX(-Math.PI / 2);

  if (groundElevGrid) {
    const pos = geo.attributes.position;
    const g = groundElevGrid;
    for (let i = 0; i < pos.count; i++) {
      const lx = pos.getX(i) + w / 2;
      const lz = pos.getZ(i) + h / 2;
      const u = lx / w, v = lz / h;
      const lat = groundTexBounds.latT - v * (groundTexBounds.latT - groundTexBounds.latB);
      const lon = groundTexBounds.lonL + u * (groundTexBounds.lonR - groundTexBounds.lonL);
      const ty = getTerrainY(lat, lon, refLat, refLon);
      pos.setY(i, ty);
    }
    pos.needsUpdate = true;
    geo.computeVertexNormals();
  }

  groundMesh3d.geometry.dispose();
  groundMesh3d.geometry = geo;
  groundMesh3d.material.dispose();
  groundMesh3d.material = new THREE.MeshLambertMaterial({ map: groundTexture, side: THREE.DoubleSide });
  groundMesh3d.rotation.set(0, 0, 0);
  groundMesh3d.position.set(cx, 0, cz);
  path3dDataDirty = true;
  request3DRender(2);
}

async function switchGround3D(mode, force = false) {
  if (ground3dMode === mode && !force) return;
  ground3dMode = mode;
  const generation = ++groundLoadGeneration;
  groundTexBounds = null;
  groundElevGrid = null;
  updateProviderAttribution();

  document.querySelectorAll("#ground3dToggle .ls-btn").forEach(button => {
    button.classList.remove("active");
    button.setAttribute("aria-pressed", "false");
  });
  const btnId = mode === "box" ? "g3dBox"
    : mode === "map" ? "g3dMap"
    : mode === "terrain" ? "g3dTerrain" : "g3dSat";
  const activeButton = document.getElementById(btnId);
  activeButton.classList.add("active");
  activeButton.setAttribute("aria-pressed", "true");

  if (mode === "box") {
    groundGrid3d.visible = true;
    groundElevGrid = null;
    groundRefAlt = 0;
    groundMesh3d.geometry.dispose();
    groundMesh3d.geometry = new THREE.PlaneGeometry(6000, 6000);
    groundMesh3d.material.dispose();
    groundMesh3d.material = new THREE.MeshBasicMaterial({ color: 0x0a0e16, side: THREE.DoubleSide });
    groundMesh3d.rotation.x = -Math.PI / 2;
    groundMesh3d.position.set(0, -0.5, 0);
    scene3d.fog = new THREE.FogExp2(0x080a10, 0.00015);
    scene3d.background = new THREE.Color(0x080a10);
    path3dDataDirty = true;
    apply3DTheme();
    update3DEntities();
    return;
  }

  groundGrid3d.visible = false;
  scene3d.fog = null;
  scene3d.background = new THREE.Color(mode === "sat" ? 0x0a1520 : 0x88aacc);
  request3DRender(2);
  const textureReady = await buildGroundTexture(mode, generation);
  if (!textureReady || generation !== groundLoadGeneration) return;
  const elevationReady = await fetchElevationGrid(groundTexBounds, generation);
  if (!elevationReady || generation !== groundLoadGeneration) return;
  applyGroundTexture();
  apply3DTheme();
  update3DEntities();
}

/* ── View switching ──────────────────────────────────────────────── */
function switchView(mode) {
  activeView = mode;
  const mapEl = document.getElementById("map");
  const v3El = document.getElementById("view3d");
  const btn2 = document.getElementById("btn2D");
  const btn3 = document.getElementById("btn3D");
  const lsEl = document.getElementById("layerSwitcher");

  const g3El = document.getElementById("ground3dToggle");
  updateProviderAttribution();

  if (mode === "3d") {
    mapEl.style.display = "none";
    v3El.style.display = "block";
    btn2.classList.remove("active");
    btn3.classList.add("active");
    btn2.setAttribute("aria-pressed", "false");
    btn3.setAttribute("aria-pressed", "true");
    if (lsEl) lsEl.style.display = "none";
    if (g3El) g3El.style.display = "flex";
    if (!scene3d) init3D();
    if (!scene3d) return;
    if (ground3dMode !== "box" && !groundTexBounds) {
      void switchGround3D(ground3dMode, true);
    }
    resize3D();
    update3DEntities();
    if (!center3DCameraOnRocket()) fit3DPath();
    request3DRender(2);
  } else {
    mapEl.style.display = "block";
    v3El.style.display = "none";
    btn2.classList.add("active");
    btn3.classList.remove("active");
    btn2.setAttribute("aria-pressed", "true");
    btn3.setAttribute("aria-pressed", "false");
    if (lsEl) lsEl.style.display = "flex";
    if (g3El) g3El.style.display = "none";
    if (animId3d !== null) { cancelAnimationFrame(animId3d); animId3d = null; }
    renderFramesRemaining3d = 0;
    setTimeout(() => { if (hostVisible) map.invalidateSize(); }, 50);
  }
}

function updateProviderAttribution() {
  const attribution = document.getElementById("providerAttribution");
  if (!attribution) return;
  const labels = {
    map: "© OpenStreetMap contributors · © CARTO · Elevation © Mapzen, OpenStreetMap, and others",
    terrain: "Map data © OpenStreetMap contributors · Map style © OpenTopoMap (CC-BY-SA) · Elevation © Mapzen, OpenStreetMap, and others",
    sat: "Tiles © Esri, Maxar, Earthstar Geographics and the GIS User Community · Elevation © Mapzen, OpenStreetMap, and others"
  };
  const label = labels[ground3dMode] || "";
  attribution.textContent = label;
  attribution.style.display = activeView === "3d" && label ? "block" : "none";
}

/* ── Altitude profile ────────────────────────────────────────────── */
function drawAltProfile() {
  const canvas = document.getElementById("altCanvas");
  const rect = canvas.parentElement.getBoundingClientRect();
  const W = rect.width, H = rect.height;
  if (W === 0 || H === 0) return;
  const dpr = window.devicePixelRatio || 1;
  canvas.width = W * dpr; canvas.height = H * dpr;
  canvas.style.width = W+"px"; canvas.style.height = H+"px";
  const ctx = canvas.getContext("2d");
  ctx.scale(dpr, dpr);
  ctx.clearRect(0,0,W,H);

  if (allAlt.length < 2) return;
  const n = allAlt.length;
  let minA=Infinity, maxA=-Infinity;
  for (let i=0;i<n;i++){minA=Math.min(minA,allAlt[i]);maxA=Math.max(maxA,allAlt[i]);}
  const range=Math.max(maxA-minA,1), pad=6;

  const accent = currentThemePalette?.accent_link;
  const rocket = currentThemePalette?.warning || currentThemePalette?.danger;
  const bgFill = colorWithAlpha(accent,0.15,currentThemeDark?"rgba(60,100,140,0.15)":"rgba(40,80,140,0.10)");
  const gradTop = colorWithAlpha(accent,0.45,currentThemeDark?"rgba(79,165,222,0.45)":"rgba(30,120,200,0.35)");
  const gradBot = colorWithAlpha(accent,0.05,currentThemeDark?"rgba(79,165,222,0.05)":"rgba(30,120,200,0.03)");
  const lineCol = accent || (currentThemeDark?"#4fa5de":"#1a73b8");
  const dotFill = rocket || (currentThemeDark?"#ff7a30":"#d05020");
  const dotStroke = currentThemePalette?.danger || (currentThemeDark?"#ffaa55":"#ff7040");
  const labelDim = currentThemePalette?.text_dim || (currentThemeDark?"#6a7a8a":"#6b7580");
  const labelBright = currentThemePalette?.text_primary || (currentThemeDark?"#e8ecf0":"#1a1a1a");

  ctx.beginPath(); ctx.moveTo(0,H);
  for (let i=0;i<n;i++){ctx.lineTo((i/(n-1))*W, H-pad-((allAlt[i]-minA)/range)*(H-pad*2));}
  ctx.lineTo(W,H); ctx.closePath();
  ctx.fillStyle=bgFill; ctx.fill();

  let aN=0;
  for(let i=0;i<n;i++){
    if(sampleIdx(allIdx,i)<trailLen)aN=i+1;
    else break;
  }
  if (aN>0) {
    ctx.beginPath(); ctx.moveTo(0,H);
    for(let i=0;i<aN;i++){ctx.lineTo((i/(n-1))*W, H-pad-((allAlt[i]-minA)/range)*(H-pad*2));}
    ctx.lineTo(((aN-1)/(n-1))*W, H); ctx.closePath();
    const g=ctx.createLinearGradient(0,0,0,H);
    g.addColorStop(0,gradTop);g.addColorStop(1,gradBot);
    ctx.fillStyle=g; ctx.fill();

    ctx.beginPath();
    for(let i=0;i<aN;i++){const x=(i/(n-1))*W,y=H-pad-((allAlt[i]-minA)/range)*(H-pad*2);i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);}
    ctx.strokeStyle=lineCol; ctx.lineWidth=2; ctx.stroke();

    const cx=((aN-1)/(n-1))*W, cy=H-pad-((allAlt[aN-1]-minA)/range)*(H-pad*2);
    ctx.beginPath(); ctx.arc(cx,cy,5,0,Math.PI*2);
    ctx.fillStyle=dotFill; ctx.fill();
    ctx.strokeStyle=dotStroke; ctx.lineWidth=1.5; ctx.stroke();
  }

  ctx.fillStyle=labelDim; ctx.font="9px monospace"; ctx.textAlign="left";
  ctx.fillText(fmtAlt(maxA),4,12); ctx.fillText(fmtAlt(minA),4,H-3);
  if(aN>0){ctx.fillStyle=labelBright;ctx.font="bold 10px monospace";ctx.textAlign="right";ctx.fillText("Alt: "+fmtAlt(allAlt[aN-1]),W-6,14);}
}

/* ── Update 2D entities ──────────────────────────────────────────── */
function updateEntities(tLen, exactPoint) {
  currentExactPoint = exactPoint || null;
  const vp=[], va=[];
  const total = totalRawSamples || allPos.length;
  for(let i=0;i<allPos.length;i++){
    const p=allPos[i];
    if(sampleIdx(allIdx,i) < tLen && validC(p.lat,p.lon)){vp.push(p);va.push(allAlt[i]);}
  }
  if(currentExactPoint && validC(currentExactPoint.lat,currentExactPoint.lon)){
    const last=vp.length?vp[vp.length-1]:null;
    if(!last || Math.abs(last.lat-currentExactPoint.lat)>1e-10 || Math.abs(last.lon-currentExactPoint.lon)>1e-10){
      vp.push({lat:currentExactPoint.lat,lon:currentExactPoint.lon});va.push(currentExactPoint.alt);
    }
  }

  if(pathDataDirty){
    const allValid=[];
    for(let i=0;i<allPos.length;i++){
      if(validC(allPos[i].lat,allPos[i].lon))allValid.push([allPos[i].lat,allPos[i].lon]);
    }
    pathLine.setLatLngs(allValid);
    pathDataDirty=false;
  }

  if(vp.length===0){
    trailLine.setLatLngs([]);launchMarker.setOpacity(0);rocketMarker.setOpacity(0);
    sendStats([],[],total);drawAltProfile();
    if(activeView==="3d")update3DEntities();
    return;
  }

  trailLine.setLatLngs(vp.map(p=>[p.lat,p.lon]));
  launchMarker.setLatLng([vp[0].lat,vp[0].lon]);launchMarker.setOpacity(1);
  const last=vp[vp.length-1];
  rocketMarker.setLatLng([last.lat,last.lon]);rocketMarker.setOpacity(1);

  if(following && activeView==="2d") map.panTo([last.lat,last.lon],{animate:true,duration:0.3});

  sendStats(vp,va,total);
  updateStatsOverlay(vp,va,total);
  drawAltProfile();
  if(activeView==="3d")update3DEntities();
}

function sendStats(vp,va,total){
  if(!bridge || typeof bridge.onStatsUpdated !== "function")return;
  if(vp.length===0){bridge.onStatsUpdated(0,0,0,-1,-1,0,total,0,0,0);return;}
  const f=vp[0],l=vp[vp.length-1],la=va[va.length-1];
  let pL=0;for(let i=1;i<vp.length;i++)pL+=hDist(vp[i-1].lat,vp[i-1].lon,vp[i].lat,vp[i].lon);
  bridge.onStatsUpdated(l.lat,l.lon,la,hDist(f.lat,f.lon,l.lat,l.lon),fBearing(f.lat,f.lon,l.lat,l.lon),pL,total,vp.length,f.lat,f.lon);
}

let lastStatsText = "";

function updateStatsOverlay(vp,va,total){
  const el=document.getElementById("statsOverlay");
  if(vp.length===0){el.style.display="none";lastStatsText="";return;}
  const f=vp[0],l=vp[vp.length-1],la=va[va.length-1];
  let pL=0;for(let i=1;i<vp.length;i++)pL+=hDist(vp[i-1].lat,vp[i-1].lon,vp[i].lat,vp[i].lon);
  const dist = hDist(f.lat,f.lon,l.lat,l.lon), bear = fBearing(f.lat,f.lon,l.lat,l.lon);
  const coords = l.lat.toFixed(6)+', '+l.lon.toFixed(6);
  const hasExactLivePath=Number.isFinite(cumulativeLivePathLength);
  const displayedPath=hasExactLivePath?cumulativeLivePathLength:pL;
  const pathPrefix=hasExactLivePath?'':'≈';
  const pointSummary=cumulativeLiveTotal
    ? vp.length+' shown · '+cumulativeLiveValid+' GPS / '+cumulativeLiveTotal+' samples'
    : vp.length+'/'+total+' pts';
  lastStatsText = 'Position: '+coords+'\nAlt: '+fmtAlt(la)+'\nRange: '+fmtDist(dist)+' '+bear.toFixed(0)+'°\nPath: '+pathPrefix+fmtDist(displayedPath)+' ('+pointSummary+')';
  el.innerHTML=
    '<div class="st">POSITION</div>'+
    '<div class="sv">'+coords+'</div>'+
    '<div class="sv">Alt '+fmtAlt(la)+'</div>'+
    '<div class="sd"><div class="st">RANGE</div><div class="sv">'+fmtDist(dist)+' · '+bear.toFixed(0)+'°</div></div>'+
    '<div class="sd"><div class="st">PATH</div><div class="sv">'+pathPrefix+fmtDist(displayedPath)+' · '+pointSummary+'</div></div>';
  el.style.display="block";
}

document.addEventListener("keydown", function(e) {
  if ((e.ctrlKey || e.metaKey) && e.key === "c" && !window.getSelection().toString() && lastStatsText) {
    navigator.clipboard.writeText(lastStatsText).catch(()=>{});
  }
});

document.addEventListener("visibilitychange", function(){
  if(document.hidden){
    if(animId3d!==null){cancelAnimationFrame(animId3d);animId3d=null;}
    renderFramesRemaining3d=0;
    return;
  }
  if(!hostVisible)return;
  if(activeView==="3d")request3DRender(2);
  else if(map)map.invalidateSize();
});

/* ── API from C++ ────────────────────────────────────────────────── */
function normalizePoint(raw, fallbackIndex) {
  if (!raw || typeof raw !== "object") return null;
  const lat = Number(raw.lat), lon = Number(raw.lon);
  const alt = Number(raw.alt), ts = Number(raw.ts);
  if (!validC(lat, lon) || !validAltitude(alt)
      || !Number.isFinite(ts) || Math.abs(ts) > MAX_ABS_TIMESTAMP) return null;
  const rawIndex = Number(raw.idx);
  const idx = Number.isSafeInteger(rawIndex) && rawIndex >= 0
    && rawIndex <= MAX_MAP_SAMPLE_COUNT ? rawIndex : fallbackIndex;
  return { lat, lon, alt, ts, idx };
}

function invalidateGroundData() {
  groundLoadGeneration++;
  groundTexBounds = null;
  groundElevGrid = null;
  path3dDataDirty = true;
}

window.addPoints = function(payload){
  let batch;
  try { batch=(typeof payload==="string")?JSON.parse(payload):payload; }
  catch (_) { return; }
  if (!batch || typeof batch !== "object") return;

  const cumulativeTotalText=String(batch.cumulativeTotal??"");
  const cumulativeValidText=String(batch.cumulativeValid??"");
  cumulativeLiveTotal=/^(0|[1-9][0-9]{0,19})$/.test(cumulativeTotalText)
    ? cumulativeTotalText : "";
  cumulativeLiveValid=/^(0|[1-9][0-9]{0,19})$/.test(cumulativeValidText)
    ? cumulativeValidText : "";
  const cumulativePath=Number(batch.cumulativePathLength);
  cumulativeLivePathLength=Number.isFinite(cumulativePath)
    && cumulativePath>=0 && cumulativePath<=MAX_MAP_DISTANCE ? cumulativePath : NaN;

  const replace=Boolean(batch.replace);
  const path=cappedRawPoints(batch.path,MAX_2D_POINTS);
  const path3d=cappedRawPoints(batch.path3d,MAX_3D_POINTS);
  if(replace){
    allPos=[];allAlt=[];allTs=[];allIdx=[];
    allPos3d=[];allAlt3d=[];allIdx3d=[];currentExactPoint=null;
  }

  const fallback2d=allPos.length;
  for(let i=0;i<path.length;i++){
    const r=normalizePoint(path[i],fallback2d+i);if(!r)continue;
    allPos.push({lat:r.lat,lon:r.lon});allAlt.push(r.alt);allTs.push(r.ts);allIdx.push(r.idx);
  }
  const fallback3d=allPos3d.length;
  for(let i=0;i<path3d.length;i++){
    const r=normalizePoint(path3d[i],fallback3d+i);if(!r)continue;
    allPos3d.push({lat:r.lat,lon:r.lon});allAlt3d.push(r.alt);allIdx3d.push(r.idx);
  }
  cap2DSeries();
  cap3DSeries();

  const declaredTotal=Number(batch.total);
  if(Number.isSafeInteger(declaredTotal) && declaredTotal >= 0
      && declaredTotal <= MAX_MAP_SAMPLE_COUNT){
    totalRawSamples=replace?declaredTotal:Math.max(totalRawSamples,declaredTotal);
  } else {
    totalRawSamples=Math.min(MAX_MAP_SAMPLE_COUNT,Math.max(totalRawSamples,allPos.length));
  }
  trailLen=totalRawSamples;
  pathDataDirty=true;
  path3dDataDirty=true;
  updateEntities(trailLen,null);
};
window.loadSession = function(payload){
  let s;
  try { s=(typeof payload==="string")?JSON.parse(payload):payload; }
  catch (_) { return; }
  if (!s || typeof s !== "object") return;
  cumulativeLiveTotal="";cumulativeLiveValid="";cumulativeLivePathLength=NaN;
  const sourcePath=Array.isArray(s)?s:(s.path||[]);
  const sourcePath3d=Array.isArray(s)?s:(s.path3d||sourcePath);
  const path=cappedRawPoints(sourcePath,MAX_2D_POINTS);
  const path3d=cappedRawPoints(sourcePath3d,MAX_3D_POINTS);
  const declaredTotal=Number(Array.isArray(s)?sourcePath.length:s.total);
  totalRawSamples=Number.isSafeInteger(declaredTotal) && declaredTotal >= 0
    && declaredTotal <= MAX_MAP_SAMPLE_COUNT
    ? declaredTotal : Math.min(sourcePath.length, MAX_MAP_SAMPLE_COUNT);
  allPos=[];allAlt=[];allTs=[];allIdx=[];
  allPos3d=[];allAlt3d=[];allIdx3d=[];currentExactPoint=null;
  for(let i=0;i<path.length;i++){
    const r=normalizePoint(path[i],i);if(!r)continue;
    allPos.push({lat:r.lat,lon:r.lon});allAlt.push(r.alt);allTs.push(r.ts);allIdx.push(r.idx);
  }
  for(let i=0;i<path3d.length;i++){
    const r=normalizePoint(path3d[i],i);if(!r)continue;
    allPos3d.push({lat:r.lat,lon:r.lon});allAlt3d.push(r.alt);allIdx3d.push(r.idx);
  }
  cap2DSeries();
  cap3DSeries();
  pathDataDirty=true;
  path3dDataDirty=true;
  invalidateGroundData();
  trailLen=totalRawSamples;
  updateEntities(trailLen,null);
  if(activeView==="3d"){
    if(!center3DCameraOnRocket())fit3DPath();
  } else {
    fitCamera();
  }
  if(activeView==="3d"&&ground3dMode!=="box")void switchGround3D(ground3dMode,true);
};
window.setTrailLength = function(nn,current){
  const requested=Number(nn);
  if(!Number.isFinite(requested))return;
  trailLen=Math.max(0,Math.min(Math.trunc(requested),totalRawSamples||allPos.length));
  updateEntities(trailLen,normalizePoint(current,Math.max(0,trailLen-1))||null);
};
window.fitCamera = function(){
  if(activeView==="3d"){fit3DPath();return;}
  const pts=[];
  for(let i=0;i<allPos.length;i++){if(validC(allPos[i].lat,allPos[i].lon))pts.push([allPos[i].lat,allPos[i].lon]);}
  if(pts.length===0)return;
  map.fitBounds(L.latLngBounds(pts).pad(0.15),{animate:true,duration:0.6});
};
window.centerOnCurrent = function(){
  if(activeView==="3d"){center3DCameraOnRocket();return;}
  if(currentExactPoint && validC(currentExactPoint.lat,currentExactPoint.lon)){
    map.setView([currentExactPoint.lat,currentExactPoint.lon],Math.max(map.getZoom(),16),{animate:true,duration:0.5});return;
  }
  for(let i=allPos.length-1;i>=0;i--){
    if(sampleIdx(allIdx,i)>=trailLen)continue;
    if(validC(allPos[i].lat,allPos[i].lon)){
      map.setView([allPos[i].lat,allPos[i].lon],Math.max(map.getZoom(),16),{animate:true,duration:0.5});return;
    }
  }
};
window.followRocket = function(en){
  setFollowing(en,false);
};
window.clearAll = function(){
  invalidateGroundData();
  allPos=[];allAlt=[];allTs=[];allIdx=[];trailLen=0;
  allPos3d=[];allAlt3d=[];allIdx3d=[];totalRawSamples=0;currentExactPoint=null;
  cumulativeLiveTotal="";cumulativeLiveValid="";cumulativeLivePathLength=NaN;
  pathDataDirty=true;path3dDataDirty=true;
  pathLine.setLatLngs([]);trailLine.setLatLngs([]);
  launchMarker.setOpacity(0);rocketMarker.setOpacity(0);
  document.getElementById("statsOverlay").style.display="none";
  drawAltProfile();
  if(scene3d)update3DEntities();
  if(activeView==="3d"&&ground3dMode!=="box")void switchGround3D("box",true);
  if(bridge && typeof bridge.onStatsUpdated === "function")bridge.onStatsUpdated(0,0,0,-1,-1,0,0,0,0,0);
};

window.setHostVisible = function(visible){
  hostVisible=Boolean(visible);
  if(!hostVisible){
    if(animId3d!==null){cancelAnimationFrame(animId3d);animId3d=null;}
    renderFramesRemaining3d=0;
    groundLoadGeneration++;
    return;
  }

  if(activeView==="2d"){
    setTimeout(()=>{if(hostVisible && map)map.invalidateSize();},0);
    return;
  }
  if(scene3d){
    resize3D();
    update3DEntities();
    if(ground3dMode!=="box" && (!groundTexBounds || !groundElevGrid)){
      void switchGround3D(ground3dMode,true);
    }
    request3DRender(2);
  }
};

window.setImperialUnits = function(enabled){
  const nextImperial=Boolean(enabled);
  if(imperialUnits===nextImperial)return;
  imperialUnits=nextImperial;
  // Reformat the current bounded display snapshots without changing raw SI
  // path geometry, sample selection, or source telemetry.
  updateEntities(trailLen,currentExactPoint);
};

/* ── Init ────────────────────────────────────────────────────────── */
/* ── Theme switching (received through the typed QWebChannel bridge) ── */
window.applyTheme = function(paletteJson) {
  const p = ((typeof paletteJson === "string") ? JSON.parse(paletteJson) : paletteJson) || {};
  const isDark = isColorDark(p.bg_base || "#1f1f1f");
  currentThemeDark = isDark;
  currentThemePalette = p;

  const rootStyle = document.documentElement.style;
  const setThemeVar = (name, value) => { if (value) rootStyle.setProperty(name, value); };
  setThemeVar("--bg", p.bg_base);
  setThemeVar("--bg-dark", p.bg_dark || p.bg_base);
  setThemeVar("--text-dim", p.text_dim);
  setThemeVar("--border", p.border_subtle);
  setThemeVar("--border-strong", p.border_light || p.accent_link || p.border_subtle);
  setThemeVar("--divider", p.border_subtle);
  setThemeVar("--accent", p.accent_link);
  setThemeVar("--focus", p.focus_ring || p.accent_link);
  setThemeVar("--text-primary", p.text_primary);
  setThemeVar("--text-muted", p.text_muted);
  setThemeVar("--control-bg", p.bg_panel || p.bg_dark || p.bg_base);
  setThemeVar("--control-hover", p.btn_hover || p.bg_base || p.bg_panel);
  setThemeVar("--active-bg", p.accent_link);
  setThemeVar("--active-text", higherContrastText(p.accent_link,p.text_primary,p.bg_base));
  setThemeVar("--alt-bg", p.bg_dark || p.bg_base);
  setThemeVar("--stats-bg", p.bg_panel || p.bg_dark || p.bg_base);
  setThemeVar("--subtle-line", p.border_subtle);
  setThemeVar("--path", colorWithAlpha(p.info || p.border_light,0.5,"rgba(60,100,140,0.5)"));
  setThemeVar("--trail", p.accent_link);
  setThemeVar("--rocket", p.warning || p.danger);
  setThemeVar("--rocket-border", p.danger || p.warning);
  setThemeVar("--launch", p.success);
  setThemeVar("--launch-border", p.success);

  if (map) switchLayer(activeLayer, true);

  if (scene3d) apply3DTheme();

  // Redraw altitude chart with new colors
  drawAltProfile();
};

function isColorDark(hex) {
  const c = hex.replace("#", "");
  const r = parseInt(c.substring(0, 2), 16);
  const g = parseInt(c.substring(2, 4), 16);
  const b = parseInt(c.substring(4, 6), 16);
  return (r * 0.299 + g * 0.587 + b * 0.114) < 128;
}

function init() {
  initMap();
  window.addEventListener("resize",()=>{drawAltProfile();if(activeView==="3d"&&renderer3d)resize3D();});

  document.getElementById("btnLocate").onclick = window.centerOnCurrent;
  document.getElementById("btnFit").onclick = window.fitCamera;
  document.getElementById("btnFollow").onclick = function(){
    setFollowing(!following,true);
  };
  document.getElementById("btnZoomIn").onclick = function(){
    if(activeView==="2d")map.zoomIn();
    else set3DCameraDistance(0.65);
  };
  document.getElementById("btnZoomOut").onclick = function(){
    if(activeView==="2d")map.zoomOut();
    else set3DCameraDistance(1.45);
  };
  document.getElementById("btn2D").onclick = function(){switchView("2d");};
  document.getElementById("btn3D").onclick = function(){switchView("3d");};
  document.getElementById("lsMap").onclick = function(){switchLayer("map");};
  document.getElementById("lsTerrain").onclick = function(){switchLayer("terrain");};
  document.getElementById("lsSat").onclick = function(){switchLayer("sat");};
  document.getElementById("g3dBox").onclick = function(){switchGround3D("box");};
  document.getElementById("g3dMap").onclick = function(){switchGround3D("map");};
  document.getElementById("g3dTerrain").onclick = function(){switchGround3D("terrain");};
  document.getElementById("g3dSat").onclick = function(){switchGround3D("sat");};

  if(typeof QWebChannel!=="undefined"){
    new QWebChannel(qt.webChannelTransport,function(ch){
      bridge=ch.objects.map3dBridge;
      if(!bridge)return;
      const connectSignal = (signal, handler) => {
        if (signal && typeof signal.connect === "function") signal.connect(handler);
      };
      connectSignal(bridge.themeChanged, theme => window.applyTheme(theme));
      connectSignal(bridge.unitSystemChanged,
        imperial => window.setImperialUnits(Boolean(imperial)));
      connectSignal(bridge.sessionLoaded, session => window.loadSession(session));
      connectSignal(bridge.trailLengthChanged,
        (length, currentPoint) => window.setTrailLength(length, currentPoint));
      connectSignal(bridge.liveBatchAdded, batch => window.addPoints(batch));
      connectSignal(bridge.hostVisibilityChanged,
        visible => window.setHostVisible(Boolean(visible)));
      connectSignal(bridge.clearRequested, () => window.clearAll());
      connectSignal(bridge.fitRequested, () => window.fitCamera());
      connectSignal(bridge.centerRequested, () => window.centerOnCurrent());
      connectSignal(bridge.followRequested, enabled => window.followRocket(Boolean(enabled)));
      if(typeof bridge.mapReady === "function")bridge.mapReady();
    });
  }
}

if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",init);
else init();
