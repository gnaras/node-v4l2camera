let fs = require('fs');
let v4l2camera = require('v4l2camera');
let webSocket = require('ws');
const webPort = 8090,
width = 640,
height = 480,
fps = 30;
let clients = {};
let cam = null;
let camStarted = false;

let frameCallback = function (image) {
    let image64 = null;
    if(image) {
        image64 = image.toString('base64');
    }
    let frame = {
        type: 'frame',
        frame64: image64
    };
    let raw = JSON.stringify(frame);
    for (let index in clients) {
        if(clients[index].readyState != webSocket.OPEN) return;
        if(image && clients[index].bufferedAmount > 128) return;
        clients[index].send(raw);
    }
};

let initCam = function() {
    if(!cam) {
        cam = new v4l2camera.Camera('/dev/video0');
        let camFormatIdx = -1;
        for (let i = 0; i < cam.formats.length; i++) {
            if(cam.formats[i].width == width && cam.formats[i].height == height && 
            (cam.formats[i].formatName == 'YUYV') &&
            cam.formats[i].interval.denominator == fps) {
                camFormatIdx = i;
                break;
            }
        }
        if(camFormatIdx < 0) {
            console.error('No valid image format found:');
            for (let i = 0; i < cam.formats.length; i++) {
                console.log(cam.formats[i].formatName + ' ' + 
                            cam.formats[i].width + 'x' + cam.formats[i].height +' ' + 
                            cam.formats[i].interval.numerator + '/' + cam.formats[i].interval.denominator);
            }
            return;
        }
        cam.configSet(cam.formats[camFormatIdx]);  //TODO: check preference and convert to jpg if format is yuv. Or maybe let browser UI take care of format conversion?
        console.log(cam.formats[camFormatIdx]);
    }
}

let disconnectClient = function (index) {
    delete clients[index];
    if (Object.keys(clients).length == 0) {
        console.log('No Clients, Closing Capture');
        isRunning = false;
    }
};

let connectClient = function (ws) {
    let index = '' + new Date().getTime();
    if (!camStarted) {
        console.log('New Clients, Opening Capture');
        cam.start();
        camStarted = true;
        cam.capture(function loop() {
            if(!isRunning) {
                cam.stop();
                camStarted = false;
                console.log('Stopped Camera');
                return;
            }
            let formatName = cam.configGet().formatName;
            if(formatName == 'YUYV') {
                cam.toJPEGAsync(function(error, frame) {
                    if(error) {
                        console.log('got jpeg error:', error);
                    }
                    frameCallback(Buffer(frame), null);
                });
                //frameCallback(Buffer(cam.toJPEG()), null);
            } else {
                frameCallback(Buffer(cam.frameRaw()), null);
            }
            cam.capture(loop);
        });
        isRunning = true;
    }
    clients[index] = ws;
    return index;
};

// Init cam
initCam();

//Create Http Server
let http = require('http');
let index = fs.readFileSync(__dirname + '/test.html', 'utf8');
let httpServer = http.createServer(function (req, resp) {
    resp.writeHead(200, {
        'Content-Type': 'text/html'
    });
    resp.end(index);
});
let wss = new webSocket.Server({
    server: httpServer
});

wss.on('connection', function (ws) {
    let index = connectClient(ws);

    ws.on('close', function () {
        disconnectClient(index);
    });

    ws.on('open', function () {
        console.log('Opened');
    });

    ws.on('message', function (message) {
        let data = JSON.parse(message);
        switch (data.type) {
        case 'size':
        {
            console.log('received:', data);
            ws.send(JSON.stringify({
                type: 'size',
                width: width,
                height: height
            }));
        }
        break;
        }
    });
});

httpServer.listen(webPort);

console.log('Http Server Started on ' + webPort);
