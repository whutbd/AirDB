#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <signal.h>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <assert.h>
#include <algorithm>
#include <vector>
#include <gflags/gflags.h>
#include <sofa/pbrpc/pbrpc.h>
#include "config.h"
#include "log.h"
#include "airdb_impl.h"
#include <iostream>
//config::ConfigParser g_cfg;

DECLARE_int32(max_cluster_size);
DECLARE_int32(vote_timeout_max);
DECLARE_int32(vote_timeout_min);
DECLARE_int32(server_id);
DECLARE_string(cluster_members);


config::ConfigParser g_cfg;
std::string log_path = "./zz-log/";
std::string log_name = "airdb";

static volatile bool s_quit = false;

static void SignalIntHandler(int sig){
    s_quit = true;
}

int main(int argc, char** argv) {
    google::ParseCommandLineFlags(&argc, &argv, false);
    
    //handle signal
    signal(SIGINT, SignalIntHandler);
    signal(SIGTERM, SignalIntHandler);
    //rpc 
    sofa::pbrpc::RpcServerOptions options;
    options.work_thread_num = 8;
    //options.max_throughput_in = -1;
    //options.max_throughput_out = -1;
    sofa::pbrpc::RpcServer rpc_server(options);
    
    std::vector<std::string> members;
    std::cout << FLAGS_cluster_members << std::endl;
    boost::split(members, FLAGS_cluster_members,
                    boost::is_any_of(","), boost::token_compress_on);
    std::string server_id = members.at(FLAGS_server_id - 1);
    log_name = log_name + "." + server_id;
    std::cout << "=============" << log_name << std::endl;
    //log init
    g_cfg.SetLogPath(log_path);
    g_cfg.SetLogName(log_name);
    LOG_INIT();
    std::cout << "server_id is " << server_id << std::endl;
    airdb::AirDBImpl* db_node = new airdb::AirDBImpl(server_id, members);
    if (db_node == NULL) {
        std::cout << "============ NULL ======" << std::endl;
        return -1;
    }
    if (!rpc_server.RegisterService(static_cast<airdb::AirDBImpl*>(db_node))) {
        LOG(WARN, "failed to register ins_node service");
        exit(-1);
    }
    std::cout << "==== start server_id is " << server_id << std::endl;
    if (!rpc_server.Start(server_id)) {
        LOG(WARN, "failed to start server on %s", server_id.c_str());
        exit(-2);
    }
    while (!s_quit) {
        usleep(1000);
    }
    return 0;
}


