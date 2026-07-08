const fs = require('fs');
const path = require('path');

function loadNative() {
  // 开发环境：本地编译产物优先于随包分发的 prebuild（与 node-gyp-build 的顺序一致）
  const localBuild = path.join(__dirname, 'build/Release/hdiffpatch.node');
  if (fs.existsSync(localBuild)) {
    return require(localBuild);
  }

  // 静态 require 路径便于打包工具分析；失败（缺文件、musl 等 ABI 不符）时回退 node-gyp-build
  try {
    switch (`${process.platform}-${process.arch}`) {
      case 'darwin-arm64':
        return require('./prebuilds/darwin-arm64/node-hdiffpatch.node');
      case 'darwin-x64':
        return require('./prebuilds/darwin-x64/node-hdiffpatch.node');
      case 'linux-x64':
        return require('./prebuilds/linux-x64/node-hdiffpatch.node');
      case 'linux-arm64':
        return require('./prebuilds/linux-arm64/node-hdiffpatch.node');
      case 'win32-x64':
        return require('./prebuilds/win32-x64/node-hdiffpatch.node');
    }
  } catch (err) {}

  return require('node-gyp-build')(__dirname);
}

const native = loadNative();

exports.native = native;

exports.diff = native.diff;
exports.patch = native.patch;
exports.diffStream = native.diffStream;
exports.patchStream = native.patchStream;
exports.diffSingleStream = native.diffSingleStream;
exports.patchSingleStream = native.patchSingleStream;
