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

// 异步版本 (Promise)
exports.diffAsync = function(oldBuf, newBuf) {
  return new Promise((resolve, reject) => {
    native.diff(oldBuf, newBuf, (err, result) => {
      if (err) reject(err);
      else resolve(result);
    });
  });
};

exports.patchAsync = function(oldBuf, diffBuf) {
  return new Promise((resolve, reject) => {
    native.patch(oldBuf, diffBuf, (err, result) => {
      if (err) reject(err);
      else resolve(result);
    });
  });
};
