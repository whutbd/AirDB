#include "airdb_impl.h"
#include "binlog.h"
#include "leveldb/db.h"
#include "leveldb/status.h"
#include <assert.h>
#include <sys/utsname.h>
#include <boost/algorithm/string/replace.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/scoped_ptr.hpp>
#include <gflags/gflags.h>
#include <limits>
#include <unistd.h>
#include <algorithm>
#include <vector>
#include <sofa/pbrpc/pbrpc.h>
#include <iostream>

DECLARE_int32(max_cluster_size);
DECLARE_int32(vote_timeout_max);
DECLARE_int32(vote_timeout_min);
DECLARE_int32(heartbeat_delay);
DECLARE_string(binlog_dir);
DECLARE_string(db_data_dir);
DECLARE_bool(binlog_compress);

/*#define SCOPED_PTR(CLASS, INSTANCE, NAME) do {\
     boost::scoped_ptr<CLASS> NAME(INSTANCE);\
  } while(0) 
*/

const std::string tag_last_applied_index = "#TAG_LAST_APPLIED_INDEX#";

using leveldb::Status;
namespace airdb {


AirDBImpl::AirDBImpl(std::string& server_id, 
			const std::vector<std::string>& members) :
			binlogger_(NULL),
			stop_(false),
			status_(kFollower), 
			headbeat_cnt_(0), 
			current_term_(0), 
			self_addr_(server_id),
			commit_index_(-1),
                        last_applied_index_(-1),
                        data_store_(NULL){ 
    binlogger_ = new BinLogger(FLAGS_binlog_dir  + "/" + server_id,
    			     FLAGS_binlog_compress);
    commit_cond_ = new CondVar(&mu_);
    replica_cond_ = new CondVar(&mu_);
    std::vector<std::string>::const_iterator it = members.begin();
    for(; it != members.end(); ++it) {
        std::string addr = *it;
        LOG(INFO, "addr[%s]",addr.c_str());
    	all_server_addr_.push_back(*it);
    }
    std::string data_store_path = FLAGS_db_data_dir + "/" + server_id;
    LOG(INFO, "data_store_path[%s]", data_store_path.c_str());
    data_store_ = new StorageKV(data_store_path);
    if (data_store_ == NULL) {
        LOG(INFO, "data_store_ is null");
        return;
    }
    std::string tag_value;
    int ret = data_store_->Get(tag_last_applied_index, &tag_value);  
    if (ret == 0) {
        LOG(INFO, "tag_value[%S]", tag_value.c_str());
        last_applied_index_ =  BinLogger::StringToInt(tag_value);
    }
    commit_pool_.AddTask(boost::bind(&AirDBImpl::CommitIndex, this));
    LOG(INFO, "all server size is [%d]", all_server_addr_.size());
    LoopVoteLeader();
}

AirDBImpl::~AirDBImpl() {
    MutexLock lock(&mu_);
    if (binlogger_) {
    	delete binlogger_;
	binlogger_ = NULL;
    }
    stop_ = true;
    loop_vote_pool_.Stop(true);
    heart_beat_pool_.Stop(true);
    follower_work_pool_.Stop(true);
}

void AirDBImpl::CommitIndex() {
    MutexLock lock(&mu_);
    while (!stop_) {
        while (!stop_ && commit_index_ <= last_applied_index_) {
            commit_cond_->Wait();
        }
        if (stop_) {
            break;
        }
        int64_t start_idx = last_applied_index_;
        int64_t end_idx = commit_index_;
        mu_.Unlock();
        LOG(INFO, "begin to CommitIndex");
        for(int i = start_idx + 1; i <= end_idx; ++i) {
            LOG(INFO, "==== aaa ;binglog read i is [%d]===", i);
            BinLogEntry log_entry;
            std::string type_and_value;
            bool slot_ok = binlogger_->ReadSlot(i, &log_entry);
            assert(slot_ok);
            LOG(INFO, "====bbb===");
            switch(log_entry.op) {
                case kPutOp:
                     LOG(INFO, "beign commit put");
                     type_and_value.append(1, static_cast<char>(log_entry.op));
                     type_and_value.append(log_entry.value);
                     //Status s = data_store_->Put(log_entry.key, type_and_value);
                     data_store_->Put(log_entry.key, type_and_value);
                     LOG(INFO, "commit put success");
                    // assert(s == kOk);
                     break;
                default:
                     break;
            }
            mu_.Lock();
            if (status_ == kLeader && sdk_ack_.find(i) != sdk_ack_.end()) {
                SdkAck& ack = sdk_ack_[i];
                if (ack.put_res) {
                    ack.put_res->set_success(true);
                    ack.done->Run();
                }
                sdk_ack_.erase(i);
           }
           ++last_applied_index_;
           data_store_->Put(tag_last_applied_index, 
                                         BinLogger::IntToString(last_applied_index_));
           LOG(INFO, "==== last_applied_index_ is [%d]", last_applied_index_);
           mu_.Unlock();
        }
        mu_.Lock();
    }
}

int32_t AirDBImpl::GetRandTimeOut(){
    float range = FLAGS_vote_timeout_max - FLAGS_vote_timeout_min;
    return FLAGS_vote_timeout_min + (int32_t)(range * rand() / (RAND_MAX + 1.0));
}

void AirDBImpl::LoopVoteLeader() {
    if (stop_) {
        return;
    }
    int32_t vote_delay_time = GetRandTimeOut();
    loop_vote_pool_.DelayTask(vote_delay_time, 
    		   boost::bind(&AirDBImpl::VoteLeader, this));
}


void AirDBImpl::GetLocalLogTermIndex(int64_t* last_log_index, 
		int64_t* last_log_term) {
    binlogger_->GetLastLogIndexAndTerm(last_log_index, last_log_term);
}

void AirDBImpl::VoteLeader() {
    MutexLock lock(&mu_);
    if (status_ == kLeader) {
	LoopVoteLeader();
	return;
    }
    if (status_ == kFollower && headbeat_cnt_ > 0) {
        headbeat_cnt_ = 0;
	LoopVoteLeader();
	return;
    }
    ++current_term_;
    status_ = kCandidate;
    vote_for_[current_term_] = self_addr_;
    vote_pass_[current_term_]++;
    //get local last_log_index and last_log_term
    int64_t last_log_index = 0;
    int64_t last_log_term = 0;
    GetLocalLogTermIndex(&last_log_index, &last_log_term);
    //broadcast vote req to others server
    std::vector<std::string>::iterator iter = all_server_addr_.begin();
    for(; iter != all_server_addr_.end(); ++iter) {
    	std::string addr = *iter;
        LOG(INFO, "vote addr[%s]", addr.c_str());
        if (*iter == self_addr_) {
	    continue;
	}
	AirDB_Stub* stub;
	rpc_client_.GetStub(*iter, &stub);
	boost::scoped_ptr<AirDB_Stub> stub_guard(stub);
	//SCOPED_PTR(AirDB_Stub, stub, stub_ptr);
        VoteRequest* req = new VoteRequest();
        VoteResponse* res = new VoteResponse();
	req->set_term(current_term_);
	req->set_candidate_addr(self_addr_);
	req->set_last_log_index(last_log_index);
	req->set_last_log_term(last_log_term);
	boost::function<void (const VoteRequest*, VoteResponse*, bool, int)> callback;
	callback = boost::bind(&AirDBImpl::VoteCallBack, this, _1, _2, _3, _4);
	rpc_client_.AsyncRequest(stub, &AirDB_Stub::Vote, req, res, callback, 1, 3);
    	
    }
    //loop vote for HA
    LoopVoteLeader();
}

void AirDBImpl::Vote(google::protobuf::RpcController* controller,
		    const VoteRequest* req, VoteResponse* res,
		    google::protobuf::Closure* done) {
    MutexLock lock(&mu_);
    if (status_ == kLeader) {
        LOG(WARN, "the leader node do not vote");
	return;
    }
    if (req->term() < current_term_) {
    	//LOG(INFO, "req term[%d] < current_term_[%d]", req->term(), current_term_);
	res->set_term(current_term_);
	res->set_vote_pass(false);
	done->Run();
	return;
    }
    int64_t last_log_index;
    int64_t last_log_term;
    GetLocalLogTermIndex(&last_log_index, &last_log_term);
    if (req->last_log_term() < last_log_term) {
	res->set_term(current_term_);
	res->set_vote_pass(false);
	done->Run();
	return;
    } else if (req->last_log_term() == last_log_term && 
                req->last_log_index() < last_log_index) {
         res->set_term(current_term_);
         res->set_vote_pass(false);
         done->Run();
         return;
    }

    if (req->term() > current_term_) {
	SwitchToFollower(req->term());
    }
    if (vote_for_.find(current_term_) != vote_for_.end() && 
        vote_for_[current_term_] != req->candidate_addr()) {
	res->set_term(current_term_);
	res->set_vote_pass(false);
	done->Run();
	return;
   }
   vote_for_[current_term_] = req->candidate_addr();
   res->set_vote_pass(true);
   res->set_term(current_term_);
   done->Run();
   return;
}

void AirDBImpl::VoteCallBack(const VoteRequest* req, VoteResponse* res, 
			bool failed, int error) {
    MutexLock lock(&mu_);
    boost::scoped_ptr< const VoteRequest> req_ptr(req);
    boost::scoped_ptr<VoteResponse> res_ptr(res);
    //SCOPED_PTR(const VoteRequest, req, req_ptr);
    //SCOPED_PTR(VoteResponse, res, res_ptr);
    if (status_ == kCandidate && !failed) {
	int64_t others_term = res_ptr->term();
        if (others_term == current_term_ && res_ptr->vote_pass()) {
	    ++vote_pass_[current_term_];
 	    if (vote_pass_[current_term_] > (all_server_addr_.size() / 2)) {
	     	SwitchToLeader();
	    }
	} else {
	    if (others_term > current_term_) {
	    	SwitchToFollower(others_term);
	    }
	}
    }
    
    /*if (vote_pass_[current_term_] > (all_server_addr_.size() / 2)) {
	  LOG(INFO, "vote_pass_[%d], half addr_size[%d]",vote_pass_[current_term_], (all_server_addr_.size() / 2));
          SwitchToLeader();
    }*/
}

void AirDBImpl::SwitchToLeader() {
    status_ = kLeader;
    cur_leader_addr_ = self_addr_;
    //broadcast heartbeat
    heart_beat_pool_.AddTask(
    		boost::bind(&AirDBImpl::BroadCastHeartBeat, this));
    //start replication log
    LOG(INFO, "i will SwitchToLeader, and do StartReplicaLog");
    StartReplicaLog();
}

void AirDBImpl::StartReplicaLog(){
    mu_.AssertHeld();
    LOG(INFO, "StartReplicateLog");
    std::vector<std::string>::const_iterator it = all_server_addr_.begin();
    for(; it != all_server_addr_.end(); ++it) {
        if (*it == self_addr_) {
            continue;
        }
        if (replicating_.find(*it) != replicating_.end()){
            LOG(INFO, "there is another thread replicating on : %s",
                it->c_str());
            continue;
        }
        std::string follower_addr = *it;
        LOG(INFO, "StartReplicaLog [%s]",follower_addr.c_str());
        next_index_[follower_addr] = binlogger_->GetLength();
        LOG(INFO, "===== StartReplicaLog binlogger_[%d]", binlogger_->GetLength());
        match_index_[follower_addr] = -1;
        replica_pool_.AddTask(boost::bind(&AirDBImpl::ReplicateLog,
                                                 this, follower_addr));
    }
    //this is very import design for start_replica
    BinLogEntry log_entry;
    log_entry.key = "Ping";
    log_entry.value = "";
    log_entry.term = current_term_;
    log_entry.op = kNop;
    binlogger_->WriteEntry(log_entry);
}

void AirDBImpl::ReplicateLog(const std::string& follower_addr) {
    MutexLock lock(&mu_);
    replicating_.insert(follower_addr);
    bool latest_replicating_ok = true;
    while (!stop_ && status_ == kLeader) {
        while (!stop_ && binlogger_->GetLength() <= next_index_[follower_addr]) {
            replica_cond_->TimeWait(2000);
            if (status_ != kLeader) {
                break;
            }
        }
        if (stop_) {
            break;
        }
        if (status_ != kLeader) {
            LOG(INFO, "stop realicate log, no longger leader"); 
            break;
        }
        LOG(INFO, "===== next_index_[%s] is [%d]", follower_addr.c_str(), next_index_[follower_addr]);
        int64_t index = next_index_[follower_addr];
        int64_t cur_term = current_term_;
        int64_t prev_index = index - 1;
        int64_t prev_term = -1;
        int64_t cur_commit_index = commit_index_;
        int64_t batch_range = binlogger_->GetLength() - index;
        if (!latest_replicating_ok) {
            batch_range = std::min(1L, batch_range);
        }
        LOG(INFO, "batch_range[%d]", batch_range);
        int64_t max_term = -1;
        std::string leader_addr = self_addr_;
        BinLogEntry prev_log_entry;
        if (prev_index > -1) {
            bool slot_ok = binlogger_->ReadSlot(prev_index, &prev_log_entry);
            if (!slot_ok) {
                LOG(INFO, "bad slot [%ld], can't replicate on %s ", 
                prev_index, follower_addr.c_str());
                break;
            }
            prev_term = prev_log_entry.term;
        }
        mu_.Unlock();
        AirDB_Stub* stub;
        rpc_client_.GetStub(follower_addr, &stub);
        boost::scoped_ptr<AirDB_Stub> stub_guard(stub);
        AppendEntriesRequest request;
        AppendEntriesResponse response;
        request.set_term(cur_term);
        request.set_leader_id(leader_addr);
        LOG(INFO, "==== zz prev_index[%d]", prev_index);
        request.set_prev_log_index(prev_index);
        request.set_prev_log_term(prev_term);
        request.set_leader_commit_index(cur_commit_index);
        bool has_bad_slot = false;
        for (int64_t idx = index; idx < (index + batch_range); ++idx) {
            BinLogEntry log_entry;
            bool slot_ok = binlogger_->ReadSlot(idx, &log_entry);
            if (!slot_ok) {
                LOG(INFO, "bad slot at %ld", idx);
                has_bad_slot = true;
                break;
            }
            LOG(INFO, "==== add Entry to slaver====");
            Entry * entry = request.add_entries();
            entry->set_term(log_entry.term);
            entry->set_key(log_entry.key);
            entry->set_value(log_entry.value);
            entry->set_op(log_entry.op);
            max_term = std::max(max_term, log_entry.term);
        }
        if (has_bad_slot) {
            LOG(INFO, "there has bad slot");
            mu_.Lock();
            break;
        }
        LOG(INFO,"ReplicateLog[%s]", follower_addr.c_str());
        LOG(INFO,"req prev_log_index[%d]", request.prev_log_index());
        bool ret = rpc_client_.SendRequest(stub, &AirDB_Stub::AppendEntries,
                        &request,
                        &response, 60, 1);
        /*if (!ret) {
            continue;
        }*/
        mu_.Lock();
        mu_.AssertHeld();
        if (ret && response.current_term() > current_term_) {
            SwitchToFollower(response.current_term());
        }
        if (status_ != kLeader) {
            LOG(INFO, "stop realicate log, no longger leader"); 
            break;
        }
        /*if (!ret) {
            LOG(INFO, "rpc has error during realication");
            break;
        }*/
        LOG(INFO, "max term is [%d]", max_term);
        if (ret) {
            if (response.success()) {
                LOG(INFO, "replica[%s] success", follower_addr.c_str());
                next_index_[follower_addr] = index + batch_range;
                match_index_[follower_addr] = index + batch_range - 1;
                if (max_term == current_term_) {
                    //in one same term
                    UpdateCommitIndex(index + batch_range - 1);
                }
                latest_replicating_ok = true;
            } else if (response.is_busy()) {
                LOG(INFO, "res is busy");
                latest_replicating_ok = true;
            } else {
                next_index_[follower_addr] = std::min(next_index_[follower_addr] - 1, response.log_length());
                LOG(INFO, "there has node dead so long,so do replic");
                LOG(INFO, "addr[%s] response.log_length[%d] > next_index_[%d], so do replica", self_addr_.c_str(), response.log_length(), next_index_[follower_addr]);
                //next_index_[follower_addr] = response.log_length();
                if (next_index_[follower_addr] < 0) {
                    next_index_[follower_addr] = 0;
                }
            }
        } else {
            latest_replicating_ok = false;
            LOG(INFO, "faild to send replicate-rpc to %s ", follower_addr.c_str());
        }
        
    }
    replicating_.erase(follower_addr);
}


void AirDBImpl::UpdateCommitIndex(int64_t index) {
    mu_.AssertHeld();
    int match_cnt = 0;
    std::vector<std::string>::iterator it = all_server_addr_.begin();
    for(; it != all_server_addr_.end(); ++it) {
        std::string server_addr = *it;
        if (match_index_[server_addr] == index) {
            ++match_cnt;
        }
        if (match_cnt >= all_server_addr_.size() / 2 && index > commit_index_) {
            commit_index_ = index;
            LOG(INFO, "update to new commit index: %ld", commit_index_);
            commit_cond_->Signal();
        }
        LOG(INFO, "match_cnt is [%d]", match_cnt);
    }
    //changed
    /*if (match_cnt >= all_server_addr_.size() / 2 && index > commit_index_) {
        commit_index_ = index;
        LOG(INFO, "update to new commit index: %ld", commit_index_);
        commit_cond_->Signal();
     }*/
}


void AirDBImpl::BroadCastHeartBeat() {
    MutexLock lock(&mu_);
    if (stop_ || status_ != kLeader) {
        return ;
    }
    std::vector<std::string>::iterator iter = all_server_addr_.begin();
    for (; iter != all_server_addr_.end(); ++iter) {
    	if (*iter == self_addr_) {
	    continue;
	}
	//send heart beat req
	AirDB_Stub* stub;
	rpc_client_.GetStub(*iter, &stub);
	boost::scoped_ptr<AirDB_Stub> stub_guard(stub);
        AppendEntriesRequest* req = new AppendEntriesRequest();
	AppendEntriesResponse* res = new AppendEntriesResponse();
	//LOG(INFO, "BroadCastHeartBeat , current_term[%d]",current_term_);
	req->set_term(current_term_);
	//req->set_leader_id(cur_leader_addr_);
	req->set_leader_id(self_addr_);
	req->set_leader_commit_index(commit_index_);
	boost::function<void (const AppendEntriesRequest*, AppendEntriesResponse*, bool, int) > call_back;
	
	call_back = boost::bind(&AirDBImpl::HeartBeatCallBack, this, _1, _2, _3, _4);
	rpc_client_.AsyncRequest(stub, &AirDB_Stub::AppendEntries, req, res, call_back, 2, 1);

    }
    heart_beat_pool_.DelayTask(FLAGS_heartbeat_delay, 
    	boost::bind(&AirDBImpl::BroadCastHeartBeat, this));
}


void AirDBImpl::AppendEntries(::google::protobuf::RpcController* rpc, 
			  const AppendEntriesRequest* req, AppendEntriesResponse* res,
			  ::google::protobuf::Closure* done) {
    //the follower will  deal sdk put  request  and node heartbeat in a time
    // so use thread_pool
    follower_work_pool_.AddTask(boost::bind(&AirDBImpl::AppendEntriesImpl,
    		this, req, res, done));
    return ;
}
void AirDBImpl::AppendEntriesImpl(const AppendEntriesRequest* req, 
				 AppendEntriesResponse* res, 
			        ::google::protobuf::Closure* done) {

    MutexLock lock(&mu_);
    //LOG(INFO, "heartbeat recv term is [%d],my_term is [%d]", req->term(), current_term_);
    //sync term if the node dead so long
    if (req->term() >= current_term_) {
        status_ = kFollower;
        current_term_ = req->term();
    } else {
        LOG(INFO, "req_term[%d], current_term[%d]", req->term(), current_term_);
    	res->set_current_term(current_term_);
	res->set_success(false);
	res->set_log_length(binlogger_->GetLength());
        LOG(INFO, "recevie term is too old");
	done->Run();
	return;
    }
    if (status_ == kFollower) {
	cur_leader_addr_ = req->leader_id();
        ++headbeat_cnt_;
        //LOG(INFO, "entries_size[%d]", req->entries_size());
        if (req->entries_size() > 0) {
            LOG(INFO, "req->entries_size(%d)", req->entries_size());
            LOG(INFO, "req_prev_log_index[%d], binlogger_len[%d]", req->prev_log_index(), binlogger_->GetLength());
            if (req->prev_log_index() >= binlogger_->GetLength()){
                LOG(INFO, "[AppendEntries] prev log is beyond");
                res->set_current_term(current_term_);
                res->set_success(false);
                res->set_log_length(binlogger_->GetLength());
                done->Run();
                return;
            }
            int64_t prev_log_term = -1;
            if (req->prev_log_index() >= 0) {
                LOG(INFO, "==== prev_log_index > 0 ===");
                BinLogEntry prev_log_entry;
                bool slot_ok = binlogger_->ReadSlot(req->prev_log_index(), &prev_log_entry);
                assert(slot_ok);
                prev_log_term = prev_log_entry.term;
            }
            if (prev_log_term != req->prev_log_term()) {
                //this is very import
                //changed
                binlogger_->Truncate(req->prev_log_index() - 1);
                res->set_current_term(current_term_);
                res->set_success(false);
                res->set_log_length(binlogger_->GetLength());
                LOG(INFO, "[AppendEntries] term not match term: %ld,%ld", prev_log_term, req->prev_log_term());
                done->Run();
                return;
            }
            if (commit_index_ - last_applied_index_ 
                > 100000) {
                res->set_current_term(current_term_);
                res->set_success(false);
                res->set_log_length(binlogger_->GetLength());
                res->set_is_busy(true);
                LOG(INFO, "[AppendEntries] speed to fast, %ld > %ld",
                    req->prev_log_index(), last_applied_index_);
                done->Run();
                return;
            }
            if (binlogger_->GetLength() > req->prev_log_index() + 1) {
                int64_t old_length = binlogger_->GetLength();
                //changed
                //binlogger_->Truncate(req->prev_log_index() );
                LOG(INFO, "[AppendEntries] log length alignment, "
                        "length: %ld,%ld", old_length, req->prev_log_index());
            }
            mu_.Unlock();
            LOG(INFO, "====I will begin to WriteEntryList===");
            binlogger_->WriteEntryList(req->entries());
            mu_.Lock();

        }
        int64_t old_commit_index = commit_index_;
        //commit_index_ = std::min(binlogger_->GetLength() - 1, req->leader_commit_index());
        commit_index_ = std::min(binlogger_->GetLength() - 1, req->leader_commit_index());
        //LOG(INFO, "leader_commit_index[%d];commit_index[%d], old_commit_index[%d]", req->leader_commit_index(),commit_index_, old_commit_index);
        //LOG(INFO, "binloger len is [%d]", binlogger_->GetLength() - 1);
        if (commit_index_ > old_commit_index) {
            commit_cond_->Signal();
            LOG(DEBUG, "follower: update my commit index to :%ld", commit_index_);
        }
	res->set_current_term(current_term_); 
	res->set_success(true);
	done->Run();
    } else {
        LOG(INFO, "invalid status: %d", status_);
        abort();
    }
    return;
}


void AirDBImpl::SwitchToFollower(int64_t term) {
    //MutexLock lock(&mu_);
    /*if (stop_) {
    	LOG(INFO, "the node stop");
	return;
    }*/
    LOG(INFO, "SwitchToFollower ,to term[%d]", term);
    status_ = kFollower;
    current_term_ = term;
}

void AirDBImpl::ShowStatus(::google::protobuf::RpcController* rpc,
			   const ShowStatusRequest* req, 
	                   ShowStatusResponse* res, ::google::protobuf::Closure* done) {
    MutexLock lock(&mu_);
    int64_t last_log_index = 0;
    int64_t last_log_term = 0;
    GetLocalLogTermIndex(&last_log_index, &last_log_term);
    LOG(INFO, "last_log_index[%d] , last_log_term[%d]", last_log_index, last_log_term);
    res->set_status(status_);
    res->set_term(current_term_);
    res->set_last_log_index(last_log_index);
    res->set_last_log_term(last_log_term);
    res->set_commit_index(commit_index_);
    res->set_last_applied(last_applied_index_);
    done->Run();
}

void AirDBImpl::HeartBeatCallBack(const AppendEntriesRequest* req, 
		AppendEntriesResponse* res, bool filed, int error) {
    MutexLock lock(&mu_);
    if (status_ != kLeader) {
    	LOG(WARN, "now the node is not leader");
	return;
    }
    if (!filed) {
        int64_t res_current_term = res->current_term();
    	if (res_current_term > current_term_) {
            LOG(INFO, "HeartBeatCallBack leader will SwitchToFollower");
	    SwitchToFollower(res_current_term);
	}
    }
}

void AirDBImpl::Put(google::protobuf::RpcController* controller,
		    const PutRequest* req, PutResponse* res,
		    google::protobuf::Closure* done) {
    LOG(INFO, "this is put");
    MutexLock lock(&mu_);
    if (status_ != kLeader) {
	LOG(INFO,"1 is no longer leader");
        res->set_success(false);
	done->Run();
        return;
    }
    BinLogEntry log_entry;
    log_entry.key = req->key();
    log_entry.value = req->value();
    log_entry.op = kPutOp;
    log_entry.term = current_term_;
    binlogger_->WriteEntry(log_entry);
    int64_t cur_index = binlogger_->GetLength() - 1;
    SdkAck& ack = sdk_ack_[cur_index];
    ack.done = done;
    ack.put_res = res;
    //notify do replica
    replica_cond_->Broadcast();
    return ;
}

void AirDBImpl::Get(google::protobuf::RpcController* controller,
                    const GetRequest* req, GetResponse* res,
                    google::protobuf::Closure* done) {
    LOG(INFO, "this is get");
    int64_t start_time = common::get_micros();
    MutexLock lock(&mu_);
    if (status_ != kLeader) {
        res->set_hit(false);
        res->set_leader_id(cur_leader_addr_);
        res->set_success(false);
        done->Run();
        return;
    }
    //get value from leader
    std::string key = req->key();
    std::string value;
    int ret = data_store_->Get(key, &value);
    if (ret != 0) {
        LOG(INFO, "get [%s] not exist", key.c_str());
        return;
    }
    /*if (s != kOk) {
        res->set_hit(false);
        res->set_success(false);
        res->set_leader_id(self_addr_);
    }*/
    std::string real_value;
    BinLogOperation op;
    ParseValue(value, op, real_value);
    if (op == kLockOp) {
        res->set_hit(false);
        res->set_success(false);
        res->set_leader_id(self_addr_);
    }
    else if (op == kPutOp) {
        LOG(INFO ,"get success, real_value is [%s]", real_value.c_str());
        res->set_hit(true);
        res->set_value(real_value);
        res->set_success(true);
        res->set_leader_id(self_addr_);
    } else {
        res->set_hit(false);
        res->set_success(false);
        res->set_leader_id(self_addr_);
    }
    int64_t end_time = common::get_micros();
    done->Run();
    LOG(INFO, "GET cost[%d](ms)", (end_time - start_time) / 1000);
    return;
}

void AirDBImpl::ParseValue(const std::string& value,
                BinLogOperation& op, 
                std::string& real_value) {
    if (value.size() >= 1) {
        op = static_cast<BinLogOperation>(value[0]);
        real_value = value.substr(1);
    }
}


}//end namespace


