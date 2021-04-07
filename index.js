/**
 * Created by housisong on 2021.04.06.
 */

var native;
try {
  native = require('./build/Release/hdiffpatch');
} catch(e) {
  console.error(e);
  native = require('./build/Debug/hdiffpatch');
}
exports.native = native;

exports.diff = function(oldBuf, newBuf) {
  var buffers = [];
  native.diff(oldBuf, newBuf, function(output){
    buffers.push(output);
  });

  return Buffer.concat(buffers);
}
