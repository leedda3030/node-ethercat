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
                        {index: 0x6040, subIndex: 0x0, name: "offControlWord0", type: "uint16"},
                        {index: 0x607a, subIndex: 0x0, name: "targetPosition0", type: "int32"}
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
                        {index: 0x6041, subIndex: 0x0, name: "statusWord0", type: "uint16"},
                        {index: 0x6064, subIndex: 0x0, name: "actualPosition0", type: "int32"}
                    ]
                }
            ],
            watchdog: "disable"
        }
    ]
}

var slaves=[
    {
        name: "rta1",
        position: {
            alias: 0,
            index: 0
        },
        config: rtaSlave
    },
    {
        name:"rta2",
        position:{
            alias:0,
            index:1
        },
        config:rtaSlave
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
    console.log(res);
})