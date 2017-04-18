#ifndef AIRDB_SDK_H_
#define AIRDB_SDK_H_
#include <string>
#include <stdint.h>
#include <gflags/gflags.h> 
#include <boost/algorithm/string.hpp>
#include <boost/scoped_ptr.hpp>
#include "rpc_client.h"
#include "log.h"
#include "proto/airdb_node.pb.h"
#include "common/macro.h"

DECLARE_string(cluster_members);
DECLARE_string(airdb_cmd);


namespace airdb {

struct NodeInfo{
    std::string server_id;
    DBNodeStatus status;
    int64_t current_term;
    int64_t last_log_index;
    int64_t last_log_term;
    int64_t commit_index;
    int64_t last_applied_index;
};

class AirDBSdk {

public:
    AirDBSdk(std::vector<std::string>& members);
    ~AirDBSdk();
    int Put(const std::string& key, const std::string& value);
    int Get(const std::string& key, std::string* value);
    int static ParseFlagFromArgs(int argc, char** argv, 
    		std::vector<std::string>& memers);
    static std::string StatusToString(int32_t status);
    int ShowClusterStatus(std::vector<NodeInfo>& cluster_info);
private:
    RpcClient* rpc_client_;
    std::vector<std::string> server_members_;
};

};


#endif
