var ethercat = require('./node-ethercat');

var rtaSlave=require('./sampleConfigs/rta_x_plus.json');
var simpleCardSlave=require('./sampleConfigs/simpleCard.json');

// Specific device configuration. Will be written to the device on "start" call
var rtaConfig=[
    {
        "id":"configValue1",
        "type":"sdo",
        "params":{
            "index":"0x6060",
            "subindex":0,
            "type":"uint8",
            "value":8
        }
    },
    {
        "id":"configValue2",
        "type":"sdo",
        "params":{
            "index":"0x3206",
            "subindex":0,
            "type":"uint8",
            "value":1
        }
    },
    {
        "id":"configValue3",
        "type":"sdo",
        "params":{
            "index":"0x6081",
            "subindex":0,
            "type":"uint32",
            "value":400
        }
    }
]

var slaves=[
    {
        id: "rta0",
        position: {
            alias: 0,
            index: 0
        },
        definition: rtaSlave,
        config:rtaConfig
    },
    {
        id:"rta1",
        position:{
            alias:0,
            index:1
        },
        definition:rtaSlave,
        config:rtaConfig
    },
    {
        id:"simpleCard0",
        position:{
            alias:0,
            index:2
        },
        definition:simpleCardSlave
    }
]


function addSlaves(index,callback){
    console.log("adding slave "+index+" slaves length=",slaves.length);
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

waitForTorqueTime=1000;

var ready=0;
addSlaves(0,function(res){
    if (res.result=="error"){
        console.log(res);
        return;
    }
    var pins=ethercat.getPins();
    console.log("calling start");
    ethercat.start({},function(res){
        if (res.result=="error"){
            console.log(res);
            return;
        }
        console.log("start success");
        ethercat.activate({useSemaphore:false},function(res){
            ethercat.writePin("rta0.controlWord",6);
            ethercat.writePin("rta1.controlWord",6);
            setTimeout(function(){
                ethercat.writePin("rta0.controlWord",0x1f);
                ethercat.writePin("rta1.controlWord",0x1f);
                console.log("Starting torque");
                setTimeout(function(){
                    console.log("Activating drives");
                    setTimeout(function(){
                        ethercat.writePin("rta0.controlWord",0x6);
                        ethercat.writePin("rta1.controlWord",0x6);
                        console.log("ControlWord=6");
                        setTimeout(function(){
                            console.log("Deactivating drives");
                            setTimeout(function(){
                                ethercat.writePin("rta0.controlWord",0x1f);
                                ethercat.writePin("rta1.controlWord",0x1f);
                                console.log("Activating torque again");
                                setTimeout(function(){
                                    console.log("Activating system again");
                                    started=true;
                                    console.log("EthercatRT started");
                                    ready=true;
                                    return;

                                })
                            },waitForTorqueTime)
                        },waitForTorqueTime)
                    },waitForTorqueTime)
                },waitForTorqueTime)
            },waitForTorqueTime)
            console.log(res);
        })

    });
})
var posAxis=0;
function dummy(){
    if (ready){
        if (!posAxis){
            posAxis=0;
        }
        posAxis+=10;
        ethercat.writePin('rta0.targetPosition',posAxis);
        if (!(posAxis%100)){
            console.log("position: "+posAxis);
        }
        setTimeout(dummy,10);
    }else{
        setTimeout(dummy,1000);
        var controlWord=ethercat.readPin('rta1.statusWord');
        var pos=ethercat.readPin('rta1.actualPosition');
        console.log("rta1.controlWord: "+controlWord+" rta1.actualPosition: "+pos);
    }
};
dummy();
