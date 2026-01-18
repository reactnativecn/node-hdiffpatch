/**
 * 性能对比测试：1.0.6 vs 当前版本
 * 使用子进程隔离测量，确保内存数据准确
 */
const { execSync, spawnSync } = require('child_process');
const crypto = require('crypto');
const path = require('path');
const fs = require('fs');
const os = require('os');

// 安装 1.0.6 版本到临时目录
const tempDir = path.join(os.tmpdir(), 'hdiff-benchmark-' + Date.now());
fs.mkdirSync(tempDir, { recursive: true });

console.log('📦 正在安装 node-hdiffpatch@1.0.6 到临时目录...');
try {
  execSync('npm init -y', { cwd: tempDir, stdio: 'ignore' });
  execSync('npm install node-hdiffpatch@1.0.6', { cwd: tempDir, stdio: 'ignore' });
} catch (e) {
  console.error('❌ 安装 1.0.6 版本失败:', e.message);
  process.exit(1);
}
console.log('✅ 安装完成\n');

// 生成测试数据并保存到临时文件
function generateTestFiles(size, prefix) {
  const base = crypto.randomBytes(size);
  const modified = Buffer.from(base);
  const changeStart = Math.floor(size * 0.1);
  const changeEnd = Math.floor(size * 0.2);
  for (let i = changeStart; i < changeEnd; i++) {
    modified[i] = (modified[i] + 1) % 256;
  }
  const newData = Buffer.concat([Buffer.from('HDR_'), modified, Buffer.from('_END')]);
  
  const oldFile = path.join(tempDir, `${prefix}_old.bin`);
  const newFile = path.join(tempDir, `${prefix}_new.bin`);
  fs.writeFileSync(oldFile, base);
  fs.writeFileSync(newFile, newData);
  
  return { oldFile, newFile, oldSize: base.length, newSize: newData.length };
}

// 在子进程中运行测试，获取精确的内存和时间数据
function runInSubprocess(modulePath, oldFile, newFile) {
  const script = `
    const fs = require('fs');
    const hdiff = require('${modulePath}');
    
    const old = fs.readFileSync('${oldFile}');
    const newBuf = fs.readFileSync('${newFile}');
    
    // 预热
    hdiff.diff(old, newBuf);
    
    // 正式测试
    const memBefore = process.memoryUsage();
    const start = process.hrtime.bigint();
    
    const result = hdiff.diff(old, newBuf);
    
    const end = process.hrtime.bigint();
    const memAfter = process.memoryUsage();
    
    console.log(JSON.stringify({
      diffSize: result.length,
      timeMs: Number(end - start) / 1e6,
      heapBefore: memBefore.heapUsed,
      heapAfter: memAfter.heapUsed,
      rssBefore: memBefore.rss,
      rssAfter: memAfter.rss,
      externalBefore: memBefore.external,
      externalAfter: memAfter.external
    }));
  `;
  
  const result = spawnSync('node', ['-e', script], {
    cwd: tempDir,
    encoding: 'utf8',
    maxBuffer: 50 * 1024 * 1024
  });
  
  if (result.status !== 0) {
    console.error('子进程错误:', result.stderr);
    return null;
  }
  
  // 提取 JSON 输出
  const lines = result.stdout.trim().split('\n');
  const jsonLine = lines.find(l => l.startsWith('{'));
  if (!jsonLine) return null;
  
  return JSON.parse(jsonLine);
}

function formatBytes(bytes) {
  const abs = Math.abs(bytes);
  const sign = bytes < 0 ? '-' : '+';
  if (abs < 1024) return sign + abs + ' B';
  if (abs < 1024 * 1024) return sign + (abs / 1024).toFixed(1) + ' KB';
  return sign + (abs / 1024 / 1024).toFixed(2) + ' MB';
}

// 测试用例
const testCases = [
  { name: '1MB', size: 1024 * 1024 },
  { name: '10MB', size: 10 * 1024 * 1024 },
];

console.log('='.repeat(80));
console.log('📊 node-hdiffpatch 性能对比: v1.0.6 vs 当前版本');
console.log('   使用子进程隔离测试，确保内存数据准确');
console.log('='.repeat(80));

const currentModulePath = path.resolve(__dirname, '..');
const oldModulePath = path.join(tempDir, 'node_modules', 'node-hdiffpatch');

const results = [];

for (const tc of testCases) {
  console.log(`\n📁 测试: ${tc.name} 文件`);
  console.log('-'.repeat(60));
  
  // 生成测试文件
  const files = generateTestFiles(tc.size, tc.name);
  console.log(`   输入: old=${files.oldSize} bytes, new=${files.newSize} bytes`);
  
  // v1.0.6 测试
  console.log('   运行 v1.0.6...');
  const result106 = runInSubprocess(oldModulePath, files.oldFile, files.newFile);
  if (result106) {
    console.log(`   [v1.0.6] diff=${result106.diffSize} bytes, time=${result106.timeMs.toFixed(1)}ms`);
    console.log(`            heap: ${formatBytes(result106.heapAfter - result106.heapBefore)}`);
    console.log(`            rss:  ${formatBytes(result106.rssAfter - result106.rssBefore)}`);
  }
  
  // 当前版本测试
  console.log('   运行 当前版本...');
  const resultCurrent = runInSubprocess(currentModulePath, files.oldFile, files.newFile);
  if (resultCurrent) {
    console.log(`   [当前]   diff=${resultCurrent.diffSize} bytes, time=${resultCurrent.timeMs.toFixed(1)}ms`);
    console.log(`            heap: ${formatBytes(resultCurrent.heapAfter - resultCurrent.heapBefore)}`);
    console.log(`            rss:  ${formatBytes(resultCurrent.rssAfter - resultCurrent.rssBefore)}`);
  }
  
  // 一致性检验
  if (result106 && resultCurrent) {
    const consistent = result106.diffSize === resultCurrent.diffSize;
    console.log(`   [一致性] ${consistent ? '✅ diff 大小相同' : '❌ diff 大小不同!'}`);
    
    results.push({
      name: tc.name,
      time106: result106.timeMs,
      timeCurrent: resultCurrent.timeMs,
      heap106: result106.heapAfter - result106.heapBefore,
      heapCurrent: resultCurrent.heapAfter - resultCurrent.heapBefore,
      rss106: result106.rssAfter - result106.rssBefore,
      rssCurrent: resultCurrent.rssAfter - resultCurrent.rssBefore,
      diffSize: resultCurrent.diffSize,
      consistent
    });
  }
  
  // 清理测试文件
  fs.unlinkSync(files.oldFile);
  fs.unlinkSync(files.newFile);
}

// 汇总
console.log('\n');
console.log('='.repeat(80));
console.log('📈 汇总');
console.log('='.repeat(80));
console.log('\n| 文件 | Diff大小 | 耗时 v1.0.6 | 耗时 当前 | Heap v1.0.6 | Heap 当前 | RSS v1.0.6 | RSS 当前 |');
console.log('|------|----------|-------------|-----------|-------------|-----------|------------|----------|');

for (const r of results) {
  console.log(`| ${r.name} | ${(r.diffSize/1024).toFixed(0)}KB | ${r.time106.toFixed(1)}ms | ${r.timeCurrent.toFixed(1)}ms | ${formatBytes(r.heap106)} | ${formatBytes(r.heapCurrent)} | ${formatBytes(r.rss106)} | ${formatBytes(r.rssCurrent)} |`);
}

console.log(`
📝 说明:
- Heap: V8 堆内存变化（JS 对象）
- RSS: 进程总内存变化（包含 Native 分配）
- 使用子进程隔离确保每次测试在干净环境运行
`);

// 清理
fs.rmSync(tempDir, { recursive: true });
console.log('🧹 临时目录已清理');
