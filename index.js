function loadNative() {
  const platform = process.platform;
  const arch = process.arch;

  if (platform === 'darwin' && arch === 'arm64') {
    return require('./prebuilds/darwin-arm64/node-hdiffpatch.node');
  }
  if (platform === 'linux' && arch === 'x64') {
    return require('./prebuilds/linux-x64/node-hdiffpatch.node');
  }
  if (platform === 'linux' && arch === 'arm64') {
    return require('./prebuilds/linux-arm64/node-hdiffpatch.node');
  }

  return require('node-gyp-build')(__dirname);
}

const native = loadNative();

exports.native = native;

exports.diff = native.diff;
exports.patch = native.patch;
exports.diffStream = native.diffStream;
exports.patchStream = native.patchStream;
