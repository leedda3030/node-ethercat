/*
    Author: Santiago Ledesma.

 This module aims to provide nodejs bindings to the ethercat controller IgH Ethercat Master, adding some syntax
 sugar.

 Features:

 a) Simple JSON structure to define slaves. It defines syncs, pdos, and also names for domain register configuration.
 b) Internally it creates a shared memory region to be mapped with other software - eg. CNC software
 c) Uses realtime preempt RT thread internally to read/write from shared memory and communicate with ethercat
 d) Intented to be embedded in a bigger node application. See ethercatServer at github for example
 e) Only critical parts are in C. Provides an abstraction layer in javascript
 f) All values to be sent/retrieved are published in a dictionary called pins. Each pin has a name, a size, a type and
    and offset in the process memory map. Pin offset is assigned once the process has started.


 */

var ethercat=require('./build/Release/ethercat');

var slaves={}; // associative array of slaves by id
var slaveList=[]; // list of slaves

var pins={}; // associative array of pins by id
var pinList=[]; // List of pins
var numPins=0; // How many pins have been added? We have one ec_pdo_entry_reg_t per pin. We need this to know            // how much memory to reserve in the C structures.
var initialised=false; // Can only be started once.

function start(options,callback){
    if (initialised){
        callback({
            result:"error",
            error:"Already started"
        });
        return;
    }
    var oldCallback=callback;
    callback=function(res){
        if (oldCallback.fired){
            return;
        }
        if (res.result=="ok"){
            initialised=true;
        }
        oldCallback.fired=true;
        oldCallback(res);
    }
    if (!options){
        options={};
    }
    // Let's create the domainReg structure in Javascript so that it is a piece of cake
    // to do what we have to do in C++
    var domainEntries=[];
    for (var i=0; i<pinList.length; i++){
        var pin=pinList[i];
        var slave=getSlave(pin.slaveId);
        if (!slave){
            callback({
                result:"error",
                error:"Inconsistency error. Cannot find slave: "+pin.slaveId
            });
            return;
        }
        domainEntries.push({
            alias:slave.position.alias,
            position:slave.position.index,
            vendor_id:slave.definition.id.vendor,
            product_code:slave.definition.id.product,
            index:pin.index,
            subindex:pin.subindex,
            offset:-1,
            name:pin.name
        });
    }
    options.domainEntries=domainEntries;
    var res=ethercat.start(options);
    if (res.result=="error"){
        callback(res);
        return;
    }

    //Reassign the offsets to the pins. Now we now the offsets
    for (var i=0; i<domainEntries.length; i++){
        var entry=domainEntries[i];
        var pin=pins[entry.name];
        if (!pin){
            callback({result:"error",error:"data inconsistency"});
            return;
        }
        pin.offset=entry.offset;
    }
    // Now we will write configuration values for the slaves
    function configSlave(slave,index,callback){
        var config=slave.config;
        if (!config || index>=config.length){
            callback({result:"ok",data:{}});
            return;
        }
        var record=config[index];
        if (!record){
            configSlave(slave,index+1,callback);
            return;
        }
        switch(record.type){
            case "sdo":
                var params=record.params;
                var params={
                    slaveId:slave.id,
                    index:params.index,
                    subindex:params.subindex,
                    type:params.type,
                    value:params.value
                }
                configSdo(params,function(res){
                    if (res.result=="error"){
                        callback(res);
                        return;
                    }
                    configSlave(slave,index+1,callback);
                })
                break;
            default:
                callback({result:"error",error:"Unsupported configuration type: "+record.type});
                return;
        }

    }

    function configSlaves(index,callback){
        if (!slaveList.length || index>=slaveList.length){
            callback({result:"ok",data:{}});
            return;
        }
        var slave=slaveList[index];
        if (slave && slave.config && slave.config.length){
            configSlave(slave,0,function(res){
                if (res.result=="error"){
                    callback(res);
                    return;
                }
                configSlaves(index+1,callback);
            })
        }else{
            configSlaves(index+1,callback);
        }
    }
    configSlaves(0,function(res){
        callback(res);
    })
}

function activate(options,callback){
    var res=ethercat.activate(options);
    callback(res);
}

// Returns a number value from string type value. C functions use fastType instead of type
function fastTypeFromType(type){
    var fastType=0;
    switch(type){
        case "uint8":
            fastType=0;
            break;
        case "int8":
            fastType=1;
            break;
        case "uint16":
            fastType=2;
            break;
        case "int16":
            fastType=3;
            break;
        case "uint32":
            fastType=4;
            break;
        case "int32":
            fastType=5;
            break;
        default:
            fastType=-1;
            return;
    }
    return fastType;
}

function addPin(options){
    var name=options.name;
    var size=options.size;
    var type=options.type;
    var slaveId=options.slaveId;
    var index=options.index;
    var subindex=options.subindex;

    if (pins[name]){
        return -1;
    }
    var fastType=fastTypeFromType(type);
    if (fastType==-1){
        callback({result:"error",error:"Unknown pdo type: "+type});
        return;
    }

    var pin={
        name:name,
        offset:-1, // indica que aún no sabemos el offset. No esta "bound"
        size:size,
        type:type,
        fastType:fastType,
        slaveId:slaveId,
        index:index,
        subindex:subindex
    }
    pinList.push(pin);
    pins[name]=pin;
    numPins++;
    return pinList.length-1;
}
// Prints on stdout information about a slave. This is for debugging purposes only
function printSlave(index){
    ethercat.printSlave(index);
}

function getSlave(id){
    for (var i=0; i<slaveList.length; i++){
        var slave=slaveList[i];
        if (slave.id==id){
            return slave;
        }
    }
}

function addSlave(options,callback){
    if (!options){
        callback({result:"error",error:"Please provide slave data"});
        return;
    }
    var id=options.id;

    if (!id){
        callback({result:"error",error:"Please provide slave id"});
        return;
    }
    if (getSlave[id]){
        callback({result:"error",error:"Duplicate slave. Please provide unique slave names"});
        return;
    }
    if (options.position===undefined ||
        options.position.alias===undefined ||
        options.position.index===undefined ||
        options.definition === undefined ||
        options.definition.id === undefined ||
        options.definition.id.vendor ===undefined ||
        options.definition.id.product === undefined ||
        options.definition.syncs ===undefined
    ){
        callback({result:"error",error:"Wrong definition for slave. Please check parameters"});
        return;
    }
    var syncs=options.definition.syncs;
    for (var s=0; s<syncs.length; s++){
        var sync=syncs[s];
        if (sync.syncManager===undefined){
            callback({result:"error",error:"Wrong sync definition for slave. Please specify syncManager index"});
            return;
        }
        var direction=sync.direction;
        var direction_enum;
        switch(direction){
            case "input":
                direction_enum=0;
                break;
            case "output":
                direction_enum=1;
                break;
            default:
                callback({result:"error",error:"direction in sync must either be input or output"});
                return;
        }
        var watchdog=sync.watchdog;
        var watchdog_enum;
        switch(watchdog){
            case "disable":
                watchdog_enum=0;
                break;
            case "enable":
                watchdog_enum=1;
                break;
            default:
                callback({result:"error",error:"watchdog in sync must either be disable or enable"});
                return;
        }
        sync.direction_enum=direction_enum;
        sync.watchdog_enum=watchdog_enum;
        var pdos=sync.pdos;
        if (!pdos || !pdos.length){
            callback({result:"error",error:"Please specify pdo list for sync "});
            return;
        }
        for (var i=0; i<pdos.length; i++){
            var pdo=pdos[i];
            if (!pdo){
                callback({result:"error",error:"wrong pdo format"});
                return;
            }
            var pdoIndex=pdo.index;
            if (pdoIndex==undefined){
                callback({result:"error",error:"Expecting index in pdo object"});
                return;
            }
            var entries=pdo.entries;
            if (!entries || !entries.length){
                callback({result:"error",error:"Please provide entries array"});
                return;
            }
            for (var j=0; j<entries.length; j++){
                var entry=entries[j];
                var entryName=entry.name;
                var entryType=entry.type;
                if (!entryName || !entryType){
                    callback({result:"error",error:"wrong entry format"});
                    return;
                }
                var bitLength;
                switch(entryType){
                    case "uint8":
                    case "int8":
                        bitLength=8;
                        break;
                    case "uint16":
                    case "int16":
                        bitLength=16;
                        break;
                    case "uint32":
                    case "int32":
                        bitLength=32;
                        break;
                    default:
                        callback({result:"error",error:"Unknown pdo type: "+entryType});
                        return;
                }
                var fullPinName=id+"."+entryName;
                var pinIndex=addPin({slaveId:id,name:fullPinName,size:bitLength,type:entryType,index:entry.index,subindex:entry.subindex});
                if (pinIndex==-1){
                    callback({result:"error",error:"Cannot add pin "+fullPinName+". Probably duplicated "})
                }
                entry.bitLength=bitLength;

            }
        }
    }

    var res=ethercat.addSlave(options);
    if (res.result=="error"){
        callback(res);
        return;
    }
    var index=res.data.index;
    options.index=index;
    slaveList.push(options);
    slaves[id]=options;
    callback(res);
}

function getPins(){
    return pins;
}

function readPin(name){
    var pin=pins[name];
    if (!pin){
        console.log("error reading pin: "+name);
        return undefined;
    }
    if (pin.offset==-1){
        console.log("Error reading pin. offset=-1, for name: "+name);
        return undefined;
    }
    if (pin && pin.offset!=-1){
        var v=ethercat.readPin(pin.offset,pin.fastType);
        return v;
    }
}

function writePin(name,value){
    var pin=pins[name];
    if (!pin){
        console.log("error writing pin: "+name);
        return undefined;
    }
    if (pin.offset==-1){
        console.log("Error writing pin. offset=-1, for name: "+name);
    }
    if (value===undefined){
        throw "writePin please provide value";
    }
    if (pin && pin.offset!=-1){
        ethercat.writePin(pin.offset,pin.fastType,value);
    }
}

function configSdo(options,callback){
    var slaveId=options.slaveId;
    var index=options.index;
    var subindex=options.subindex||0;
    var type=options.type; // "uint8","int8","uint16","int16","uint32","int32"
    var value=options.value;

    var slave=slaves[slaveId];
    if (!slave){
        callback({result:"error",error:"Cannot find slave with Id"+slaveId});
        return;
    }
    if (isNaN(value)){
        callback({result:"error",error:"Please supply valid value"});
        return;
    }

    var fastType=fastTypeFromType(type);
    if (fastType==-1){
        callback({result:"error",error:"Please specify sdo type"});
        return;
    }
    var res=ethercat.configSdo(slave.index,index,subindex,fastType,value);
    if (!res){
        res={result:"ok",data:{}};
    }
    callback(res);
/*

 void configSdo(const FunctionCallbackInfo<Value>&args){

 int slaveIndex=args[0]->NumberValue(); // we fetch slave index
 int sdoIndex=args[1]->NumberValue(); // we fetch sdoIndex
 int sdoSubIndex=args[2]->NumberValue(); // we fetch sdoSubIndex
 int type=args[3]->NumberValue(); // we fetch size
 int value=args[4]->NumberValue(); // we fetch the value
     */
}

function getMasterState(){
    var masterState=ethercat.getMasterState();
    return masterState;
}

module.exports={
    start:start,
    activate:activate,
    addSlave:addSlave,
    printSlave:printSlave,
    getPins:getPins,
    readPin:readPin,
    writePin:writePin,
    configSdo:configSdo,
    getMasterState:getMasterState
}

