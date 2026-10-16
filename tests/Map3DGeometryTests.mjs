// Run: node --test tests/Map3DGeometryTests.mjs
import test from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
globalThis.window = { addEventListener() {} };
globalThis.document = { readyState: "loading", addEventListener() {} };
const { createRocketModel, attitudeFromPayload } = await import("../assets/map/map3d.js");
const THREE = await import("../assets/map/vendor/three/build/three.module.js");

// Exercise the actual module's private scene state without exporting test hooks
// in the shipped application. Only DOM/Leaflet presentation is stubbed.
const mapUrl = new URL("../assets/map/map3d.js", import.meta.url);
const source = (await readFile(mapUrl, "utf8")).replaceAll(
  'from "./vendor/', `from "${new URL("./vendor/", mapUrl).href}`);
// Only replace the GPU renderer; camera math, OrbitControls and production
// event handlers run unchanged against a small DOM event target.
const probeSource = source.replace("new THREE.WebGLRenderer", "new ProbeRenderer");
const probeModule = await import(`data:text/javascript;base64,${Buffer.from(probeSource + `
class ProbeCanvas extends EventTarget {
  style={}; clientWidth=800; clientHeight=600;
  setPointerCapture() {} releasePointerCapture() {}
  getBoundingClientRect() { return {left:0,top:0,width:800,height:600}; }
}
class ProbeRenderer {
  domElement=new ProbeCanvas();
  setPixelRatio() {} setSize() {}
  getSize(size) { return size.set(800,600); }
}
export function initializeCameraProbe(followBeforeInit=false) {
  hostVisible=false; activeView="3d"; following=false;
  controls3d=camera3d=scene3d=null;
  allPos=[];allAlt=[];allPos3d=[];allAlt3d=[];
  currentRocketWorld3d=new THREE.Vector3();
  bridge=null;
  window.followRocket(followBeforeInit);
  init3D();
  return renderer3d.domElement;
}
export function cameraProbeState() {
  return {following,position:camera3d.position.clone(),target:controls3d.target.clone(),
    panEnabled:controls3d.enablePan};
}
export function advanceCameraProbeRocket(x,y,z) {
  currentRocketWorld3d.set(x,y,z);
  for(let i=0;i<150;i++) { update3DFollowCamera();controls3d.update(); }
}
export function settleCameraProbe() {
  for(let i=0;i<150;i++)controls3d.update();
}

let probeAltitude;
export function resetProbe(points) {
  hostVisible=false; activeView="3d"; following=false;
  groundRefAlt=0;groundElevGrid=null;ground3dMode="box";
  allPos=points.map(p=>normalizePoint({...p,ts:p.ts??0},p.idx));
  allAlt=points.map(p=>p.alt);allIdx=points.map(p=>p.idx);
  allTs=points.map(p=>p.ts||0);
  allPos3d=allPos.slice();allAlt3d=allAlt.slice();allIdx3d=allIdx.slice();
  totalRawSamples=10;trailLen=0;currentExactPoint=null;
  pathDataDirty=path3dDataDirty=true;
  scene3d=new THREE.Scene();
  ghostLine3d=createWideLine(0x888888,2,0.5,true);
  trailLine3d=createWideLine(0xff8800,2,1,true);
  rocketModel3d=new THREE.Object3D();launchSphere3d=new THREE.Object3D();
  pathLine=trailLine={setLatLngs(){}};
  launchMarker=rocketMarker={setLatLng(){},setOpacity(){}};
  bridge={onStatsUpdated(lat,lon,alt){probeAltitude=alt;}};
  drawAltProfile=()=>{};updateStatsOverlay=()=>{};
}
export function probeState() {
  return {altitude:probeAltitude,rocketAltitude:currentRocketWorld3d?.y/ALT_EXAGGERATION,
    rocketX:currentRocketWorld3d?.x,
    trailGeometry:trailLine3d.geometry,ghostGeometry:ghostLine3d.geometry,
    vertices:trailLine3d.geometry.attributes.instanceStart
      ? Array.from({length:trailLine3d.geometry.attributes.instanceStart.count},(_,i)=>[
        new THREE.Vector3().fromBufferAttribute(trailLine3d.geometry.attributes.instanceStart,i),
        new THREE.Vector3().fromBufferAttribute(trailLine3d.geometry.attributes.instanceEnd,i)]) : [],
    ghostVertices:ghostLine3d.geometry.attributes.instanceEnd
      ? Array.from({length:ghostLine3d.geometry.attributes.instanceEnd.count},(_,i)=>
        new THREE.Vector3().fromBufferAttribute(ghostLine3d.geometry.attributes.instanceEnd,i)) : [],
    quaternion:rocketModel3d.quaternion.toArray()};
}
`).toString("base64")}`);
const elements = new Map();
document.getElementById = id => {
  if (!elements.has(id)) elements.set(id, { textContent: "", style: {}, appendChild() {},
    clientWidth:800, clientHeight:600, classList:{toggle() {}}, setAttribute() {} });
  return elements.get(id);
};

test("the exact replay altitude survives repeated GPS coordinates and rewinding", () => {
  probeModule.resetProbe([
    {idx:0,lat:54,lon:-5,alt:100}, {idx:2,lat:54,lon:-5,alt:300}
  ]);
  for (const [length, idx, alt] of [[2,1,200],[3,2,300],[2,1,200],[1,0,100]]) {
    window.setTrailLength(length, {idx,lat:54,lon:-5,alt,ts:idx});
    assert.equal(probeModule.probeState().altitude, alt);
    assert.equal(probeModule.probeState().rocketAltitude, alt);
  }
});

test("3D rocket follows the interpolated path while telemetry keeps the measured fix", () => {
  probeModule.resetProbe([
    {idx:0,lat:54,lon:-5,alt:100,ts:0},
    {idx:4,lat:54,lon:-4.999,alt:200,ts:1}
  ]);
  window.setTrailLength(2, {idx:1,lat:54,lon:-5,displayLat:54,
    displayLon:-4.99975,alt:125,ts:0.25});
  const state=probeModule.probeState();
  assert.equal(state.altitude,125);
  assert.equal(state.rocketAltitude,125);
  assert.ok(state.rocketX>16 && state.rocketX<17);
  window.setTrailLength(1, {idx:0,lat:54,lon:-5,alt:100,ts:0});
  assert.equal(probeModule.probeState().rocketX,0);
});

test("the rendered trajectory rounds corners without overshooting altitude", () => {
  probeModule.resetProbe([
    {idx:0,lat:54,lon:-5,alt:100,ts:0},
    {idx:1,lat:54,lon:-4.999,alt:200,ts:1},
    {idx:2,lat:54,lon:-4.998,alt:210,ts:2}
  ]);
  window.setTrailLength(3,{idx:2,lat:54,lon:-4.998,alt:210,ts:2});
  const {vertices}=probeModule.probeState();
  assert.ok(vertices.length>2);
  for(const pair of vertices) for(const p of pair) {
    assert.ok(p.y>=300 && p.y<=630);
    assert.ok(Number.isFinite(p.x+p.y+p.z));
  }
  // Curved mid-interval altitude differs from a straight chord's 150 m.
  assert.ok(vertices.some(pair=>pair.some(p=>p.x>32 && p.x<33 && p.y>450)));
  assert.equal(probeModule.probeState().rocketAltitude,210);
});

test("missing-data segments never acquire a connecting 3D line", () => {
  probeModule.resetProbe([
    {idx:0,lat:54,lon:-5,alt:100,ts:0,segment:0},
    {idx:1,lat:54,lon:-4.999,alt:200,ts:1,segment:0},
    {idx:5,lat:54,lon:-4.98,alt:150,ts:10,segment:1},
    {idx:6,lat:54,lon:-4.979,alt:100,ts:11,segment:1}
  ]);
  window.setTrailLength(7,{idx:6,lat:54,lon:-4.979,alt:100,ts:11,segment:1});
  for(const [a,b] of probeModule.probeState().vertices)
    assert.ok(a.distanceTo(b)<400);
});

test("an exact seek omitted by decimation also lies on the full displayed curve", () => {
  probeModule.resetProbe([
    {idx:0,lat:54,lon:-5,alt:100,ts:0},
    {idx:2,lat:54,lon:-4.999,alt:200,ts:1},
    {idx:4,lat:54,lon:-4.998,alt:210,ts:2}
  ]);
  window.setTrailLength(2,{idx:1,lat:54,lon:-4.9995,alt:150,ts:0.5});
  const state=probeModule.probeState();
  assert.ok(state.ghostVertices.some(p=>Math.abs(p.y-450)<0.001
    && Math.abs(p.x-state.rocketX)<0.001));
  assert.ok(state.vertices.at(-1)[1].distanceTo(new THREE.Vector3(state.rocketX,450,0))<0.001);
});

test("attitude-only updates retain trail and full-path geometries", () => {
  probeModule.resetProbe([
    {idx:0,lat:54,lon:-5,alt:100}, {idx:1,lat:54,lon:-5,alt:200}
  ]);
  const point={idx:1,lat:54,lon:-5,alt:200,ts:1};
  window.setTrailLength(2,{...point,attitude:[0,0,0,1]});
  const before=probeModule.probeState();
  window.setTrailLength(2,{...point,attitude:[Math.SQRT1_2,0,0,Math.SQRT1_2]});
  const after=probeModule.probeState();
  assert.equal(after.trailGeometry,before.trailGeometry);
  assert.equal(after.ghostGeometry,before.ghostGeometry);
  assert.notDeepEqual(after.quaternion,before.quaternion);
  window.addPoints({total:3,path:[],path3d:[],attitude:[0,0,0,1]});
  assert.equal(probeModule.probeState().trailGeometry,before.trailGeometry);
  assert.equal(probeModule.probeState().ghostGeometry,before.ghostGeometry);
  window.setTrailLength(2,{...point,alt:210});
  assert.equal(probeModule.probeState().rocketAltitude,210);
});

test("the rocket has a slender nose, swept fins and orange on every surface", () => {
  const orange = new THREE.Color(1, 0.35, 0);
  const rocket = createRocketModel(orange);
  const bounds = new THREE.Box3().setFromObject(rocket).getSize(new THREE.Vector3());
  assert.ok(bounds.y > bounds.x * 1.8);
  let surfaces = 0;
  rocket.traverse(part => {
    if (!part.isMesh) return;
    ++surfaces;
    assert.ok(part.material.color.equals(orange));
    assert.ok(part.geometry.attributes.position.array.every(Number.isFinite));
  });
  assert.equal(surfaces, 5);
});

test("map attitude payload keeps Qt XYZW order and rejects missing angles", () => {
  const q = attitudeFromPayload([Math.SQRT1_2, 0, 0, Math.SQRT1_2]);
  assert.ok(q);
  const direction = new THREE.Vector3(0, 1, 0).applyQuaternion(q);
  assert.ok(Math.abs(direction.z - 1) < 1e-6);
  assert.ok(Math.abs(direction.y) < 1e-6);
  assert.equal(attitudeFromPayload([]), null);
  assert.equal(attitudeFromPayload([0, 0, 0, 0]), null);
  assert.equal(attitudeFromPayload([null, 0, 0, 1]), null);
  assert.equal(attitudeFromPayload([NaN, 0, 0, 1]), null);
});


test("orbit and zoom preserve camera follow and the chosen viewing offset", () => {
  const canvas=probeModule.initializeCameraProbe();
  window.followRocket(true);
  const before=probeModule.cameraProbeState();
  const pointer=(type,x,y)=>{
    const event=new Event(type);
    Object.assign(event,{pointerId:1,pointerType:"mouse",button:0,clientX:x,clientY:y});
    canvas.dispatchEvent(event);
  };
  pointer("pointerdown",200,200);
  pointer("pointermove",320,240);
  pointer("pointerup",320,240);
  probeModule.settleCameraProbe();
  const orbited=probeModule.cameraProbeState();
  assert.equal(orbited.following,true);
  assert.equal(orbited.panEnabled,false);
  assert.ok(orbited.position.distanceTo(before.position)>1);
  const offset=orbited.position.clone().sub(orbited.target);
  probeModule.advanceCameraProbeRocket(12,45,-8);
  const advanced=probeModule.cameraProbeState();
  assert.ok(advanced.target.distanceTo(new THREE.Vector3(12,45,-8))<0.02);
  assert.ok(advanced.position.clone().sub(advanced.target).distanceTo(offset)<0.001);
  const wheel=new Event("wheel",{cancelable:true});
  Object.assign(wheel,{deltaY:100,deltaMode:0,clientX:400,clientY:300});
  canvas.dispatchEvent(wheel);
  probeModule.settleCameraProbe();
  const zoomed=probeModule.cameraProbeState();
  assert.equal(zoomed.following,true);
  assert.ok(Math.abs(zoomed.position.distanceTo(zoomed.target)-offset.length())>1);
  window.followRocket(false);
  const released=probeModule.cameraProbeState();
  assert.equal(released.panEnabled,true);
  probeModule.advanceCameraProbeRocket(100,200,300);
  assert.ok(probeModule.cameraProbeState().target.equals(released.target));
  probeModule.initializeCameraProbe(true);
  assert.equal(probeModule.cameraProbeState().panEnabled,false);
});
