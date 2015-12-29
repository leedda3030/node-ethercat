var ethercat=require('./build/Release/ethercat');

var slaves={};

function start(){
    return ethercat.start();
}

function printSlave(index){
    ethercat.printSlave(index);
}
function addSlave(options,callback){
    if (!options){
        callback({result:"error",error:"Please provide slave data"});
        return;
    }
    var name=options.name;

    if (!name){
        callback({result:"error",error:"Please provide slave name"});
        return;
    }
    if (slaves[name]){
        callback({result:"error",error:"Duplicate slave. Please provide unique slave names"});
        return;
    }
    if (options.position===undefined ||
        options.position.alias===undefined ||
        options.position.index===undefined ||
        options.config === undefined ||
        options.config.id === undefined ||
        options.config.id.vendor ===undefined ||
        options.config.id.product === undefined ||
        options.config.syncs ===undefined
    ){
        callback({result:"error",error:"Wrong configuration for slave. Please check parameters"});
        return;
    }
    var syncs=options.config.syncs;
    for (var s=0; s<syncs.length; s++){
        var sync=syncs[s];
        if (sync.syncManager===undefined){
            callback({result:"error",error:"Wrong sync configuration for slave. Please specify syncManager index"});
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
    console.log("adding ethercat slave on index: ",index);
    slaves[name]={
        index:index,
        options:options
    }
    callback(res);
}
module.exports={
    start:start,
    addSlave:addSlave,
    printSlave:printSlave
}

