const native = require('./build/Release/hdiffpatch');

exports.native = native;

exports.diff = native.diff;
exports.patch = native.patch;

