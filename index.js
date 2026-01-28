var native;
try {
  native = require('./build/Release/hdiffpatch');
} catch(e) {
  console.error(e);
}
exports.native = native;

// 同步版本
exports.diff = native.diff;
exports.patch = native.patch;

