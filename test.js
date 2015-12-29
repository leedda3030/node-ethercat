var ethercat = require('./node-ethercat');

var rtaSlave={
    id: {vendor: 0x0000017f, product: 0x00000014},
    syncs: [
        {
            syncManager: 2,
            direction: "output",
            pdos: [
                {
                    index: 0x1601,
                    entries: [
                        {index: 0x6040, subindex: 0x0, name: "controlWord", type: "uint16"},
                        {index: 0x607a, subindex: 0x0, name: "targetPosition", type: "int32"}
                    ]
                }
            ],
            watchdog: "disable"
        },
        {
            syncManager: 3,
            direction: "input",
            pdos: [
                {
                    index: 0x1A01,
                    entries: [
                        {index: 0x6041, subindex: 0x0, name: "statusWord", type: "uint16"},
                        {index: 0x6064, subindex: 0x0, name: "actualPosition", type: "int32"}
                    ]
                }
            ],
            watchdog: "disable"
        }
    ]
}

var simpleCardSlave={
    id: {vendor: 3543808, product: 0xcacb},
    syncs: [
        {
            syncManager: 2,
            direction: "output",
            pdos: [
                {
                    index: 0x1601,
                    entries: [
                        {index: 0x7010, subindex: 0x0, name: "Position0", type: "int32"},
                        {index: 0x7011, subindex: 0x0, name: "Position1", type: "int32"},
                        {index: 0x7012, subindex: 0x0, name: "Position2", type: "int32"},
                        {index: 0x7013, subindex: 0x0, name: "Position3", type: "int32"},
                        {index: 0x7014, subindex: 0x0, name: "ControlWord", type: "uint32"},
                    ]
                }
            ],
            watchdog: "disable"
        },
        {
            syncManager: 3,
            direction: "input",
            pdos: [
                {
                    index: 0x1A00,
                    entries: [
                        {index: 0x6001, subindex: 0x0, name: "Encoder0", type: "int32"},
                        {index: 0x6002, subindex: 0x0, name: "Encoder1", type: "int32"},
                        {index: 0x6003, subindex: 0x0, name: "Encoder2", type: "int32"},
                        {index: 0x6004, subindex: 0x0, name: "Encoder3", type: "int32"},
                        {index: 0x6005, subindex: 0x0, name: "statusWord", type: "uint32"}
                    ]
                }
            ],
            watchdog: "disable"
        }
    ]
}

var slaves=[
    {
        id: "rta1",
        position: {
            alias: 0,
            index: 0
        },
        config: rtaSlave
    },
    {
        id:"rta2",
        position:{
            alias:0,
            index:1
        },
        config:rtaSlave
    },
    {
        id:"simpleCard1",
        position:{
            alias:0,
            index:2
        },
        config:simpleCardSlave
    }
]

function addSlaves(index,callback){
    if (index>=slaves.length){
        callback({result:"ok",data:{}});
        return;
    }
    ethercat.addSlave(slaves[index],function(res){
        if (res.result=="error"){
            callback(res);
            return;
        }
        ethercat.printSlave(res.data.index);
        addSlaves(index+1,callback);
    })
}

addSlaves(0,function(res){
    var pins=ethercat.getPins();
    ethercat.start({},function(res){
        ethercat.activate({},function(res){
            console.log(res);
        })
    });
})
function dummy(){
    setTimeout(dummy,1000);
    var pos=ethercat.readPin('rta1.actualPosition');
    console.log("rta1.actualPosition: "+pos);
};
dummy();
