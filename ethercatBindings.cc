/*
Author: Santiago Ledesma (sledesm@gmail.com)

Note: The functions here are intended to be called from node-ethercat.js

It is unsafe to call them without parameter checking as the C code will assume that all prechecks about data validity
will be performed by the caller.


Please take into account that some structures (like pin names and offsets) are kept by the javascript part.

*/

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>
#include <sched.h>
#include <sys/mman.h>
#include <string.h>
#include <math.h>
#include <vector>
/****************************************************************************/
extern "C"{
    #include "ecrt.h"
    #include <pthread.h>
}

#include <node.h>
#include <node_buffer.h>

using namespace v8;
using namespace std;


/****************************************************************************/

#define LOOP_PERIOD_NS 1000000


/****************************************************************************/

// EtherCAT
static ec_master_t *master = NULL;
static ec_master_state_t master_state ;

static ec_domain_t *domain = NULL;
static ec_domain_state_t domain_state ;

class SSlave{
 public:

    int positionAlias;
    int positionIndex;
    int vendorId;
    int productId;
    ec_slave_config_t *slave_config;
    ec_sync_info_t *syncs;
    int numSyncs; // numero de syncs
};

static ec_pdo_entry_reg_t * domain_regs=NULL; // domain regs
static uint8_t *domain_pd=NULL; // process data

vector <SSlave> slaves;
static bool initialised=false;
static bool active = false;


/****************************************************************************/



void check_domain_state(void)
{
    ec_domain_state_t ds;

    ecrt_domain_state(domain, &ds);

    #ifdef DEBUG
        if (ds.working_counter != domain_state.working_counter)
            printf("Domain1: WC %u.\n", ds.working_counter);
        if (ds.wc_state != domain_state.wc_state)
            printf("Domain1: State %u.\n", ds.wc_state);
    #endif
    domain_state = ds;
}

/*****************************************************************************/

void check_master_state(void)
{
    ec_master_state_t ms;

    ecrt_master_state(master, &ms);

    #ifdef DEBUG
        if (ms.slaves_responding != master_state.slaves_responding)
            printf("%u slave(s).\n", ms.slaves_responding);
        if (ms.al_states != master_state.al_states)
            printf("AL states: 0x%02X.\n", ms.al_states);
        if (ms.link_up != master_state.link_up)
            printf("Link is %s.\n", ms.link_up ? "up" : "down");
    #endif
    master_state = ms;
}

/*****************************************************************************/


#define NSEC_PER_SEC    (1000000000) /* The number of nsecs per sec. */

#define EC_NEWTIMEVAL2NANO(TV) \
(((TV).tv_sec - 946684800ULL) * 1000000000ULL + (TV).tv_nsec)



static int _cycles=0;
int cycleStatus=0;
timespec cur_time;

void _cycle_start(){
        _cycles++;

        ecrt_master_send(master);

        clock_gettime(CLOCK_REALTIME, &cur_time);
        ecrt_master_application_time(master, EC_NEWTIMEVAL2NANO(cur_time));
        ecrt_master_sync_reference_clock(master);
        ecrt_master_sync_slave_clocks(master);
        ecrt_master_receive(master);
}

void _cycle_end(char *hal){
    clock_gettime(CLOCK_REALTIME, &cur_time);
    check_master_state();
    ecrt_domain_process(domain);
    ecrt_domain_queue(domain);
}


/****************************************************************************/

int _start()
{
    printf("Requesting master 0\n");
    master = ecrt_request_master(0);
    if (!master)
        return -1;
    printf("Master 0 obtained\n");
    domain = ecrt_master_create_domain(master);
    if (!domain)
        return -1;

    printf("Configuring PDOs...\n");
    printf("PDOs configured\n");
    printf("CheckPoint 5 \n");
    printf("Activating master...\n");
    if (ecrt_master_activate(master))
        return -1;
    if (!(domain_pd = ecrt_domain_data(domain))) {
        return -1;
    }
    printf("Started 2.\n");
    return 0;
}


void setError(const FunctionCallbackInfo<Value>&args,char *msg){
    Isolate *isolate=Isolate::GetCurrent();
    Local<Object> res=Object::New(isolate);
    res->Set(String::NewFromUtf8(isolate,"result"),String::NewFromUtf8(isolate,"error"));
    res->Set(String::NewFromUtf8(isolate,"error"),String::NewFromUtf8(isolate,msg));
    args.GetReturnValue().Set(res);
}


void start(const FunctionCallbackInfo<Value>& args) {
    Isolate *isolate=Isolate::GetCurrent();
    HandleScope scope(isolate);

    Local<Object> options=args[0]->ToObject();
    if (initialised){
        setError(args,(char*)"Cannot start system. Already started");
        return;
    }
    Local<Array>domainEntries=Local<Array>::Cast(options->Get(String::NewFromUtf8(isolate,"domainEntries")));
    int numEntries=domainEntries->Length();

    if (!master){
        master=ecrt_request_master(0);
        if (!master){
            setError(args,(char*)"Cannot retrieve master. Is ethercat running and are you root?");
            return;
        }
    }
    if (!domain){
        domain=ecrt_master_create_domain(master);
        if (!domain){
            setError(args,(char*)"Cannot create domain.");
            return;
        }
    }

    initialised=true;


    int numSlaves=slaves.size();
    printf("We have a total of %d slaves\n",numSlaves);
    for (int s=0; s<numSlaves; s++){
        SSlave slave=slaves[s];
        int positionAlias=slave.positionAlias;
        int positionIndex=slave.positionIndex;
        int vendorId=slave.vendorId;
        int productId=slave.productId;
        slave.slave_config=ecrt_master_slave_config(master,positionAlias,positionIndex,vendorId,productId);
        if (!slave.slave_config){
            setError(args,(char *)"Cannot get slave_config");
            return;
        }
        printf("Configuring pdos for slave %d, numSyncs: %d\n",s,slave.numSyncs);
        if (ecrt_slave_config_pdos(slave.slave_config,slave.numSyncs,slave.syncs)){
            setError(args,(char *)"Cannot configure slave pdos for slave");
            return;
        }
    }

    // Let's create the process domain array
    domain_regs=(ec_pdo_entry_reg_t *)malloc(numEntries*sizeof(ec_pdo_entry_reg_t));
    unsigned int *offsets=(unsigned int *)malloc(numEntries*sizeof(unsigned int));
    for (int i=0; i<numEntries; i++){
        Local<Object>entry=domainEntries->Get(i)->ToObject();
        ec_pdo_entry_reg_t domainReg;
        domainReg.alias=entry->Get(String::NewFromUtf8(isolate,"alias"))->NumberValue();
        domainReg.position=entry->Get(String::NewFromUtf8(isolate,"position"))->NumberValue();
        domainReg.vendor_id=entry->Get(String::NewFromUtf8(isolate,"vendor_id"))->NumberValue();
        domainReg.product_code=entry->Get(String::NewFromUtf8(isolate,"product_code"))->NumberValue();
        domainReg.index=entry->Get(String::NewFromUtf8(isolate,"index"))->NumberValue();
        domainReg.subindex=entry->Get(String::NewFromUtf8(isolate,"subindex"))->NumberValue();
        offsets[i]=-1;
        domainReg.offset=&(offsets[i]);
        domain_regs[i]=domainReg;
    }
    if (ecrt_domain_reg_pdo_entry_list(domain,domain_regs)){
        setError(args,(char *)"Cannot register domain");
        return;
    }
    for(int i=0; i<numEntries; i++){
        Local<Object>entry=domainEntries->Get(i)->ToObject();
        entry->Set(String::NewFromUtf8(isolate,"offset"),Number::New(isolate,offsets[i]));
        printf("Entry %d, offset %d\n",i,offsets[i]);
    }

        Local<Object> res=Object::New(isolate);
        res->Set(String::NewFromUtf8(isolate,"result"),String::NewFromUtf8(isolate,"ok"));
        res->Set(String::NewFromUtf8(isolate,"data"),Object::New(isolate));
        args.GetReturnValue().Set(res);

/*    if (!ecrt_master_activate(master)){
        setError(args,(char *)"Cannot activate master");
        return;
    }
    domain_pd=ecrt_domain_data(domain);
    if (!domain_pd){
        setError(args,(char*)"Did not get domain data");
        return;
    }*/

//    _start();


}





void printSlave(const FunctionCallbackInfo<Value>&args){
    Local<Value> vIndex=args[0];
    if (!vIndex->IsNumber()){
        return;
    }
    unsigned int index=vIndex->NumberValue();
    if (index>slaves.size()){
        return;
    }
    SSlave slave=slaves[index];
    int numSyncs=slave.numSyncs;
    printf("Slave %d, has %d syncs\n",index,numSyncs);
    for (int i=0; i<numSyncs; i++){
        ec_sync_info_t sync=slave.syncs[i];
        int syncIndex=sync.index;
        int syncDir=sync.dir;
        int nPdos=sync.n_pdos;
        int watchdogMode=sync.watchdog_mode;
        printf("  Sync %d has index %d, dir: %d, watchdog_mode: %d, number of pdos: %d\n",i,syncIndex,syncDir,watchdogMode,nPdos);
        for (int j=0; j<nPdos; j++){
            ec_pdo_info_t pdo=sync.pdos[j];
            int pdoIndex=pdo.index;
            int pdoNumEntries=pdo.n_entries;
            printf("     pdo %d has index 0x%X , number of entries: %d\n",j,pdoIndex,pdoNumEntries);
            for (int k=0; k<pdoNumEntries; k++){
                ec_pdo_entry_info_t entry=pdo.entries[k];
                int entryIndex=entry.index;
                int entrySubIndex=entry.subindex;
                int entryBitLength=entry.bit_length;
                printf("         entry %d has index 0x%X, subindex 0x%X, bit_length: %d\n",k,entryIndex,entrySubIndex,entryBitLength);
            }
        }
    }
}


// Adds a slave config to the slave list. This does not configure the slave yet. That will be done during the
// run phase.
void addSlave(const FunctionCallbackInfo<Value>&args){
    Isolate *isolate=Isolate::GetCurrent();
    HandleScope scope(isolate);

    Local<Value> options=args[0];
    if (!options->IsObject()){
        setError(args,(char *)"Options must be an object");
        return;
    }
//    offLoopPeriod=options->Get(String::NewFromUtf8(isolate,"offLoopPeriod"))->NumberValue();
    Local<Value> position=options->ToObject()->Get(String::NewFromUtf8(isolate,"position"));
    if (!position->IsObject()){
        setError(args,(char *)"position must be an object");
        return;
    }
    Local<Value> vAlias=position->ToObject()->Get(String::NewFromUtf8(isolate,"alias"));
    if (!vAlias->IsNumber()){
        setError(args,(char *)"alias must be a number");
        return;
    }
    int positionAlias=vAlias->NumberValue();
    Local<Value>vIndex=position->ToObject()->Get(String::NewFromUtf8(isolate,"index"));
    if (!vIndex->IsNumber()){
        setError(args,(char *)"index must be a number");
        return;
    }
    int positionIndex=vIndex->NumberValue();

    Local<Value>config=options->ToObject()->Get(String::NewFromUtf8(isolate,"config"));
    if (!config->IsObject()){
        setError(args,(char *)"config mut be an object");
        return;
    }

    Local<Value> id=config->ToObject()->Get(String::NewFromUtf8(isolate,"id"));
    if (!id->IsObject()){
        setError(args,(char *)"id must be an object");
        return;
    }
    Local<Value>vVendorId=id->ToObject()->Get(String::NewFromUtf8(isolate,"vendor"));
    if (!vVendorId->IsNumber()){
        setError(args,(char *)"vendor Must be a number");
        return;
    }
    Local<Value>vProductId=id->ToObject()->Get(String::NewFromUtf8(isolate,"product"));
    if (!vProductId->IsNumber()){
        setError(args,(char *)"product Must be a number");
        return;
    }
    int vendorId=vVendorId->NumberValue();
    int productId=vProductId->NumberValue();

    Local<Value>syncs=config->ToObject()->Get(String::NewFromUtf8(isolate,"syncs"));
    if (!syncs->IsArray()){
        setError(args,(char *)"Please provide valid sync data");
    }
    int numSyncs=Local<Array>::Cast(syncs)->Length();
    ec_sync_info_t *syncArray=(ec_sync_info_t *)malloc(numSyncs*sizeof(ec_sync_info_t));
    for (int s=0; s<numSyncs; s++){
        Local<Value>sync=Local<Array>::Cast(syncs)->Get(s);
        if (!sync->IsObject()){
            setError(args,(char *)"sync must be an object");
            return;
        }
        int syncManager=sync->ToObject()->Get(String::NewFromUtf8(isolate,"syncManager"))->NumberValue();
        int direction_enum=sync->ToObject()->Get(String::NewFromUtf8(isolate,"direction_enum"))->NumberValue();
        int watchdog_enum=sync->ToObject()->Get(String::NewFromUtf8(isolate,"watchdog_enum"))->NumberValue();
        syncArray[s].index=syncManager;
        ec_direction_t direction=EC_DIR_INPUT;
        switch(direction_enum){
            case 0: // input
              direction=EC_DIR_INPUT;
              break;
            case 1:
              direction=EC_DIR_OUTPUT;
              break;
        }
        ec_watchdog_mode_t watchdog=EC_WD_DISABLE;
        switch(watchdog_enum){
            case 0:
                watchdog=EC_WD_DISABLE;
               break;
            case 1:
               watchdog=EC_WD_ENABLE;
               break;
        }
        Local<Value>pdos=sync->ToObject()->Get(String::NewFromUtf8(isolate,"pdos"));
        if (!pdos->IsArray()){
            setError(args,(char *)"invalid object syntax. Please include pdos member");
            return;
        }
    //Local<Array> sessions = Local<Array>::Cast(
        int numPdos=Local<Array>::Cast(pdos)->Length();
        if (!numPdos){
            setError(args,(char *)"Please specify pdos");
            return;
        }
        ec_pdo_info_t *pdoArray=(ec_pdo_info_t *)malloc(numPdos*sizeof(ec_pdo_info_t));
        syncArray[s].dir=direction;
        syncArray[s].n_pdos=numPdos;
        syncArray[s].pdos=pdoArray;
        syncArray[s].watchdog_mode=watchdog;
        for (int p=0; p<numPdos; p++){
            Local<Value>oPdo=Local<Array>::Cast(pdos)->Get(p)->ToObject();
            if (!oPdo->IsObject()){
                setError(args,(char *)"Pdo not an object");
                return;
            }
            Local<Value>vPdoIndex=oPdo->ToObject()->Get(String::NewFromUtf8(isolate,"index"));
            if (!vPdoIndex->IsNumber()){
                setError(args,(char *)"index property in pdo must be a number");
                return;
            }
            unsigned int uPdoIndex=vPdoIndex->NumberValue();
            Local<Value>entries=oPdo->ToObject()->Get(String::NewFromUtf8(isolate,"entries"));
            int numEntries=Local<Array>::Cast(entries)->Length();
            ec_pdo_entry_info_t *pdoEntryArray=(ec_pdo_entry_info_t *)malloc(numEntries*sizeof(ec_pdo_entry_info_t));
            pdoArray[p].index=uPdoIndex;
            pdoArray[p].n_entries=numEntries;
            pdoArray[p].entries=pdoEntryArray;
            for (int i=0; i<numEntries; i++){
                Local<Value>oEntry=Local<Array>::Cast(entries)->Get(i)->ToObject();
                Local<Value>vEntryIndex=oEntry->ToObject()->Get(String::NewFromUtf8(isolate,"index"));
                if (!vEntryIndex->IsNumber()){
                    setError(args,(char *)"index property in pdo must be a number");
                    return;
                }
                Local<Value>vSubIndex=oEntry->ToObject()->Get(String::NewFromUtf8(isolate,"subindex"));
                if (!vSubIndex->IsNumber()){
                    setError(args,(char *)"subIndex property in pdo must be a number");
                    return;
                }
                Local<Value>vBitLength=oEntry->ToObject()->Get(String::NewFromUtf8(isolate,"bitLength"));
                if (!vBitLength->IsNumber()){
                    setError(args,(char *)"bit length must be a number");
                    return;
                }

                unsigned int uEntryIndex=vEntryIndex->NumberValue();
                unsigned int uSubIndex=vSubIndex->NumberValue();
                unsigned int uBitLength=vBitLength->NumberValue();

                pdoEntryArray[i].index=uEntryIndex;
                pdoEntryArray[i].subindex=uSubIndex;
                pdoEntryArray[i].bit_length=uBitLength;
            }
        }
    }


    SSlave slave;
    slave.slave_config=NULL;
    slave.syncs=syncArray;
    slave.numSyncs=numSyncs;
    slave.positionAlias=positionAlias;
    slave.positionIndex=positionIndex;
    slave.vendorId=vendorId;
    slave.productId=productId;

    int slaveIndex=slaves.size();
    slaves.push_back(slave);

    Local<Object>res=Object::New(isolate);
    res->Set(String::NewFromUtf8(isolate,"result"),String::NewFromUtf8(isolate,"ok"));
    Local<Object>data=Object::New(isolate);
    data->Set(String::NewFromUtf8(isolate,"index"),Number::New(isolate,slaveIndex));
    res->Set(String::NewFromUtf8(isolate,"data"),data);
    args.GetReturnValue().Set(res);



}


pthread_t cycleId;

#define MY_PRIORITY (49) /* we use 49 as the PRREMPT_RT use 50
 as the priority of kernel tasklets
 and interrupt handler by default */

#define MAX_SAFE_STACK (8*1024) /* The maximum stack size which is
 guaranteed safe to access without
 faulting */

#define NSEC_PER_SEC    (1000000000) /* The number of nsecs per sec. */

#define EC_NEWTIMEVAL2NANO(TV) \
(((TV).tv_sec - 946684800ULL) * 1000000000ULL + (TV).tv_nsec)

unsigned int _times=0;

void stack_prefault(void) {

    unsigned char dummy[MAX_SAFE_STACK];

    memset(dummy, 0, MAX_SAFE_STACK);
    return;
}

unsigned long maxDif=0;
double accumDif=0;
double avgDif=0;
unsigned long curTime=0;
unsigned long curSecs=0;

void * cycle(void *arg){
    struct sched_param param;
    /* Declare ourself as a real time task */
    pid_t tid;
    tid = syscall(SYS_gettid);
    printf("tid: %d\n",tid);
    param.sched_priority = MY_PRIORITY;
    if(sched_setscheduler(tid, SCHED_FIFO, &param) == -1) {
        perror("sched_setscheduler failed");
        return NULL;
        //exit(-1);
    }

    /* Lock memory */

    if(mlockall(MCL_CURRENT|MCL_FUTURE) == -1) {
        perror("mlockall failed");
        return NULL;
        //exit(-2);
    }

    /* Pre-fault our stack */

    stack_prefault();

    timespec cur_time;
    clock_gettime(CLOCK_REALTIME, &cur_time);

    timespec wait_time = {
        0,
        0
    };

    cpu_set_t my_set;
    CPU_ZERO(&my_set);
    CPU_SET(0, &my_set);
    sched_setaffinity(tid, sizeof(cpu_set_t), &my_set);

    clock_gettime(CLOCK_REALTIME, &cur_time);
    wait_time.tv_nsec=1000000000-wait_time.tv_nsec; // Nos sincronizamos al siguiente segundo
    //wait_time.tv_nsec = LOOP_PERIOD_NS - ((cur_time.tv_nsec+1) % LOOP_PERIOD_NS);
    nanosleep(&wait_time,NULL);
    wait_time.tv_nsec = LOOP_PERIOD_NS - ((cur_time.tv_nsec) % LOOP_PERIOD_NS);
    nanosleep(&wait_time,NULL);
    printf("Entering loop\n");
    while (1) {
        _times++;

        ecrt_master_send(master);
        ecrt_master_receive(master);


        ecrt_domain_process(domain);
        ecrt_domain_queue(domain);
        clock_gettime(CLOCK_REALTIME, &cur_time);
        wait_time.tv_nsec = LOOP_PERIOD_NS - ((cur_time.tv_nsec) % LOOP_PERIOD_NS);
        nanosleep(&wait_time, NULL);
        clock_gettime(CLOCK_REALTIME, &cur_time);
    }
}

//Activates the master
void activate(const FunctionCallbackInfo<Value>&args){
    Isolate *isolate=Isolate::GetCurrent();
    HandleScope scope(isolate);
    printf("Calling activate\n");
    if (active){
        setError(args,(char *)"Ehtercat already active");
        return;
    }
    pthread_create(&cycleId,NULL,&cycle,NULL);
    if (ecrt_master_activate(master)){
        setError(args,(char *)"Could not activate master");
        return;
    }
    if (!(domain_pd = ecrt_domain_data(domain))) {
        setError(args,(char*)"Could not retrieve domain data");
        return;
    }
    printf("Master active\n");
    Local<Object> res=Object::New(isolate);
    res->Set(String::NewFromUtf8(isolate,"result"),String::NewFromUtf8(isolate,"ok"));
    res->Set(String::NewFromUtf8(isolate,"data"),Object::New(isolate));
    args.GetReturnValue().Set(res);
}


void init(Handle<Object> exports) {

  NODE_SET_METHOD(exports,"addSlave",addSlave);
  NODE_SET_METHOD(exports,"printSlave",printSlave);
  NODE_SET_METHOD(exports,"start", start);
  NODE_SET_METHOD(exports,"activate",activate);

}

NODE_MODULE(ethercat, init)

/****************************************************************************/
