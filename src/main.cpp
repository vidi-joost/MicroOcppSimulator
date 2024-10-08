// matth-x/MicroOcppSimulator
// Copyright Matthias Akstaller 2022 - 2024
// GPL-3.0 License

#include <iostream>

#include <MicroOcpp.h>
#include "evse.h"
#include "api.h"
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>

#if MO_NUMCONNECTORS == 3
std::array<Evse, MO_NUMCONNECTORS - 1> connectors {{1,2}};
#else
std::array<Evse, MO_NUMCONNECTORS - 1> connectors {{1}};
#endif

bool g_isOcpp201 = false;

#define MO_NETLIB_MONGOOSE 1
#define MO_NETLIB_WASM 2


#if MO_NETLIB == MO_NETLIB_MONGOOSE
#include "mongoose.h"
#include <MicroOcppMongooseClient.h>

#include "net_mongoose.h"

struct mg_mgr mgr;
MicroOcpp::MOcppMongooseClient *osock;

#elif MO_NETLIB == MO_NETLIB_WASM
#include <emscripten.h>

#include <MicroOcpp/Core/Connection.h>

#include "net_wasm.h"

MicroOcpp::Connection *conn = nullptr;

#else
#error Please ensure that build flag MO_NETLIB is set as MO_NETLIB_MONGOOSE or MO_NETLIB_WASM
#endif

/*
 * Setup MicroOcpp and API
 */
void load_ocpp_version(std::shared_ptr<MicroOcpp::FilesystemAdapter> filesystem) {

    MicroOcpp::configuration_init(filesystem);

    #if MO_ENABLE_V201
    {
        auto protocolVersion_stored = MicroOcpp::declareConfiguration<const char*>("OcppVersion", "1.6", SIMULATOR_FN, false, false, false);
        MicroOcpp::configuration_load(SIMULATOR_FN);
        if (!strcmp(protocolVersion_stored->getString(), "2.0.1")) {
            //select OCPP 2.0.1
            g_isOcpp201 = true;
            return;
        }
    }
    #endif //MO_ENABLE_V201

    g_isOcpp201 = false;
}

void app_setup(MicroOcpp::Connection& connection, std::shared_ptr<MicroOcpp::FilesystemAdapter> filesystem) {
    mocpp_initialize(connection,
            g_isOcpp201 ?
                ChargerCredentials::v201("MicroOcpp Simulator", "MicroOcpp") :
                ChargerCredentials("MicroOcpp Simulator", "MicroOcpp"),
            filesystem,
            false,
            g_isOcpp201 ?
                MicroOcpp::ProtocolVersion{2,0,1} :
                MicroOcpp::ProtocolVersion{1,6}
            );

    for (unsigned int i = 0; i < connectors.size(); i++) {
        connectors[i].setup();
    }
}

/*
 * Execute one loop iteration
 */
void app_loop() {
    mocpp_loop();
    for (unsigned int i = 0; i < connectors.size(); i++) {
        connectors[i].loop();
    }
}

#if MO_NETLIB == MO_NETLIB_MONGOOSE

struct dnsConfig {
    const char* dns4;
    const char* dns6;
};

dnsConfig get_dns_config() {
    dnsConfig config = {
        .dns4 = NULL,
        .dns6 = NULL
    };

    if(res_init() == 0)
    {
        for(int i = 0; i < MAXNS; i++)
        {
            sockaddr_in resolver = _res.nsaddr_list[i];

            char name[INET6_ADDRSTRLEN];
            char port[10];
            char full[INET6_ADDRSTRLEN + 20];
            full[0] = '\0';

            switch(resolver.sin_family) {
                case AF_INET:
                    getnameinfo((sockaddr*) &resolver, sizeof(sockaddr_in), name, sizeof(name), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
                    if(config.dns4 == NULL) {
                        strcat(full, "udp://");
                        strcat(full, name);
                        strcat(full, ":");
                        strcat(full, port);
                        config.dns4 = strdup(full);
                        printf("IP4: %s\n", config.dns4);
                    }
                    break;
                case AF_INET6:
                    getnameinfo((sockaddr*) &resolver, sizeof(sockaddr_in6), name, sizeof(name), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
                    if(config.dns6 == NULL)
                    {
                        strcat(full, "udp://[");
                        strcat(full, name);
                        strcat(full, "]:");
                        strcat(full, port);
                        config.dns6 = strdup(full);
                        printf("IP6: %s\n", config.dns4);
                    }

                    break;
                default:
                    printf("Unknown AF\n");
            }
        }
    }

    return config;
}

int main() {
    mg_log_set(MG_LL_INFO);                            
    mg_mgr_init(&mgr);

    auto dnsConfig = get_dns_config();
    if(dnsConfig.dns4) {
        mgr.dns4.url = dnsConfig.dns4;
    }
    if(dnsConfig.dns6) {
        mgr.dns6.url = dnsConfig.dns6;
    }

    mg_http_listen(&mgr, "0.0.0.0:8000", http_serve, NULL);     // Create listening connection

    auto filesystem = MicroOcpp::makeDefaultFilesystemAdapter(MicroOcpp::FilesystemOpt::Use_Mount_FormatOnFail);

    load_ocpp_version(filesystem);

    osock = new MicroOcpp::MOcppMongooseClient(&mgr,
        "ws://echo.websocket.events",
        "charger-01",
        "",
        "",
        filesystem,
        g_isOcpp201 ?
            MicroOcpp::ProtocolVersion{2,0,1} :
            MicroOcpp::ProtocolVersion{1,6}
        );

    server_initialize(osock);
    app_setup(*osock, filesystem);

    for (;;) {                    // Block forever
        mg_mgr_poll(&mgr, 100);
        app_loop();
    }

    delete osock;
    mg_mgr_free(&mgr);
    return 0;
}

#elif MO_NETLIB == MO_NETLIB_WASM

int main() {

    printf("[WASM] start\n");

    auto filesystem = MicroOcpp::makeDefaultFilesystemAdapter(MicroOcpp::FilesystemOpt::Deactivate);

    conn = wasm_ocpp_connection_init(nullptr, nullptr, nullptr);

    app_setup(*conn, filesystem);

    const int LOOP_FREQ = 10; //called 10 times per second
    const int BLOCK_INFINITELY = 0; //0 for non-blocking execution, 1 for blocking infinitely
    emscripten_set_main_loop(app_loop, LOOP_FREQ, BLOCK_INFINITELY);

    printf("[WASM] setup complete\n");
}
#endif
