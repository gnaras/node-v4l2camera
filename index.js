const DEBUG = process.env.DEBUG === 'true' ? true : false;
var path = require('path');
if(DEBUG) {
  var raw = require(path.resolve( __dirname,"./build/Debug/v4l2camera"));
} else {
  var raw = require(path.resolve( __dirname,"./build/Release/v4l2camera"));
}

exports.Camera = raw.Camera;
