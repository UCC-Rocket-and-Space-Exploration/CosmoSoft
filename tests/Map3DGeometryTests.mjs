// Run: node --test tests/Map3DGeometryTests.mjs
import test from "node:test";
import assert from "node:assert/strict";
globalThis.window = { addEventListener() {} };
globalThis.document = { readyState: "loading", addEventListener() {} };
const { createRocketModel, attitudeFromPayload } = await import("../assets/map/map3d.js");
const THREE = await import("../assets/map/vendor/three/build/three.module.js");

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
