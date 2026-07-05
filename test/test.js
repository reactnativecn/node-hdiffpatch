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

console.log("\nTest 5: diffWithCovers emits patchable hdiff...");
var coverOld = Buffer.from("abcXYZdef");
var coverNew = Buffer.from("abc123def");
var coverResult = hdiffpatch.diffWithCovers(coverOld, coverNew, [
  { oldPos: "0", newPos: "0", len: "3" },
  { oldPos: "6", newPos: "6", len: "3" },
]);
assert(Buffer.isBuffer(coverResult.diff));
assert.strictEqual(coverResult.usedCovers, true);
assert.strictEqual(coverResult.requestedCoverCount, 2);
assert(Number.isInteger(coverResult.nativeCoverCapacity));
assert(coverResult.nativeCoverCapacity >= 0);
assert.strictEqual(coverResult.finalCoverCount, 2);
assert.strictEqual(coverResult.coverMode, "replace");
assert.deepStrictEqual(hdiffpatch.patch(coverOld, coverResult.diff), coverNew);
var mergeCoverResult = hdiffpatch.diffWithCovers(coverOld, coverNew, [
  { oldPos: "0", newPos: "0", len: "3" },
  { oldPos: "6", newPos: "6", len: "3" },
], { mode: "merge", debugCovers: true });
assert(Buffer.isBuffer(mergeCoverResult.diff));
assert.strictEqual(mergeCoverResult.usedCovers, true);
assert.strictEqual(mergeCoverResult.requestedCoverCount, 2);
assert.strictEqual(mergeCoverResult.coverMode, "merge");
assert(Number.isInteger(mergeCoverResult.finalCoverCount));
assert(mergeCoverResult.finalCoverCount >= mergeCoverResult.nativeCoverCapacity);
assert(Array.isArray(mergeCoverResult.nativeCovers));
assert(Array.isArray(mergeCoverResult.finalCovers));
assert.strictEqual(mergeCoverResult.nativeCovers.length, mergeCoverResult.nativeCoverCapacity);
assert.strictEqual(mergeCoverResult.finalCovers.length, mergeCoverResult.finalCoverCount);
assert.deepStrictEqual(hdiffpatch.patch(coverOld, mergeCoverResult.diff), coverNew);
var coalescedCoverResult = hdiffpatch.diffWithCovers(coverOld, coverNew, [], {
  mode: "native-coalesce",
  debugCovers: true,
});
assert(Buffer.isBuffer(coalescedCoverResult.diff));
assert.strictEqual(coalescedCoverResult.usedCovers, true);
assert.strictEqual(coalescedCoverResult.requestedCoverCount, 0);
assert.strictEqual(coalescedCoverResult.coverMode, "native-coalesce");
assert(Array.isArray(coalescedCoverResult.nativeCovers));
assert(Array.isArray(coalescedCoverResult.finalCovers));
assert(coalescedCoverResult.finalCoverCount <= coalescedCoverResult.nativeCoverCapacity);
assert.deepStrictEqual(hdiffpatch.patch(coverOld, coalescedCoverResult.diff), coverNew);
assert.throws(
  () => hdiffpatch.diffWithCovers(coverOld, coverNew, [
    { oldPos: "99", newPos: "0", len: "1" },
  ]),
  /old range is out of bounds/
);
assert.throws(
  () => hdiffpatch.diffWithCovers(coverOld, coverNew, [], { mode: "invalid" }),
  /Invalid options.mode/
);
console.log("  ✓ diffWithCovers patch(old, diff) === new");

console.log("\nTest 6: Single-compressed patchSingleStream (file paths)...");
var tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "hdiffpatch-"));
var oldPath = path.join(tempDir, "old.bin");
var newPath = path.join(tempDir, "new.bin");
var diffPath = path.join(tempDir, "diff.bin");
var outNewPath = path.join(tempDir, "out.bin");
var singleDiffPath = path.join(tempDir, "single.diff");
var singleOutNewPath = path.join(tempDir, "single-out.bin");

fs.writeFileSync(oldPath, oldData);
fs.writeFileSync(newPath, newData);
fs.writeFileSync(singleDiffPath, diffResult);

var singlePatchedOutPath = hdiffpatch.patchSingleStream(oldPath, singleDiffPath, singleOutNewPath);
assert.strictEqual(singlePatchedOutPath, singleOutNewPath);

var singleOutNewData = fs.readFileSync(singleOutNewPath);
assert.deepStrictEqual(singleOutNewData, newData);
console.log("  ✓ patchSingleStream applies diff(old, new) output");

console.log("\nTest 7: Stream diff/patch (file paths)...");

var diffOutPath = hdiffpatch.diffStream(oldPath, newPath, diffPath);
assert.strictEqual(diffOutPath, diffPath);
var patchedOutPath = hdiffpatch.patchStream(oldPath, diffPath, outNewPath);
assert.strictEqual(patchedOutPath, outNewPath);

var outNewData = fs.readFileSync(outNewPath);
assert.deepStrictEqual(outNewData, newData);
console.log("  ✓ Stream diff/patch works");

// ---- 异步与 CLI 测试 ----
var util = require("util");
var execFile = util.promisify(require("child_process").execFile);
var diffAsync = util.promisify(hdiffpatch.diff);
var patchAsync = util.promisify(hdiffpatch.patch);
var diffStreamAsync = util.promisify(hdiffpatch.diffStream);
var patchStreamAsync = util.promisify(hdiffpatch.patchStream);
var patchSingleStreamAsync = util.promisify(hdiffpatch.patchSingleStream);

async function runAsyncTests() {
  console.log("\nTest 8: Async diff/patch callbacks...");
  var asyncDiff = await diffAsync(oldData, newData);
  assert(Buffer.isBuffer(asyncDiff));
  assert.deepStrictEqual(asyncDiff, diffResult);
  var asyncPatched = await patchAsync(oldData, asyncDiff);
  assert.deepStrictEqual(asyncPatched, newData);
  console.log("  ✓ Async diff/patch works");

  console.log("\nTest 9: Async error propagation...");
  var corruptDiff = Buffer.from("this is definitely not a diff");
  await assert.rejects(() => patchAsync(oldData, corruptDiff));
  assert.throws(() => hdiffpatch.patch(oldData, corruptDiff));
  console.log("  ✓ Corrupt diff rejects in both sync and async modes");

  console.log("\nTest 10: Async stream diff/patch callbacks...");
  var asyncDiffPath = path.join(tempDir, "async.diff");
  var asyncOutPath = path.join(tempDir, "async-out.bin");
  var asyncSingleOutPath = path.join(tempDir, "async-single-out.bin");
  assert.strictEqual(await diffStreamAsync(oldPath, newPath, asyncDiffPath), asyncDiffPath);
  assert.strictEqual(await patchStreamAsync(oldPath, asyncDiffPath, asyncOutPath), asyncOutPath);
  assert.deepStrictEqual(fs.readFileSync(asyncOutPath), newData);
  assert.strictEqual(
    await patchSingleStreamAsync(oldPath, singleDiffPath, asyncSingleOutPath),
    asyncSingleOutPath
  );
  assert.deepStrictEqual(fs.readFileSync(asyncSingleOutPath), newData);
  await assert.rejects(
    () => patchStreamAsync(oldPath, path.join(tempDir, "no-such.diff"), asyncOutPath)
  );
  console.log("  ✓ Async stream diff/patch works");

  console.log("\nTest 11: CLI auto-detects both diff formats...");
  var cliBin = path.join(__dirname, "..", "bin", "hdiffpatch.js");
  var cliDiffPath = path.join(tempDir, "cli.diff");
  var cliOutPath = path.join(tempDir, "cli-out.bin");
  var cliSingleOutPath = path.join(tempDir, "cli-single-out.bin");
  await execFile(process.execPath, [cliBin, "diff", oldPath, newPath, cliDiffPath]);
  await execFile(process.execPath, [cliBin, "patch", oldPath, cliDiffPath, cliOutPath]);
  assert.deepStrictEqual(fs.readFileSync(cliOutPath), newData);
  await execFile(process.execPath, [cliBin, "patch", oldPath, singleDiffPath, cliSingleOutPath]);
  assert.deepStrictEqual(fs.readFileSync(cliSingleOutPath), newData);
  await assert.rejects(
    () => execFile(process.execPath, [cliBin, "patch", oldPath, oldPath, cliOutPath])
  );
  console.log("  ✓ CLI patch handles stream, single-compressed, and invalid inputs");
}

runAsyncTests()
  .then(() => {
    fs.rmSync(tempDir, { recursive: true, force: true });
    console.log("\nAll tests passed.");
  })
  .catch((err) => {
    fs.rmSync(tempDir, { recursive: true, force: true });
    console.error(err);
    process.exit(1);
  });
