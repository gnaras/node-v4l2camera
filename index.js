const DEBUG = process.env.DEBUG === 'true' ? true : false;
var path = require('path');
var raw = null
if(DEBUG) {
  raw = require(path.resolve( __dirname,"./build/Debug/v4l2camera"));
} else {
  raw = require(path.resolve( __dirname,"./build/Release/v4l2camera"));
}

exports.Camera = raw.Camera;
