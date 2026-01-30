#!/usr/bin/env node
'use strict';

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
      '  - Outputs are files specified by <outDiff>/<outNew>.',
    ].join('\n')
  );
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
    hdiffpatch.patchStream(oldFile, diffFile, outNew);
    console.log(outNew);
  } catch (err) {
    fail(err && err.message ? err.message : String(err));
  }
  process.exit(0);
}

fail(`unknown command: ${cmd}`);
