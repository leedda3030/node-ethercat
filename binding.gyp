{
    "targets": [
    {
        "target_name": "ethercat",
        "sources": [ "ethercatBindings.cc" ],
        "include_dirs":[
            "/usr/local/etherlab/src/ethercat-1.5.2/include",
            "../../include"
        ],
        "libraries":[
            "/usr/local/etherlab/src/ethercat-1.5.2/lib/.libs/libethercat.so",
            "-L/usr/local/etherlab/src/ethercat-1.5.2/lib/.libs",
            "-Wl,-rpath -Wl,/usr/local/etherlab/lib64"
        ],
        "cflags": [
            "-Wno-missing-field-initializers"
        ]
    }
]
}