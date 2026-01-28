const native = require('./build/Release/hdiffpatch');

exports.native = native;

// 同步版本
exports.diff = native.diff;
exports.patch = native.patch;

