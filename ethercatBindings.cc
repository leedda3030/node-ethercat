/*
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
#include <stdint.h>
#include <unistd.h>
#include <sched.h>
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

#define LOOP_PERIOD_NS 1000000


/****************************************************************************/

// EtherCAT
static ec_master_t *master = NULL;
static ec_master_state_t master_state ;

static ec_domain_t *domain1 = NULL;
static ec_domain_state_t domain1_state ;
static bool initialised=false;

/*static ec_slave_config_t *sc_ana_in = NULL;
static ec_slave_config_state_t sc_ana_in_state = {};

static ec_slave_config_t *sc_rta_x = NULL;
static ec_slave_config_state_t sc_rta_x_state = {};*/

/****************************************************************************/

// process data
static uint8_t *domain1_pd = NULL;


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
    ecrt_domain_process(domain1);
    ecrt_domain_queue(domain1);
}


/****************************************************************************/

int _start()
{
    printf("Requesting master 0\n");
    master = ecrt_request_master(0);
    if (!master)
        return -1;
    printf("Master 0 obtained\n");
    domain1 = ecrt_master_create_domain(master);
    if (!domain1)
        return -1;

    printf("Configuring PDOs...\n");
    printf("PDOs configured\n");
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


}

void addSlave(const FunctionCallbackInfo<Value>&args){
    Isolate *isolate=Isolate::GetCurrent();
    HandleScope scope(isolate);

}


void init(Handle<Object> exports) {
                         /* the defined mask, i.e. only 7. */

  NODE_SET_METHOD(exports,"addSlave",addSlave);
  NODE_SET_METHOD(exports, "start", start);

}

NODE_MODULE(ethercat, init)

/****************************************************************************/
