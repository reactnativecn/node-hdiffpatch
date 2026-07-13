const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const hdiffpatch = require('..');

if (process.env.HDIFF_MT_CHILD === '1') {
  const [oldPath, newPath, outPath, rawThreads] = process.argv.slice(2);
  const compressionThreads = Number(rawThreads);
  const cpuStartedAt = process.cpuUsage();
  const startedAt = performance.now();
  hdiffpatch.diffWindow(oldPath, newPath, outPath, {
    windowSize: 8 * 1024 * 1024,
    compressionThreads,
  });
  const durationMs = performance.now() - startedAt;
  const cpuUsage = process.cpuUsage(cpuStartedAt);
  const cpuTotalMs = (cpuUsage.user + cpuUsage.system) / 1000;
  const patch = fs.readFileSync(outPath);
  console.log(JSON.stringify({
    compressionThreads,
    cpuPercent: (cpuTotalMs / durationMs) * 100,
    cpuSystemMs: cpuUsage.system / 1000,
    cpuTotalMs,
    cpuUserMs: cpuUsage.user / 1000,
    durationMs,
    maxRSSKiB: process.resourceUsage().maxRSS,
    patchBytes: patch.length,
    patchSha256: crypto.createHash('sha256').update(patch).digest('hex'),
  }));
  process.exit(0);
}

function deterministicBytes(size, seed) {
  const out = Buffer.allocUnsafe(size);
  let x = seed >>> 0;
  for (let i = 0; i < size; i++) {
    x ^= x << 13;
    x ^= x >>> 17;
    x ^= x << 5;
    out[i] = x & 0xff;
  }
  return out;
}

function runChild(oldPath, newPath, outPath, compressionThreads) {
  const result = spawnSync(
    process.execPath,
    [__filename, oldPath, newPath, outPath, String(compressionThreads)],
    {
      encoding: 'utf8',
      env: { ...process.env, HDIFF_MT_CHILD: '1' },
    },
  );
  if (result.status !== 0) {
    throw new Error(result.stderr || `benchmark child exited ${result.status}`);
  }
  const outputLines = result.stdout.trim().split('\n');
  return JSON.parse(outputLines[outputLines.length - 1]);
}

const sizeMiB = Number(process.env.HDIFF_BENCHMARK_MB ?? 32);
const rounds = Number(process.env.HDIFF_BENCHMARK_ROUNDS ?? 3);
if (!Number.isInteger(sizeMiB) || sizeMiB < 8 ||
    !Number.isInteger(rounds) || rounds < 1) {
  throw new Error('HDIFF_BENCHMARK_MB must be >= 8 and rounds must be >= 1');
}

const tempRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'hdiff-mt-'));
try {
  const size = sizeMiB * 1024 * 1024;
  const oldData = deterministicBytes(size, 0x12345678);
  const newData = Buffer.from(oldData);
  deterministicBytes(Math.floor(size / 4), 0x9abcdef0).copy(
    newData,
    Math.floor(size / 3),
  );
  const oldPath = path.join(tempRoot, 'old.bin');
  const newPath = path.join(tempRoot, 'new.bin');
  fs.writeFileSync(oldPath, oldData);
  fs.writeFileSync(newPath, newData);

  const results = [];
  for (let round = 0; round < rounds; round++) {
    const order = round % 2 === 0 ? [1, 2] : [2, 1];
    for (const compressionThreads of order) {
      results.push(runChild(
        oldPath,
        newPath,
        path.join(tempRoot, `patch-${round}-${compressionThreads}.bin`),
        compressionThreads,
      ));
    }
  }

  const summary = [1, 2].map((compressionThreads) => {
    const samples = results.filter(
      (result) => result.compressionThreads === compressionThreads,
    );
    return {
      compressionThreads,
      cpuPercent: samples.reduce((sum, sample) => sum + sample.cpuPercent, 0) /
        samples.length,
      cpuTotalMs: samples.reduce((sum, sample) => sum + sample.cpuTotalMs, 0) /
        samples.length,
      durationMs: samples.reduce((sum, sample) => sum + sample.durationMs, 0) /
        samples.length,
      maxRSSKiB: Math.max(...samples.map((sample) => sample.maxRSSKiB)),
      patchBytes: samples[0].patchBytes,
      patchSha256: samples[0].patchSha256,
    };
  });
  if (summary[0].patchSha256 !== summary[1].patchSha256) {
    throw new Error('single-thread and dual-thread patches differ');
  }
  console.log(JSON.stringify({ sizeMiB, rounds, samples: results, summary }, null, 2));
} finally {
  fs.rmSync(tempRoot, { recursive: true, force: true });
}
