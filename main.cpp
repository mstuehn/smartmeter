#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <atomic>

#include "json/json.h"

#include <modbus/modbus.h>
#include <mosquitto.h>

#include <unistd.h>

#include "eventqueue/eventqueue.h"

static std::atomic<bool> s_KeepRunning;
static modbus_t* ctx;
static events::EventQueue EventQueue;

struct mosquitto* mqtt;
static int reconnects = 0;

std::string random_string( size_t length )
{
    auto randchar = []() -> char
    {
        const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
        const size_t max_index = (sizeof(charset) - 1);
        return charset[ rand() % max_index ];
    };
    std::string str(length,0);
    std::generate_n( str.begin(), length, randchar );
    return str;
}

void my_handler( int s ){
    std::cout << "Stopping smartmeter" << std::endl;
    s_KeepRunning = false;
    EventQueue.break_dispatch();
}

static void query_registers( Json::Value& desc )
{
    Json::Value pub;
    uint16_t regs[2];

    int result = modbus_read_input_registers( ctx, desc["modbus-address"].asInt(), 2, regs );
    if( result == 2 )
    {
        Json::StreamWriterBuilder wbuilder;
        pub[desc["attr"].asCString()] = modbus_get_float_dcba( regs );

        std::string msg = Json::writeString(wbuilder, pub);
        std::string topic = desc["mqtt-publish"].asCString();

        mosquitto_publish( mqtt, nullptr,
                           topic.c_str(),
                           msg.length(),
                           msg.c_str(), 0, false );
    }

    EventQueue.call_in( desc["poll"].asInt(), query_registers, desc );
}

static void __attribute__((noreturn))
usage(void)
{
    printf("usage: %s [-c] config-file\n", getprogname());
	exit(1);
}

int main( int argc, char* argv[] )
{
    mosquitto_lib_init();
    s_KeepRunning = true;
    signal(SIGINT,my_handler);

    std::string config_filename = "/usr/local/etc/smartmeter/config.json";

    signed char ch;
    while ((ch = getopt(argc, argv, "c:h")) != -1) {
        switch (ch) {
        case 'c':
		config_filename = std::string(optarg);
            break;
        case 'h':
        default:
            usage();
            break;
        }
    }
    argc -= optind;
    argv += optind;

    std::ifstream config( config_filename, std::ifstream::in );
    Json::Value root;

    try{
        config >> root;
    } catch( std::exception& e)
    {
        std::cerr << "Cannot parse json" << std::endl;
        return 1;
    }

    mqtt = mosquitto_new( random_string(7).c_str(), true, nullptr );
    if( mqtt == nullptr ) {
        std::cerr << "unable to init mqtt" << std::endl;
        return (2);
    }

    auto mqtt_con = root["mqtt-configuration"];
    int result = mosquitto_connect( mqtt,
            mqtt_con["server"].asCString(),
            mqtt_con["port"].asInt(),
            60 );
    if( result < 0 ) return 5;

    auto mb_con = root["modbus-configuration"]["connection"];
    ctx = modbus_new_rtu( mb_con["port"].asCString(),
                          mb_con["speed"].asInt(),
                          *(mb_con["parity"].asCString()),
                          mb_con["bits"].asInt(),
                          mb_con["stopbits"].asInt());
    if( ctx == nullptr ) {
        std::cerr << "Failed to init modbus ctx" << std::endl;
        return 1;
    }

    modbus_set_slave( ctx, mb_con["slave"].asInt() );
    modbus_set_debug( ctx, mb_con["debug"].asBool() );

    if( modbus_connect( ctx ) < 0  ) {
        std::cerr << "Unable to connect modbus" << std::endl;
        return 2;
    }

    auto mb_regs = root["modbus-configuration"]["registers"];
    for( auto& array_index : mb_regs ) {
        EventQueue.call(query_registers, array_index );
    }

    mosquitto_loop_start( mqtt );
    while( s_KeepRunning ) EventQueue.dispatch( 1 );
    mosquitto_loop_stop( mqtt, true );

    modbus_close(ctx);

    mosquitto_lib_cleanup();
    return 0;
}
