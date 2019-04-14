# -*- mode: python -*-
{
    "targets": [{
        "target_name": "v4l2camera", 
        "sources": ["capture.c", "v4l2camera.cc"],
        "include_dirs" : [
            "<!(node -e \"require('nan')\")",
            "/opt/libjpeg-turbo/include"
        ],
        "conditions": [
            [ 'target_arch == "x64"', {
                "link_settings": {
                    "libraries": [
                        "/opt/libjpeg-turbo/lib64/libjpeg.a",
                        "/opt/libjpeg-turbo/lib64/libturbojpeg.a"
                    ]
                },
            }],
            [ 'target_arch == "ia32"', {
                "link_settings": {
                    "libraries": [
                        "/opt/libjpeg-turbo/lib32/libjpeg.a"
                        "/opt/libjpeg-turbo/lib32/libturbojpeg.a"
                    ]
                },
            }],
            [ 'target_arch == "arm"', {
                "link_settings": {
                    "libraries": [
                        "/opt/libjpeg-turbo/lib32/libjpeg.a",
                        "/opt/libjpeg-turbo/lib32/libturbojpeg.a",
                   ]
                },
                "cflags": ["-mfpu=neon"],
            }],
        ],
        "cflags": ["-Wall", "-Wextra", "-pedantic", "-O3", "-Wunused-function", "-Wunused-parameter"],
        "xcode_settings": {
    	    "OTHER_CPLUSPLUSFLAGS": ["-std=c++14"],
        },
        "cflags_c": ["-std=c++14"],
        "cflags_cc": ["-std=c++14"]
    }]
}

