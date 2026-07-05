#!/usr/bin/env node
'use strict';

const fs = require('fs');
const hdiffpatch = require('..');

function usage() {
  console.log(
    [
      'Usage:',
      '  hdp diff <oldFile> <newFile> <outDiff>',
      '  hdp patch <oldFile> <diffFile> <outNew>',
      '',
      'Notes:',
      '  - Uses streaming diff/patch for low memory usage.',
      '  - patch auto-detects the diff format (diffStream or diff/diffWithCovers output).',
      '  - Outputs are files specified by <outDiff>/<outNew>.',
    ].join('\n')
  );
}

// 两种 diff 格式的文件头:流式为 "HDIFF13",单压缩(diff()/diffWithCovers() 产物)为 "HDIFFSF20"
function detectDiffFormat(diffFile) {
  const header = Buffer.alloc(9);
  const fd = fs.openSync(diffFile, 'r');
  let bytesRead;
  try {
    bytesRead = fs.readSync(fd, header, 0, header.length, 0);
  } finally {
    fs.closeSync(fd);
  }
  const magic = header.slice(0, bytesRead).toString('latin1');
  if (magic.startsWith('HDIFFSF20')) return 'single';
  if (magic.startsWith('HDIFF13')) return 'stream';
  return null;
}

function fail(msg) {
  if (msg) console.error(`Error: ${msg}`);
  usage();
  process.exit(1);
}

const args = process.argv.slice(2);
if (args.length === 0 || args[0] === '-h' || args[0] === '--help') {
  usage();
  process.exit(0);
}

const cmd = args[0];
if (cmd === 'diff') {
  if (args.length !== 4) fail('diff expects 3 arguments.');
  const oldFile = args[1];
  const newFile = args[2];
  const outDiff = args[3];
  try {
    hdiffpatch.diffStream(oldFile, newFile, outDiff);
    console.log(outDiff);
  } catch (err) {
    fail(err && err.message ? err.message : String(err));
  }
  process.exit(0);
}

if (cmd === 'patch') {
  if (args.length !== 4) fail('patch expects 3 arguments.');
  const oldFile = args[1];
  const diffFile = args[2];
  const outNew = args[3];
  try {
    const format = detectDiffFormat(diffFile);
    if (format === 'single') {
      hdiffpatch.patchSingleStream(oldFile, diffFile, outNew);
    } else if (format === 'stream') {
      hdiffpatch.patchStream(oldFile, diffFile, outNew);
    } else {
      throw new Error(`${diffFile} is not a recognized hdiffpatch diff file.`);
    }
    console.log(outNew);
  } catch (err) {
    fail(err && err.message ? err.message : String(err));
  }
  process.exit(0);
}

fail(`unknown command: ${cmd}`);
