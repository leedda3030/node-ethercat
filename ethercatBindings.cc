/*
*/

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <unistd.h>
#include <sched.h>
#include <stdio.h>
#include <time.h>
#include <sched.h>
#include <sys/mman.h>
#include <string.h>
#include <math.h>
/****************************************************************************/
extern "C"{
    #include "ecrt.h"
    #include <pthread.h>
}

#include <node.h>
#include <node_buffer.h>

using namespace v8;


/****************************************************************************/

#define CONFIGURE_PDOS  1
#define SDO_ACCESS      0
#define LOOP_PERIOD_NS 1000000


/****************************************************************************/

// EtherCAT
static ec_master_t *master = NULL;
static ec_master_state_t master_state ;

static ec_domain_t *domain1 = NULL;
static ec_domain_state_t domain1_state ;

/*static ec_slave_config_t *sc_ana_in = NULL;
static ec_slave_config_state_t sc_ana_in_state = {};

static ec_slave_config_t *sc_rta_x = NULL;
static ec_slave_config_state_t sc_rta_x_state = {};*/

/****************************************************************************/

// process data
static uint8_t *domain1_pd = NULL;

#define BusCouplerPos  0, 0
#define DigOutSlavePos 0, 2
#define AnaInSlavePos  0, 3
#define AnaOutSlavePos 0, 4

#define Beckhoff_EK1100 0x00000002, 0x044c2c52
#define Beckhoff_EL2004 0x00000002, 0x07d43052
#define Beckhoff_EL2032 0x00000002, 0x07f03052
#define Beckhoff_EL3152 0x00000002, 0x0c503052
#define Beckhoff_EL3102 0x00000002, 0x0c1e3052
#define Beckhoff_EL4102 0x00000002, 0x10063052
#define RTA_X_PLUS   0x0000017f,0x00000014


#define KNUMDRIVES 2

ec_slave_config_t *slave_config[KNUMDRIVES];
static ec_slave_config_state_t sc_state[KNUMDRIVES];

typedef struct slave_data_t{
    unsigned int offControlWord;
    unsigned int offTargetPosition;
    unsigned int offStatusWord;
    unsigned int offActualPosition;
    uint16_t controlWord;
    int32_t targetPosition;
    uint16_t statusWord;
    int32_t actualPosition;
    bool active; // si está activo o no
    bool oldActive; // si está activo o no

    int moveType; // 0 --> en velocidad, 1 senoidal

    unsigned int period; // ciclos por periodo de 360 grados
    unsigned int startCycle; //  tick donde se inició la senoidal
    float fStartPos; // Posición donde se inició la onda
    float fAmplitude; // amplitud del movimiento

    double fSpeed;
    double fTargetPos; // Target position en flotante
}slave_data_t;

slave_data_t slave_data[KNUMDRIVES];


// OJO. NO DEPENDE DE KNUMDRIVES!!!

/*const static ec_pdo_entry_reg_t domain1_regs[] = {
    {0,0,RTA_X_PLUS,0x6040, 0x00, &(slave_data[0].offControlWord)}, // Control Word
    {0,0,RTA_X_PLUS,0x6060, 0x00, &(slave_data[0].offModesOfOperation)}, // Modes of Operation
    {0,0,RTA_X_PLUS,0x607a, 0x00, &(slave_data[0].offTargetPosition)}, // Target Position
    {0,0,RTA_X_PLUS,0x6041, 0x00, &(slave_data[0].offStatusWord)}, // Status Word
    {0,0,RTA_X_PLUS,0x6061, 0x00, &(slave_data[0].offModesOfOperationDisplay)}, // Modes of Operation Display
    {0,0,RTA_X_PLUS,0x6064, 0x00, &(slave_data[0].offActualPosition)}, // Position Actual Value
    {0,1,RTA_X_PLUS,0x6040, 0x00, &(slave_data[1].offControlWord)}, // Control Word
    {0,1,RTA_X_PLUS,0x6060, 0x00, &(slave_data[1].offModesOfOperation)}, // Modes of Operation
    {0,1,RTA_X_PLUS,0x607a, 0x00, &(slave_data[1].offTargetPosition)}, // Target Position
    {0,1,RTA_X_PLUS,0x6041, 0x00, &(slave_data[1].offStatusWord)}, // Status Word
    {0,1,RTA_X_PLUS,0x6061, 0x00, &(slave_data[1].offModesOfOperationDisplay)}, // Modes of Operation Display
    {0,1,RTA_X_PLUS,0x6064, 0x00, &(slave_data[1].offActualPosition)}, // Position Actual Value
   {}
};*/


const static ec_pdo_entry_reg_t domain1_regs[] = {
    {0,0,RTA_X_PLUS,0x6040, 0x00, &(slave_data[0].offControlWord)}, // Control Word
    {0,0,RTA_X_PLUS,0x607a, 0x00, &(slave_data[0].offTargetPosition)}, // Target Position
    {0,0,RTA_X_PLUS,0x6041, 0x00, &(slave_data[0].offStatusWord)}, // Status Word
    {0,0,RTA_X_PLUS,0x6064, 0x00, &(slave_data[0].offActualPosition)}, // Position Actual Value
    {0,1,RTA_X_PLUS,0x6040, 0x00, &(slave_data[1].offControlWord)}, // Control Word
    {0,1,RTA_X_PLUS,0x607a, 0x00, &(slave_data[1].offTargetPosition)}, // Target Position
    {0,1,RTA_X_PLUS,0x6041, 0x00, &(slave_data[1].offStatusWord)}, // Status Word
    {0,1,RTA_X_PLUS,0x6064, 0x00, &(slave_data[1].offActualPosition)}, // Position Actual Value
   {}
};



/*****************************************************************************/



// RTA

ec_pdo_entry_info_t rta_x_plus_pdo_entries[] = {
    {0x6040, 0x00, 16}, // Control Word
    {0x607a, 0x00, 32}, // Target Position
    {0x6041, 0x00, 16}, // Status Word
    {0x6064, 0x00, 32}, // Position Actual Value
};

ec_pdo_info_t rta_x_plus_pdos[] = {
    {0x1601, 2, rta_x_plus_pdo_entries + 0}, // RxPDO100
    {0x1A01, 2, rta_x_plus_pdo_entries + 2}, // TxPDO100
};


ec_sync_info_t rta_x_plus_syncs[] = {
/*    {0, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE},
    {1, EC_DIR_INPUT, 0, NULL, EC_WD_DISABLE},*/
    {2, EC_DIR_OUTPUT, 1, rta_x_plus_pdos + 0, EC_WD_DISABLE},
    {3, EC_DIR_INPUT, 1, rta_x_plus_pdos + 1, EC_WD_DISABLE},
    {0xff}
};




/*****************************************************************************/

#if SDO_ACCESS
static ec_sdo_request_t *sdo;
#endif

/*****************************************************************************/

void check_domain1_state(void)
{
    ec_domain_state_t ds;

    ecrt_domain_state(domain1, &ds);

    #ifdef DEBUG
        if (ds.working_counter != domain1_state.working_counter)
            printf("Domain1: WC %u.\n", ds.working_counter);
        if (ds.wc_state != domain1_state.wc_state)
            printf("Domain1: State %u.\n", ds.wc_state);
    #endif
    domain1_state = ds;
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

void check_slave_config_states(void)
{
    #ifdef DEBUG

    int i;
    int allOperational;
    ec_slave_config_t *current;

    allOperational=0;
    for (i=0; i<KNUMDRIVES; i++){
        current=slave_config[i];
        ecrt_slave_config_state(current,&(sc_state[i]));
        if (sc_state[i].al_state&0x8){
           allOperational=1;
        }
    }
        static int beforeAllOperational=0;
        if (allOperational!=beforeAllOperational){
            printf("All operational now is: %s\n",allOperational?"true":"false");

        }
        beforeAllOperational=allOperational;
    #endif
}

/*****************************************************************************/

#if SDO_ACCESS
void read_sdo(void)
{
    switch (ecrt_sdo_request_state(sdo)) {
        case EC_REQUEST_UNUSED: // request was not used yet
            ecrt_sdo_request_read(sdo); // trigger first read
            break;
        case EC_REQUEST_BUSY:
            fprintf(stderr, "Still busy...\n");
            break;
        case EC_REQUEST_SUCCESS:
            fprintf(stderr, "SDO value: 0x%04X\n",
                    EC_READ_U16(ecrt_sdo_request_data(sdo)));
            ecrt_sdo_request_read(sdo); // trigger next read
            break;
        case EC_REQUEST_ERROR:
            fprintf(stderr, "Failed to read SDO!\n");
            ecrt_sdo_request_read(sdo); // retry reading
            break;
    }
}
#endif


#define NSEC_PER_SEC    (1000000000) /* The number of nsecs per sec. */

#define EC_NEWTIMEVAL2NANO(TV) \
(((TV).tv_sec - 946684800ULL) * 1000000000ULL + (TV).tv_nsec)



static int _cycles=0;
int cycleStatus=0;
timespec cur_time;
timespec wait_time = {
        0,
        0
};

void _cycle_start(){
        _cycles++;

        ecrt_master_send(master);

        clock_gettime(CLOCK_REALTIME, &cur_time);
        ecrt_master_application_time(master, EC_NEWTIMEVAL2NANO(cur_time));
        if (_cycles<=2){
/*           for (int i=0; i<KNUMDRIVES; i++){
            clock_gettime(CLOCK_REALTIME, &cur_time);
            ecrt_slave_config_dc(slave_config[i], 0x0300, LOOP_PERIOD_NS,LOOP_PERIOD_NS - (cur_time.tv_nsec % LOOP_PERIOD_NS), 0, 0);
           }*/
        }
        ecrt_master_sync_reference_clock(master);
        ecrt_master_sync_slave_clocks(master);
        ecrt_master_receive(master);

//            ecrt_domain_process(domain1);

}

void _cycle_end(char *hal){
    int i;
    unsigned int controlWord;
    unsigned int sinCycles;
    double u;
    double pos;
    clock_gettime(CLOCK_REALTIME, &cur_time);
    check_master_state();
    if (master_state.al_states&0x8){
       for (i=0; i<KNUMDRIVES; i++){
            slave_data[i].statusWord=EC_READ_U16(domain1_pd+slave_data[i].offStatusWord);
            slave_data[i].actualPosition=EC_READ_S32(domain1_pd+slave_data[i].offActualPosition);
            controlWord=slave_data[i].controlWord;
            if (slave_data[i].active){
            //    if (!(slave_data[i].statusWord&0x400)  || !slave_data[i].oldActive){
                        slave_data[i].targetPosition=slave_data[i].fTargetPos;
            //    }
            }else{
                slave_data[i].targetPosition=slave_data[i].actualPosition;
            }
            if (slave_data[i].moveType==0){
                slave_data[i].fTargetPos+=slave_data[i].fSpeed;
                slave_data[i].startCycle=0;
            }else{
                if (slave_data[i].startCycle==0){
                   slave_data[i].startCycle=_cycles;
                   slave_data[i].fStartPos=slave_data[i].fTargetPos;
                }
                sinCycles=(_cycles-slave_data[i].startCycle)%slave_data[i].period;
                u=((double)sinCycles)/slave_data[i].period;
                pos=slave_data[i].fAmplitude*sin(u*M_PI*2);
                pos+=slave_data[i].fStartPos;
                slave_data[i].fTargetPos=pos;
            }
            EC_WRITE_U16(domain1_pd+slave_data[i].offControlWord,controlWord);
            EC_WRITE_S32(domain1_pd+slave_data[i].offTargetPosition,slave_data[i].targetPosition);
            slave_data[i].oldActive=slave_data[i].active;
        }
    }
    ecrt_domain_process(domain1);
    ecrt_domain_queue(domain1);


/*
    ecrt_master_receive(master);
    ecrt_domain_process(domain1);*/
}


/****************************************************************************/

int _start()
{
    int i;

    printf("Requesting master 0\n");
    master = ecrt_request_master(0);
    if (!master)
        return -1;
    printf("Master 0 obtained\n");
    domain1 = ecrt_master_create_domain(master);
    if (!domain1)
        return -1;

    printf("Configuring PDOs...\n");


    for (i=0; i<KNUMDRIVES; i++){



        slave_data[i].controlWord=0;
        slave_data[i].targetPosition=0;

        sc_state[i].online=0;
        sc_state[i].operational=0;
        sc_state[i].al_state=1;
        slave_data[i].fSpeed=0;
        slave_data[i].fTargetPos=0;
        slave_data[i].startCycle=0;
        slave_data[i].period=1000; // 1 segundo
        slave_data[i].fAmplitude=10000; // casi una vuelta
        slave_data[i].fStartPos=0;
        slave_data[i].moveType=0;
        slave_data[i].active=false;
        slave_data[i].oldActive=false;


        if (!(slave_config[i] = ecrt_master_slave_config(
                        master, 0,i, RTA_X_PLUS))) {
            fprintf(stderr, "Failed to get slave configuration.\n");
            return -1;
        }


//20151224 --> idea

/*


slaveConfig:{
    pdos:[
         {index:0x6040,value:0x0,size:16,pinName:"offControlWord0"},
         {index:0x607a,value:0x0,size:32,pinName:"targetPosition0"},
         {index:0x6041,value:0x0,size:16,pinName:"statusWord0"},
         {index:0x6064,value:0x0,size:32,pinName:"actualPosition0"}
    ],
    rxPdo:{
        index:0x1601,
        records:[0,1]
    },
    txPdo:{
       index:0x1A01,
       records:[2,3]
    },
    syncs:[
      {index:2,
       dir:"output",
       value:1,
       records:[0,1],
       wd:"EC_WD_DISABLE"
      },
      {index:2,
       dir:"input",
       value:1,
       records:[2,3],
       wd:"EC_WD_DISABLE"
    ]
}

   ethercat.addSlave({
        position:[0,i], // donde están en el bus
        ids:[0x0000017f,0x00000014], // cuál es el id del dispositivo
        config:{
                   pdos:[
                        {index:0x6040,value:0x0,size:16,pinName:"offControlWord0"},
                        {index:0x607a,value:0x0,size:32,pinName:"targetPosition0"},
                        {index:0x6041,value:0x0,size:16,pinName:"statusWord0"},
                        {index:0x6064,value:0x0,size:32,pinName:"actualPosition0"}
                   ],
                   rxPdo:{
                       index:0x1601,
                       records:[0,1]
                   },
                   txPdo:{
                      index:0x1A01,
                      records:[2,3]
                   },
                   syncs:[
                     {index:2,
                      dir:"output",
                      value:1,
                      records:[0,1],
                      wd:"EC_WD_DISABLE"
                     },
                     {index:2,
                      dir:"input",
                      value:1,
                      records:[2,3],
                      wd:"EC_WD_DISABLE"
                   ]
               }
   });


const static ec_pdo_entry_reg_t domain1_regs[] = {
    {0,0,RTA_X_PLUS,0x6040, 0x00, &(slave_data[0].offControlWord)}, // Control Word
    {0,0,RTA_X_PLUS,0x607a, 0x00, &(slave_data[0].offTargetPosition)}, // Target Position
    {0,0,RTA_X_PLUS,0x6041, 0x00, &(slave_data[0].offStatusWord)}, // Status Word
    {0,0,RTA_X_PLUS,0x6064, 0x00, &(slave_data[0].offActualPosition)}, // Position Actual Value
    {0,1,RTA_X_PLUS,0x6040, 0x00, &(slave_data[1].offControlWord)}, // Control Word
    {0,1,RTA_X_PLUS,0x607a, 0x00, &(slave_data[1].offTargetPosition)}, // Target Position
    {0,1,RTA_X_PLUS,0x6041, 0x00, &(slave_data[1].offStatusWord)}, // Status Word
    {0,1,RTA_X_PLUS,0x6064, 0x00, &(slave_data[1].offActualPosition)}, // Position Actual Value
   {}
};



ec_pdo_entry_info_t rta_x_plus_pdo_entries[] = {
    {0x6040, 0x00, 16}, // Control Word
    {0x607a, 0x00, 32}, // Target Position
    {0x6041, 0x00, 16}, // Status Word
    {0x6064, 0x00, 32}, // Position Actual Value
};

ec_pdo_info_t rta_x_plus_pdos[] = {
    {0x1601, 2, rta_x_plus_pdo_entries + 0}, // RxPDO100
    {0x1A01, 2, rta_x_plus_pdo_entries + 2}, // TxPDO100
};



*/
        //printf("Configuring mode for slave: %i in 0x6060 as 8\n",i);
        ecrt_slave_config_sdo8(slave_config[i],0x6060,0,8);
        ecrt_slave_config_sdo8(slave_config[i],0x3206,0,1);
        ecrt_slave_config_sdo32(slave_config[i],0x6081,0,400);

        if (ecrt_slave_config_pdos(slave_config[i] , EC_END, rta_x_plus_syncs)) {
                fprintf(stderr, "Failed to configure PDOs.\n");
                return -1;
            }

        //LOOP_PERIOD_NS. OJO. HAY QUE PASARSELO AQUI, LEYENDOLO DEL PIN DE LA HAL DESDE JAVASCRIPT
        ecrt_slave_config_dc(slave_config[i], 0x0300, LOOP_PERIOD_NS, 200000, 0, 0);
        //ecrt_slave_config_dc(slave_config[i], 0x0300, LOOP_PERIOD_NS, 0, 0, 0);

    }

    printf("PDOs configured\n");

    if (ecrt_domain_reg_pdo_entry_list(domain1, domain1_regs)) {
        fprintf(stderr, "PDO entry registration failed!\n");
        return -1;
    }

        printf("CheckPoint 5 \n");


    printf("Activating master...\n");
    if (ecrt_master_activate(master))
        return -1;

    if (!(domain1_pd = ecrt_domain_data(domain1))) {
        return -1;
    }


    printf("Started 2.\n");
    return 0;
}


void start(const FunctionCallbackInfo<Value>& args) {

    // Vamos a recuperar los parámetros
    Isolate *isolate=Isolate::GetCurrent();
    HandleScope scope(isolate);

    _start();
/*  Local<Object> bufferObj = args[0]->ToObject();
  char * bufferData=node::Buffer::Data(bufferObj);
  size_t bufferLength=node::Buffer::Length(bufferObj);
  Isolate* isolate = Isolate::GetCurrent();
  HandleScope scope(isolate);

//  printf("%s: %d\n",bufferData,(int)bufferLength);
  if (bufferLength!=GLOBAL_SIZE){
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "Buffer size does not match shared memory size")));
    return;
  }else
    memcpy(bufferData,k->globals,GLOBAL_SIZE);
  //strcpy(bufferData,"Hello from C++");
  //bufferData[0]='K';*/

}

void setSpeed(const FunctionCallbackInfo<Value> &args){
    Isolate *isolate=Isolate::GetCurrent();
    HandleScope scope(isolate);
    if (args.Length()<2){
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate,"Wrong number of arguments. Specify slaveId and target pos")));
        return;
    }
    if (!args[0]->IsNumber() || !args[1]->IsNumber()){
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate,"Please specify slaveId and target pos as numeric values")));
        return;
    }
    int slaveId=args[0]->NumberValue();
    double fSpeed=args[1]->NumberValue();
//    printf("setting target position for drive %d to: %d\n",slaveId,targetPosition);
    if (slaveId<0 || slaveId>=KNUMDRIVES){
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate,"Invalid drive id")));
        return;
    }
    slave_data[slaveId].fSpeed=fSpeed;
    slave_data[slaveId].moveType=0;
}


unsigned char testAddr[8];


void init(Handle<Object> exports) {
                         /* the defined mask, i.e. only 7. */
  NODE_SET_METHOD(exports, "start", start);
}

NODE_MODULE(ethercatNative, init)

/****************************************************************************/
