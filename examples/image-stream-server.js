var http = require("http");
var v4l2camera = require("../");

var server = http.createServer(function (req, res) {
    //console.log(req.url);
    if (req.url === "/") {
        res.writeHead(200, {
            "content-type": "text/html;charset=utf-8",
        });
        res.end([
            "<!doctype html>",
            "<html><head><meta charset='utf-8'/>",
            "<script>(", script.toString(), ")()</script>",
            "</head><body>",
            "<img id='cam' width='640' height='480' />",
            "</body></html>",
        ].join(""));
        return;
    }
    if (req.url.match(/^\/.+\.jpg$/)) {
        res.writeHead(200, {
            "content-type": "image/jpg",
            "cache-control": "no-cache",
        });
        var jpg = cam.frameRaw();
        return res.end(Buffer(jpg));
    }
});
server.listen(3000);

var script = function () {
    window.addEventListener("load", function (ev) {
        var cam = document.getElementById("cam");
        (function load() {
            var img = new Image();
            img.addEventListener("load", function loaded(ev) {
                cam.parentNode.replaceChild(img, cam);
                img.id = "cam";
                cam = img;
                load();
            }, false);
            img.src = "/" + Date.now() + ".jpg";
        })();
    }, false);
};

var cam = new v4l2camera.Camera("/dev/video0")
for (var i = 0; i < cam.formats.length; i++) {
    console.log(cam.formats[i].formatName + " " + cam.formats[i].width + "x" + cam.formats[i].height +
                " " + cam.formats[i].interval.numerator + "/" + cam.formats[i].interval.denominator);
}
cam.configSet(cam.formats[3]);
cam.start();
cam.capture(function loop() {
    cam.capture(loop);
});
