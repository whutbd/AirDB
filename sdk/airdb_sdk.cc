#include "airdb_sdk.h"
#include<vector>
#include "proto/airdb_node.pb.h"
#include <boost/scoped_ptr.hpp>
#include <boost/bind.hpp>

namespace airdb {

AirDBSdk::AirDBSdk(std::vector<std::string>& members) {
    server_members_ = members;
    rpc_client_ = new RpcClient();
}

AirDBSdk::~AirDBSdk() {
    delete rpc_client_;
}


int AirDBSdk::ParseFlagFromArgs(int argc, char** argv,
		std::vector<std::string>& members) {
    google::ParseCommandLineFlags(&argc, &argv, true);
    boost::split(members, FLAGS_cluster_members,
                 boost::is_any_of(","), boost::token_compress_on);
    if (members.size() < 1) {
        LOG(WARN, "invalid cluster size");
        abort();
    }
}

int AirDBSdk::Put(const std::string& key, const std::string& value) {
    std::vector<std::string>::const_iterator it ;
    for (it = server_members_.begin(); it != server_members_.end(); ++it) {
        std::string server_id = *it;
        AirDB_Stub* stub;
        rpc_client_->GetStub(server_id, &stub);
        boost::scoped_ptr<AirDB_Stub> stub_guard(stub);
        PutRequest request;
        PutResponse response;
        request.set_key(key);
        request.set_value(value);
        bool ok = rpc_client_->SendRequest(stub, &AirDB_Stub::Put,
                                               &request, &response, 2, 1);
        if (!ok) {
            LOG(INFO, "rpc failed");
            continue;
        }
        //if one server res success stand for put success
        if (response.success()) {
            return 0;
        }
    }
    return -1;
}


int AirDBSdk::Get(const std::string& key, std::string* value) {
    std::vector<std::string>::const_iterator it ;
    for (it = server_members_.begin(); it != server_members_.end(); ++it) {
        std::string server_id = *it;
        AirDB_Stub* stub;
        rpc_client_->GetStub(server_id, &stub);
        boost::scoped_ptr<AirDB_Stub> stub_guard(stub);
        GetRequest request;
        GetResponse response;
        request.set_key(key);
        bool ok = rpc_client_->SendRequest(stub, &AirDB_Stub::Get, &request, &response, 2, 1);
        if (!ok) {
            LOG(INFO, "rpc failed");
            continue;
        }
        if (response.hit() && response.success()) {
            *value = response.value();
            return 0;
        }
    }
    return -1;
}



int AirDBSdk::ShowClusterStatus(std::vector<NodeInfo>& cluster_info) {
    std::vector<std::string>::iterator iter = server_members_.begin();
    for (; iter!= server_members_.end(); ++iter) {
	NodeInfo  node_info;
	node_info.server_id = *iter;
	AirDB_Stub* stub;
	rpc_client_->GetStub(*iter, &stub);
	boost::scoped_ptr<AirDB_Stub> stub_guard(stub);
        //SCOPED_PTR(AirDB_Stub, stub, stub_ptr);
	ShowStatusRequest request;
	ShowStatusResponse response;
	bool ok = rpc_client_->SendRequest(stub, &AirDB_Stub::ShowStatus, 
	                                           &request, &response, 1, 1);
       if (ok) {
           node_info.status = response.status();
           node_info.current_term = response.term();
           node_info.last_log_index = response.last_log_index();
           node_info.last_log_term = response.last_log_term();
           node_info.commit_index = response.commit_index();
           node_info.last_applied_index = response.last_applied();
       } else {
           std::cout << "========= call ShowStatus happen error ====" << std::endl;
           node_info.status = kOffline;
           node_info.current_term = -1;
           node_info.last_log_index = -1;
           node_info.last_log_term = -1;
           node_info.commit_index = -1;
           node_info.last_applied_index = -1;
       }

	cluster_info.push_back(node_info);
    }
    return 0;
}

std::string AirDBSdk::StatusToString(int32_t status) {
    switch (status) {
        case kLeader:
            return "Leader";
            break;
        case kCandidate:
            return "Candidate";
            break;
        case kFollower:
            return "Follower";
            break;
        case kOffline:
            return "Offline";
            break;
    }
    return "UnKnown";
}

}//end namespace 
