#include "eventqueue/eventqueue.h"

#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <atomic>
#include <filesystem>

#include <json/json.h>

#include <modbus/modbus.h>
#include <mosquitto.h>

#include <unistd.h>


namespace fs = std::filesystem;

static std::atomic<bool> s_KeepRunning;
static modbus_t* ctx;
static events::EventQueue EventQueue;

struct mosquitto* mqtt;
static int reconnects = 0;

static inline int asInt( Json::Value& item )
{
    return item.isString() ?
        std::stoi(item.asString(), nullptr, 0) :
        item.asInt();
}

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
    union {
        uint16_t u16[2] ;
        int16_t i16[2] ;
        uint32_t u32;
        int32_t i32;
    } reads;

    Json::Value pub;
    const size_t regcnt = desc["modbus-regcnt"].asInt();
    const float factor = desc["factor"].asFloat();
    const bool regsigned = desc["modbus-signed"].asBool();
    const auto address = desc["modbus-address"].isString() ?
                        std::stoi(desc["modbus-address"].asString(), nullptr, 0) :
                        desc["modbus-address"].asInt();

    int result = modbus_read_input_registers( ctx, address, regcnt, reads.u16 );
    if( result > 0 )
    {
        Json::StreamWriterBuilder wbuilder;
        wbuilder.settings_["precision"] = 7;

        if( result == 1 )
        {
            float val = regsigned ? reads.i16[0] : reads.u16[0];
            pub[desc["attr"].asCString()] = val * factor;
        }
        if( result == 2 )
        {
            float val = regsigned ? (float)reads.i32 : (float)reads.u32;
            pub[desc["attr"].asCString()] = val * factor;
        }

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
    printf("usage: [-c] config-file\n");
    exit(1);
}

int main( int argc, char* argv[] )
{
    bool verbose = false;
    mosquitto_lib_init();
    s_KeepRunning = true;
    signal(SIGINT,my_handler);

    std::string config_filename = "/usr/local/etc/smartmeter/config.json";

    signed char ch;
    while ((ch = getopt(argc, argv, "c:hv")) != -1) {
        switch (ch) {
        case 'v':
            verbose = true;
            break;
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

    std::string devbasename = mb_con["port"].asString();

    auto module_name = mb_con["modbus-name"].asString();
    auto module_name_address = asInt( mb_con["modbus-address"] );

    std::cout << "Name " << module_name << std::endl;
    std::cout << "Address " << module_name_address << std::endl;

    bool found = false;
    for( auto const& dev : fs::directory_iterator{"/dev"} ) {

        if( found || !s_KeepRunning ) break;

        auto devname = dev.path().string();
        if( verbose ) std::cout << "Trying " << devname << " <-> "  << devbasename << std::endl;

        if( !(devname.rfind( devbasename ) != std::string::npos)
            || (devname.rfind( ".lock" ) != std::string::npos)
            || (devname.rfind( ".init" ) != std::string::npos)
            || !dev.is_character_file() ) {
            if( verbose ) std::cout << " No match, trying next " << std::endl;
            continue;
        }

        std::cout << "Try to connect " << dev.path() << " via modbus-rtu" << std::endl;

        ctx = modbus_new_rtu( dev.path().string().c_str(),
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
            modbus_close(ctx);
            modbus_free(ctx);
            std::cerr << "Unable to connect modbus" << std::endl;
            return 2;
        }

        {
            union {
                char c[14];
                uint8_t u8[14];
                uint16_t u16[7];
            }reads;

            int cnt = 5;
            while( s_KeepRunning ) {

                int result = modbus_read_registers( ctx, module_name_address, 7, reads.u16 );
                if( result < 0 ) {
                    if( cnt-- < 0 ) {
                        std::cerr << "Could not connect to "
                            << devname
                            << ", closing modbus and try next"
                            << std::endl;
                        modbus_close( ctx );
                        modbus_free( ctx );
                        ctx = nullptr;
                        break;
                    } else {
                        std::cerr << "Error during Read, try again" << std::endl;
                        sleep(1);
                        continue;
                    }
                }

                std::string mod_name(reads.c, sizeof(reads.c));
                std::cout << "Found Module " << mod_name << std::endl;
                if( reads.c != module_name ) {
                    std::cerr << "-" << mod_name
                        << " does not match configured name "
                        << module_name << std::endl;
                    modbus_close( ctx );
                    modbus_free( ctx );
                    ctx = nullptr;
                } else {
                    std::cout << "-" << mod_name << " is configured, continue" << std::endl;
                    found = true;
                }
                break;
            }
        }
    }

    if( ctx == nullptr ){
        std::cerr << "No port found, abort" << std::endl;
        return 6;
    }

    auto mb_regs = root["modbus-configuration"]["registers"];
    for( auto& array_index : mb_regs ) {
        EventQueue.call(query_registers, array_index );
    }

    mosquitto_loop_start( mqtt );
    while( s_KeepRunning ) EventQueue.dispatch( 1 );
    mosquitto_loop_stop( mqtt, true );

    modbus_close(ctx);
    modbus_free(ctx);

    mosquitto_destroy( mqtt );
    mosquitto_lib_cleanup();
    return 0;
}
