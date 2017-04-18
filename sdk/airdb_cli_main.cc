#include <iostream>
#include "airdb_sdk.h"
#include "leveldb/db.h"
#include <gflags/gflags.h>
#include <boost/lexical_cast.hpp>
#include "common/tprinter.h"
#include <iostream>
#include "timer.h"

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
    int64_t zz_end_time = common::get_micros();
    std::cout << "sdk init time used:" << (zz_end_time - start_time) / 1000 << "(ms)" << std::endl;
    AirDBSdk sdk(members);
    if (FLAGS_airdb_cmd == "show") {
    	common::TPrinter cprinter(7);
	cprinter.AddRow(7, "server node", "role", "term", "last_log_index",
	                           "last_log_term", "commit_index", "last_applied");
	std::vector<airdb::NodeInfo> cluster_info;
	sdk.ShowClusterStatus(cluster_info);
	if (cluster_info.size() == 0) {
	    std::cout << "=== size is 0 "<< std::endl;
	    return -1;
	}
	std::cout << "cluster status size is " << cluster_info.size() << std::endl;
	//LOG(INFO, "cluster status size is [%d]", cluster_info.size());
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
            std::cout << "put success" << std::endl;
            //LOG(INFO, "put key[%s], value[%s] success", key.c_str(), value.c_str());
        } else {
            std::cout << "put failed" << std::endl;
            //LOG(INFO, "put key[%s], value[%s] failed", key.c_str(), value.c_str());
        }
    } else if (FLAGS_airdb_cmd == "get") {
        std::string key = FLAGS_airdb_key;
        std::string value;
        std::cout << "get key is " << key << std::endl;
        if (FLAGS_airdb_target_addr != "addr") {
            //get value by leveldb
            std::cout << "==== airdb_target_addr is " << FLAGS_airdb_target_addr << std::endl;
            //std::string data_path = "data/" + FLAGS_airdb_target_addr + "/@db";
            //std::string data_path = "data/127.0.1.1\:8881@db";
            std::string data_path = "/tmp/testdb";
            leveldb::DB* db_ptr = NULL;
            leveldb::Options options;
            options.create_if_missing = true;
            //leveldb::Status status = leveldb::DB::Open(options, "/tmp/testdb", &db_ptr);
            leveldb::Status status = leveldb::DB::Open(options, "/root/ps/bin/data/127.0.1.1:8881/@db", &db_ptr);
            assert(status.ok()); 
            /*if (db_ptr == NULL){
                std::cout << "data is=" << data_path << "=" << std::endl;
                std::cout << "leveldb open failed " << std::endl;
                //return -1;
            }*/
            db_ptr->Get(leveldb::ReadOptions(), key, &value);
            std::cout << "get target addr:" << data_path << ";value is " << value << std::endl;
            //std::cout << "get target addr:" << FLAGS_airdb_target_addr << ";value is " << value << std::endl;
            return 0;
        }
        start_time = common::get_micros();
        if (sdk.Get(key, &value) == 0) {
            std::cout << "key:" << key  <<";value:" << value << std::endl;
        } else {
             std::cout << "get failed " << std::endl;
        }
    } else {
	std::cout << " the cmd not support " << std::endl;
    }
    int64_t end_time = common::get_micros();
    std::cout << "time used:" << (end_time - start_time) / 1000 << "(ms)" << std::endl;
    return 0;
}
