var hdiffpatch = require("../index");
var crypto = require("crypto");
var assert = require("assert").strict;
var fs = require("fs");
var path = require("path");
var os = require("os");

console.log("Test 1: diff + patch = original (sync)...");
var oldData = crypto.randomBytes(40960);
var newData = Buffer.concat([
  Buffer.from("prefix_"),
  oldData.slice(0, 4096),
  Buffer.from("_middle_"),
  oldData.slice(4096),
  Buffer.from("_suffix")
]);

var diffResult = hdiffpatch.diff(oldData, newData);
console.log("  oldData size:", oldData.length);
console.log("  newData size:", newData.length);
console.log("  diff size:", diffResult.length);

var patchedData = hdiffpatch.patch(oldData, diffResult);
assert.deepStrictEqual(patchedData, newData);
console.log("  ✓ patch(old, diff(old, new)) === new");

console.log("\nTest 2: Same data diff...");
var sameData = Buffer.from("Hello World".repeat(100));
var sameDiff = hdiffpatch.diff(sameData, sameData);
console.log("  data size:", sameData.length, "diff size:", sameDiff.length);
assert(sameDiff.length < sameData.length);
var samePatch = hdiffpatch.patch(sameData, sameDiff);
assert.deepStrictEqual(samePatch, sameData);
console.log("  ✓ Same data works");

console.log("\nTest 3: Large data (100KB) sync...");
var largeOld = crypto.randomBytes(1024 * 100);
var largeNew = Buffer.concat([Buffer.from("header"), largeOld, Buffer.from("footer")]);
var largeDiff = hdiffpatch.diff(largeOld, largeNew);
var largePatched = hdiffpatch.patch(largeOld, largeDiff);
console.log("  largeOld size:", largeOld.length);
console.log("  largeNew size:", largeNew.length);
console.log("  largeDiff size:", largeDiff.length);
assert.deepStrictEqual(largePatched, largeNew);
console.log("  ✓ Large data sync works");

console.log("\nTest 4: TypedArray support...");
var uint8Old = new Uint8Array(oldData);
var uint8New = new Uint8Array(newData);
var uint8Diff = hdiffpatch.diff(uint8Old, uint8New);
var uint8Patched = hdiffpatch.patch(uint8Old, uint8Diff);
assert.deepStrictEqual(Buffer.from(uint8Patched), newData);
console.log("  ✓ Uint8Array works");

console.log("\nTest 5: Stream diff/patch (file paths)...");
var tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "hdiffpatch-"));
var oldPath = path.join(tempDir, "old.bin");
var newPath = path.join(tempDir, "new.bin");
var diffPath = path.join(tempDir, "diff.bin");
var outNewPath = path.join(tempDir, "out.bin");

fs.writeFileSync(oldPath, oldData);
fs.writeFileSync(newPath, newData);

var diffOutPath = hdiffpatch.diffStream(oldPath, newPath, diffPath);
assert.strictEqual(diffOutPath, diffPath);
var patchedOutPath = hdiffpatch.patchStream(oldPath, diffPath, outNewPath);
assert.strictEqual(patchedOutPath, outNewPath);

var outNewData = fs.readFileSync(outNewPath);
assert.deepStrictEqual(outNewData, newData);
console.log("  ✓ Stream diff/patch works");

fs.rmSync(tempDir, { recursive: true, force: true });
