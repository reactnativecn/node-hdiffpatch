var hdiffpatch = require("../index");
var crypto = require("crypto");
var assert = require("assert").strict;
var fs = require("fs");
var path = require("path");
var os = require("os");

function deterministicBytes(size, seed) {
  var out = Buffer.allocUnsafe(size);
  var x = seed >>> 0;
  for (var i = 0; i < out.length; ++i) {
    x ^= x << 13;
    x ^= x >>> 17;
    x ^= x << 5;
    out[i] = x & 0xff;
  }
  return out;
}

function assertHeaderPrefix(diffData, prefix) {
  var expected = Buffer.from(prefix, "latin1");
  assert.deepStrictEqual(diffData.subarray(0, expected.length), expected);
}

assert.deepStrictEqual(hdiffpatch.capabilities, {
  diffStreamVerifiesOutput: true,
  diffSingleStreamVerifiesOutput: true,
  diffWindowVerifiesOutput: true,
  maxCompressionThreads: 2
});
assert(Object.isFrozen(hdiffpatch.capabilities));

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

console.log("\nTest 5: raw single-format diffs clear compressType...");
var tinyOld = deterministicBytes(280 * 1024, 0x12345678);
var tinyNew = Buffer.from(tinyOld);
for (var i = 0; i < 17; ++i) {
  tinyNew[Math.floor(tinyNew.length / 2) + i] ^= i + 1;
}
var tinyDiff = hdiffpatch.diff(tinyOld, tinyNew);
assertHeaderPrefix(tinyDiff, "HDIFFSF20&\x00");
assert.deepStrictEqual(hdiffpatch.patch(tinyOld, tinyDiff), tinyNew);

var tinyDir = fs.mkdtempSync(path.join(os.tmpdir(), "hdiffpatch-raw-"));
var tinyOldPath = path.join(tinyDir, "old.bin");
var tinyNewPath = path.join(tinyDir, "new.bin");
fs.writeFileSync(tinyOldPath, tinyOld);
fs.writeFileSync(tinyNewPath, tinyNew);

function assertRawLabelClearedForFileDiff(diffFn, name) {
  var tinyDiffPath = path.join(tinyDir, name + ".diff");
  var tinyOutPath = path.join(tinyDir, name + "-out.bin");
  diffFn(tinyOldPath, tinyNewPath, tinyDiffPath);
  var tinyFileDiff = fs.readFileSync(tinyDiffPath);
  assertHeaderPrefix(tinyFileDiff, "HDIFFSF20&\x00");
  assert.deepStrictEqual(hdiffpatch.patch(tinyOld, tinyFileDiff), tinyNew);
  hdiffpatch.patchSingleStream(tinyOldPath, tinyDiffPath, tinyOutPath);
  assert.deepStrictEqual(fs.readFileSync(tinyOutPath), tinyNew);
}

assertRawLabelClearedForFileDiff(
  (oldPath, newPath, diffPath) =>
    hdiffpatch.diffWindow(oldPath, newPath, diffPath, 8 * 1024 * 1024),
  "window"
);
assertRawLabelClearedForFileDiff(hdiffpatch.diffSingleStream, "single");
fs.rmSync(tinyDir, { recursive: true, force: true });
console.log("  ✓ diff(), diffWindow(), and diffSingleStream() clear raw payload labels");

console.log("\nTest 5a: compressed single-format diffs keep compressType...");
var compressibleOld = deterministicBytes(280 * 1024, 0x9abcdef0);
var repeated = Buffer.alloc(64 * 1024, "A");
var compressibleNew = Buffer.concat([
  compressibleOld.subarray(0, 128 * 1024),
  repeated,
  compressibleOld.subarray(128 * 1024)
]);
var compressibleDiff = hdiffpatch.diff(compressibleOld, compressibleNew);
assertHeaderPrefix(compressibleDiff, "HDIFFSF20&lzma2\x00");
assert.deepStrictEqual(hdiffpatch.patch(compressibleOld, compressibleDiff), compressibleNew);
console.log("  ✓ lzma2 label remains when compression is used");

console.log("\nTest 5aa: optional LZMA2 compression threads...");
var mtOld = deterministicBytes(2 * 1024 * 1024, 0x2468ace0);
var mtNew = Buffer.from(mtOld);
for (var mtIndex = 0; mtIndex < mtNew.length; mtIndex += 4096) {
  mtNew[mtIndex] ^= (mtIndex >>> 12) & 0xff;
}
var mtDiffSingle = hdiffpatch.diff(mtOld, mtNew, { compressionThreads: 1 });
var mtDiffDual = hdiffpatch.diff(mtOld, mtNew, { compressionThreads: 2 });
assert.deepStrictEqual(hdiffpatch.patch(mtOld, mtDiffDual), mtNew);
assert.deepStrictEqual(mtDiffDual, mtDiffSingle);
assert.throws(() => hdiffpatch.diff(mtOld, mtNew, { compressionThreads: 0 }));
assert.throws(() => hdiffpatch.diff(mtOld, mtNew, { compressionThreads: 3 }));
assert.throws(() => hdiffpatch.diff(mtOld, mtNew, { compressionThreads: 1.5 }));
console.log("  ✓ 1/2 threads are deterministic; invalid counts are rejected");

console.log("\nTest 5b: diffSingleStream (single format, low-memory generation)...");
var ssDir = fs.mkdtempSync(path.join(os.tmpdir(), "hdiffpatch-ss-"));
var ssOldPath = path.join(ssDir, "old.bin");
var ssNewPath = path.join(ssDir, "new.bin");
var ssDiffPath = path.join(ssDir, "diff.bin");
var ssOutPath = path.join(ssDir, "out.bin");
fs.writeFileSync(ssOldPath, oldData);
fs.writeFileSync(ssNewPath, newData);
var ssDiffOut = hdiffpatch.diffSingleStream(ssOldPath, ssNewPath, ssDiffPath);
assert.strictEqual(ssDiffOut, ssDiffPath);
var ssDiffData = fs.readFileSync(ssDiffPath);
// 产物是 single 格式:内存版 patch() 能直接应用(与 diff() 产物同规范)
assert.deepStrictEqual(hdiffpatch.patch(oldData, ssDiffData), newData);
var ssMtDiffPath = path.join(ssDir, "diff-mt.bin");
hdiffpatch.diffSingleStream(ssOldPath, ssNewPath, ssMtDiffPath, {
  compressionThreads: 2
});
assert.deepStrictEqual(hdiffpatch.patch(oldData, fs.readFileSync(ssMtDiffPath)), newData);
// 文件版 patchSingleStream 也能应用
hdiffpatch.patchSingleStream(ssOldPath, ssDiffPath, ssOutPath);
assert.deepStrictEqual(fs.readFileSync(ssOutPath), newData);
console.log("  ✓ diffSingleStream output applies via patch() and patchSingleStream()");

console.log("\nTest 5c: diffWindow (single format, window/fast-match generation)...");
var winDiffPath = path.join(ssDir, "win.diff");
var winOutPath = path.join(ssDir, "win-out.bin");
var winDiffOut = hdiffpatch.diffWindow(ssOldPath, ssNewPath, winDiffPath);
assert.strictEqual(winDiffOut, winDiffPath);
var winDiffData = fs.readFileSync(winDiffPath);
// 产物是标准 single 格式(HDIFFSF20 开头),与 diff() 产物同规范
assert.strictEqual(winDiffData.slice(0, 9).toString("latin1"), "HDIFFSF20");
assert.deepStrictEqual(hdiffpatch.patch(oldData, winDiffData), newData);
hdiffpatch.patchSingleStream(ssOldPath, winDiffPath, winOutPath);
assert.deepStrictEqual(fs.readFileSync(winOutPath), newData);
// 匹配质量应接近内存版:不应显著差于 diff() 产物(经验上限 1.5x)
assert(
  winDiffData.length <= Math.max(diffResult.length * 1.5, diffResult.length + 256),
  "diffWindow size " + winDiffData.length + " vs diff " + diffResult.length
);
console.log(
  "  ✓ diffWindow output (" + winDiffData.length + "B vs diff " + diffResult.length +
  "B) applies via patch() and patchSingleStream()"
);
// 显式 windowSize(8MB)同步调用
var winDiff8Path = path.join(ssDir, "win8.diff");
hdiffpatch.diffWindow(ssOldPath, ssNewPath, winDiff8Path, 8 * 1024 * 1024);
var winDiff8Data = fs.readFileSync(winDiff8Path);
assert.strictEqual(winDiff8Data.slice(0, 9).toString("latin1"), "HDIFFSF20");
assert.deepStrictEqual(hdiffpatch.patch(oldData, winDiff8Data), newData);
assert.throws(() => hdiffpatch.diffWindow(ssOldPath, ssNewPath, winDiff8Path, -1));
var winDiffMtPath = path.join(ssDir, "win-mt.diff");
hdiffpatch.diffWindow(ssOldPath, ssNewPath, winDiffMtPath, {
  windowSize: 8 * 1024 * 1024,
  compressionThreads: 2
});
assert.deepStrictEqual(hdiffpatch.patch(oldData, fs.readFileSync(winDiffMtPath)), newData);
console.log("  ✓ diffWindow with explicit windowSize works, invalid size throws");

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
var mtStreamDiffPath = path.join(tempDir, "stream-mt.diff");
hdiffpatch.diffStream(oldPath, newPath, mtStreamDiffPath, { compressionThreads: 2 });
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
var diffSingleStreamAsync = util.promisify(hdiffpatch.diffSingleStream);
var patchSingleStreamAsync = util.promisify(hdiffpatch.patchSingleStream);
var diffWindowAsync = util.promisify(hdiffpatch.diffWindow);

async function runAsyncTests() {
  console.log("\nTest 8: Async diff/patch callbacks...");
  var asyncDiff = await diffAsync(oldData, newData);
  assert(Buffer.isBuffer(asyncDiff));
  assert.deepStrictEqual(asyncDiff, diffResult);
  var asyncPatched = await patchAsync(oldData, asyncDiff);
  assert.deepStrictEqual(asyncPatched, newData);
  var asyncMtDiff = await diffAsync(oldData, newData, { compressionThreads: 2 });
  assert.deepStrictEqual(await patchAsync(oldData, asyncMtDiff), newData);
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
  var asyncMtDiffPath = path.join(tempDir, "async-mt.diff");
  assert.strictEqual(
    await diffStreamAsync(oldPath, newPath, asyncMtDiffPath, { compressionThreads: 2 }),
    asyncMtDiffPath
  );
  var asyncMtSinglePath = path.join(tempDir, "async-mt-single.diff");
  assert.strictEqual(
    await diffSingleStreamAsync(
      oldPath,
      newPath,
      asyncMtSinglePath,
      { compressionThreads: 2 }
    ),
    asyncMtSinglePath
  );
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
  var asyncWinDiffPath = path.join(tempDir, "async-win.diff");
  assert.strictEqual(await diffWindowAsync(oldPath, newPath, asyncWinDiffPath), asyncWinDiffPath);
  assert.deepStrictEqual(hdiffpatch.patch(oldData, fs.readFileSync(asyncWinDiffPath)), newData);
  // 带 windowSize 的异步形态:(old, new, out, windowSize, cb)
  var asyncWin8Path = path.join(tempDir, "async-win8.diff");
  await new Promise((resolve, reject) => {
    hdiffpatch.diffWindow(oldPath, newPath, asyncWin8Path, 4 * 1024 * 1024, (err, out) =>
      err ? reject(err) : resolve(out)
    );
  });
  assert.deepStrictEqual(hdiffpatch.patch(oldData, fs.readFileSync(asyncWin8Path)), newData);
  await assert.rejects(
    () => diffWindowAsync(path.join(tempDir, "no-such.bin"), newPath, asyncWinDiffPath)
  );
  console.log("  ✓ Async stream diff/patch works (incl. diffWindow)");

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

  console.log("\nTest 12: transactional file outputs...");
  function assertNoTempFiles(dir) {
    var leftovers = fs.readdirSync(dir).filter(function (name) {
      return name.indexOf(".hdptmp") !== -1;
    });
    assert.deepStrictEqual(leftovers, [], "temp files left behind: " + leftovers);
  }
  // (a) in-place patch:输出路径就是旧文件,成功后旧文件被原子替换为新文件
  var inplacePath = path.join(tempDir, "inplace.bin");
  fs.writeFileSync(inplacePath, oldData);
  hdiffpatch.patchStream(inplacePath, diffPath, inplacePath);
  assert.deepStrictEqual(fs.readFileSync(inplacePath), newData);
  fs.writeFileSync(inplacePath, oldData);
  hdiffpatch.patchSingleStream(inplacePath, singleDiffPath, inplacePath);
  assert.deepStrictEqual(fs.readFileSync(inplacePath), newData);
  console.log("  ✓ In-place patch (out === old) replaces the old file atomically");
  // (b) diff 输出覆盖自身输入也安全:验证完成后才替换
  var selfDiffPath = path.join(tempDir, "self.bin");
  fs.writeFileSync(selfDiffPath, oldData);
  hdiffpatch.diffSingleStream(selfDiffPath, newPath, selfDiffPath);
  assert.deepStrictEqual(hdiffpatch.patch(oldData, fs.readFileSync(selfDiffPath)), newData);
  console.log("  ✓ diffSingleStream(out === old) generates a valid diff");
  // (c) 失败时保留既有输出文件,不残留临时文件
  var preciousPath = path.join(tempDir, "precious.bin");
  var precious = Buffer.from("do not clobber me");
  fs.writeFileSync(preciousPath, precious);
  // singleDiffPath 是 HDIFFSF20 格式,patchStream 期待 HDIFF13,必然失败
  assert.throws(function () {
    hdiffpatch.patchStream(oldPath, singleDiffPath, preciousPath);
  });
  assert.deepStrictEqual(fs.readFileSync(preciousPath), precious);
  assert.throws(function () {
    hdiffpatch.patchSingleStream(oldPath, diffPath, preciousPath);
  });
  assert.deepStrictEqual(fs.readFileSync(preciousPath), precious);
  assert.throws(function () {
    hdiffpatch.diffStream(oldPath, path.join(tempDir, "no-such.bin"), preciousPath);
  });
  assert.deepStrictEqual(fs.readFileSync(preciousPath), precious);
  assertNoTempFiles(tempDir);
  console.log("  ✓ Failed diff/patch preserves existing output and leaves no temp files");

  console.log("\nTest 13: patch resource limits...");
  assert.throws(function () {
    hdiffpatch.patch(oldData, diffResult, { maxOutputBytes: 16 });
  }, /maxOutputBytes/);
  // 小 diff 的实际 stepMemSize 只有几个字节,上限取 1 才能稳定触发
  assert.throws(function () {
    hdiffpatch.patch(oldData, diffResult, { maxWorkingMemoryBytes: 1 });
  }, /maxWorkingMemoryBytes/);
  assert.deepStrictEqual(
    hdiffpatch.patch(oldData, diffResult, {
      maxOutputBytes: newData.length,
      maxWorkingMemoryBytes: 1024 * 1024
    }),
    newData
  );
  await assert.rejects(
    function () { return patchAsync(oldData, diffResult, { maxOutputBytes: 16 }); },
    /maxOutputBytes/
  );
  var limitOutPath = path.join(tempDir, "limit-out.bin");
  assert.throws(function () {
    hdiffpatch.patchStream(oldPath, diffPath, limitOutPath, { maxOutputBytes: 16 });
  }, /maxOutputBytes/);
  assert(!fs.existsSync(limitOutPath));
  assert.throws(function () {
    hdiffpatch.patchSingleStream(oldPath, singleDiffPath, limitOutPath, { maxOutputBytes: 16 });
  }, /maxOutputBytes/);
  assert(!fs.existsSync(limitOutPath));
  assert.strictEqual(
    hdiffpatch.patchStream(oldPath, diffPath, limitOutPath, {
      maxOutputBytes: newData.length
    }),
    limitOutPath
  );
  assert.deepStrictEqual(fs.readFileSync(limitOutPath), newData);
  assert.throws(function () {
    hdiffpatch.patch(oldData, diffResult, { maxOutputBytes: 0 });
  }, /maxOutputBytes/);
  assert.throws(function () {
    hdiffpatch.patch(oldData, diffResult, { maxOutputBytes: 1.5 });
  }, /maxOutputBytes/);
  assertNoTempFiles(tempDir);
  console.log("  ✓ Oversized declared output/work-memory rejected before allocation");

  console.log("\nTest 14: DataView inputs...");
  var dvOld = new DataView(oldData.buffer, oldData.byteOffset, oldData.byteLength);
  var dvNew = new DataView(newData.buffer, newData.byteOffset, newData.byteLength);
  var dvDiff = hdiffpatch.diff(dvOld, dvNew);
  assert.deepStrictEqual(dvDiff, diffResult);
  var dvPatched = hdiffpatch.patch(
    dvOld,
    new DataView(dvDiff.buffer, dvDiff.byteOffset, dvDiff.byteLength)
  );
  assert.deepStrictEqual(dvPatched, newData);
  console.log("  ✓ DataView works for diff() and patch()");

  console.log("\nTest 15: async workers own copies of their inputs...");
  var mutOld = Buffer.from(oldData);
  var mutNew = Buffer.from(newData);
  var mutPromise = diffAsync(mutOld, mutNew);
  mutOld.fill(0);
  mutNew.fill(0);
  assert.deepStrictEqual(await mutPromise, diffResult);
  var taOld = new Uint8Array(oldData);
  var taNew = new Uint8Array(newData);
  var transferPromise = diffAsync(taOld, taNew);
  if (typeof structuredClone === "function") {
    structuredClone(taOld.buffer, { transfer: [taOld.buffer] });
    assert.strictEqual(taOld.byteLength, 0);
  }
  assert.deepStrictEqual(await transferPromise, diffResult);
  console.log("  ✓ Mutating or transferring inputs after the call cannot corrupt results");

  console.log("\nTest 16: windowSize bounds...");
  var boundsOutPath = path.join(tempDir, "bounds.diff");
  assert.throws(function () {
    hdiffpatch.diffWindow(oldPath, newPath, boundsOutPath, Math.pow(2, 64));
  });
  assert.throws(function () {
    hdiffpatch.diffWindow(oldPath, newPath, boundsOutPath, Number.MAX_SAFE_INTEGER + 1);
  });
  assert.throws(function () {
    hdiffpatch.diffWindow(oldPath, newPath, boundsOutPath, Infinity);
  });
  assert.throws(function () {
    hdiffpatch.diffWindow(oldPath, newPath, boundsOutPath, NaN);
  });
  assert.throws(function () {
    hdiffpatch.diffWindow(oldPath, newPath, boundsOutPath, { windowSize: Math.pow(2, 64) });
  });
  assert(!fs.existsSync(boundsOutPath));
  console.log("  ✓ Out-of-safe-integer windowSize values are rejected");
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
