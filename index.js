const native = require('node-gyp-build')(__dirname);

exports.native = native;

exports.diff = native.diff;
exports.patch = native.patch;
exports.diffStream = native.diffStream;
exports.patchStream = native.patchStream;
