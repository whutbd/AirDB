#include "airdb_sdk.h"
#include "leveldb/db.h"
#include "timer.h"
#include <gflags/gflags.h>
#include <iostream>
#include <boost/lexical_cast.hpp>
#include "common/tprinter.h"
#include <iostream>


using airdb::AirDBSdk;

DECLARE_string(airdb_cmd);
DECLARE_string(airdb_key);
DECLARE_string(airdb_value);
DECLARE_string(airdb_target_addr);

//log config
config::ConfigParser g_cfg;
std::string log_path = "./cli-log/";
std::string log_name = "cli";

int main(int argc, char** argv) {
    int64_t start_time = common::get_micros();
    //int64_t start_time = 0;
    std::vector<std::string> members;
    AirDBSdk::ParseFlagFromArgs(argc, argv, members);
    AirDBSdk sdk(members);
    if (FLAGS_airdb_cmd == "show") {
    	common::TPrinter cprinter(7);
	cprinter.AddRow(7, "server node", "role", "term", "last_log_index",
	                           "last_log_term", "commit_index", "last_applied");
	std::vector<airdb::NodeInfo> cluster_info;
	sdk.ShowClusterStatus(cluster_info);
	if (cluster_info.size() == 0) {
	    return -1;
	}
	std::vector<airdb::NodeInfo>::iterator it;
	for (it = cluster_info.begin(); it != cluster_info.end(); ++it) {
	    std::string s_status = airdb::AirDBSdk::StatusToString(it->status);
	    cprinter.AddRow(7, it->server_id.c_str(), s_status.c_str(),
                        boost::lexical_cast<std::string>(it->current_term).c_str(),
                        boost::lexical_cast<std::string>(it->last_log_index).c_str(),
                        boost::lexical_cast<std::string>(it->last_log_term).c_str(),
                        boost::lexical_cast<std::string>(it->commit_index).c_str(),
                        boost::lexical_cast<std::string>(it->last_applied_index).c_str());
	}
	std::cout << cprinter.ToString();
    } else if (FLAGS_airdb_cmd == "put") {
        std::string key = FLAGS_airdb_key;
        std::string value = FLAGS_airdb_value;
        if (sdk.Put(key, value) == 0) {
            LOG(INFO, "put key[%s], value[%s] success", key.c_str(), value.c_str());
        } else {
           LOG(WARN, "put key[%s], value[%s] failed", key.c_str(), value.c_str());
        }
    } else if (FLAGS_airdb_cmd == "get") {
        std::string key = FLAGS_airdb_key;
        std::string value;
        if (sdk.Get(key, &value) == 0) {
            std::cout << "key:" << key  <<";value:" << value << std::endl;
        } else {
             std::cout << "get failed " << std::endl;
        }
    } else if (FLAGS_airdb_cmd == "del") {
        std::string del_key = FLAGS_airdb_key;
        int ret = sdk.Delete(del_key);
        if (ret == 0) {
            std::cout << "del key[" << del_key << "] success" << std::endl;
            return 0;
        } else {
            std::cout << "del key[" << del_key << "] failed" << std::endl;
            return -1;
        }
    } 
    
    else {
	std::cout << " the cmd not support " << std::endl;
    }
    int64_t end_time = common::get_micros();
    std::cout << "time used:" << (end_time - start_time) / 1000 << "(ms)" << std::endl;
    return 0;
}
