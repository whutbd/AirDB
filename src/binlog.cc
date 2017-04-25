#include "binlog.h"
#include <sstream> 
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sstream> 
#include <string>
#include "log.h"
#include "mutex.h"

using common::Mutex;
using common::MutexLock;

namespace airdb {

const std::string log_dbname = "#binlog";
const std::string length_tag = "#BINLOG_LEN#";

static int Mkdirs(const char *dir) {
    /*char path_buf[1024];
    const int dir_mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
    char *p = NULL;
    size_t len = 0;
    snprintf(path_buf, sizeof(path_buf), "%s", dir);
    len = strlen(path_buf);
    if(path_buf[len - 1] == '/') {
        path_buf[len - 1] = 0;
    }
    for(p = path_buf + 1; *p; p++) {
        if(*p == '/') {
            *p = 0;
            int status = mkdir(path_buf, dir_mode);
            if (status != 0 && errno != EEXIST) {
                return false;
            }
            *p = '/';
        }
    }
    int status = mkdir(path_buf, dir_mode);
    if (status != 0 && errno != EEXIST) {
        return -1;
    }*/
    mkdir(dir, 0755);
    return 0;
}


BinLogger::~BinLogger() {
    delete db_;
}

BinLogger::BinLogger(const std::string& data_dir,
                     bool compress,
                     int32_t block_size,
                     int32_t write_buffer_size) : db_(NULL),
                                                  length_(0),
                                                  last_log_term_(-1) {
    LOG(INFO, "data_dir is [%s]", data_dir.c_str());
    int ret = Mkdirs(data_dir.c_str());
    if (ret != 0) {
        LOG(WARN, "failed to create dir :%s", data_dir.c_str());
        abort();
    }
    std::string full_name = data_dir + "/" + log_dbname;
    leveldb::Options options;
    options.create_if_missing = true;
    if (compress) {
        options.compression = leveldb::kSnappyCompression;
        LOG(INFO, "enable snappy compress for binlog");
    } else {
    	LOG(INFO, "snappy compress not for binlog");
    }
    options.write_buffer_size = write_buffer_size;
    options.block_size = block_size;
    LOG(INFO, "[binlog]: block_size: %d, writer_buffer_size: %d", 
        options.block_size,
        options.write_buffer_size);
    leveldb::Status status = leveldb::DB::Open(options, full_name, &db_);
    if (!status.ok()) {
        LOG(WARN, "failed to open db %s err %s", 
                   full_name.c_str(), status.ToString().c_str()); 
        assert(status.ok());
    }
    std::string value;
    status = db_->Get(leveldb::ReadOptions(), length_tag, &value);
    if (status.ok() && !value.empty()) {
        length_ = StringToInt(value);
        if (length_ > 0) {
            BinLogEntry log_entry;
            bool slot_ok = ReadSlot(length_ - 1, &log_entry);
            assert(slot_ok);
            last_log_term_ = log_entry.term;
        }
    }
}

void BinLogger::Truncate(int64_t trunk_slot_index) {
    if (trunk_slot_index < -1) {
        trunk_slot_index = -1;
    }
    LOG(INFO, "====BinLogger Truncate======");
    {
        MutexLock lock(&mu_);
        length_ = trunk_slot_index + 1;
        leveldb::Status status = db_->Put(leveldb::WriteOptions(), 
                                          length_tag, IntToString(length_));
        assert(status.ok());
        if (length_ > 0) {
            BinLogEntry log_entry;
            bool slot_ok = ReadSlot(length_ - 1, &log_entry);
            assert(slot_ok);
            last_log_term_ = log_entry.term;
        }
    }
}

std::string BinLogger::IntToString(int64_t num) {
    std::stringstream ss;
    ss << num;
    return ss.str();
}

int64_t BinLogger::StringToInt(const std::string& s) {
    std::stringstream ss;
    int num;
    ss << s;
    ss >> num;
    return num;
}

bool BinLogger::ReadSlot(int64_t slot_index, BinLogEntry* log_entry) {
    std::string value;
    std::string key = IntToString(slot_index);
    leveldb::Status status = db_->Get(leveldb::ReadOptions(), key, &value);
    if (status.ok()) {
        LoadBinLogEntry(value, log_entry);
        return true;
    } else if (status.IsNotFound()) {
        return false;
    }
    abort();
}

void BinLogger::DumpBinLogEntry(const BinLogEntry& log_entry, std::string* buf) {
    assert(buf);
    int32_t total_len = sizeof(uint8_t) 
                        + sizeof(int32_t) + log_entry.key.size()
                        + sizeof(int32_t) + log_entry.value.size()
                        + sizeof(int64_t);
    buf->resize(total_len);
    int32_t key_size = log_entry.key.size();
    int32_t value_size = log_entry.value.size();
    char* p = reinterpret_cast<char*>(& ((*buf)[0]));
    p[0] = static_cast<uint8_t>(log_entry.op);
    p += sizeof(uint8_t);
    memcpy(p, static_cast<const void*>(&key_size), sizeof(int32_t));
    p += sizeof(int32_t);
    memcpy(p, static_cast<const void*>(log_entry.key.data()), log_entry.key.size());
    p += log_entry.key.size();
    memcpy(p, static_cast<const void*>(&value_size), sizeof(int32_t));
    p += sizeof(int32_t);
    memcpy(p, static_cast<const void*>(log_entry.value.data()), log_entry.value.size());
    p += log_entry.value.size();
    memcpy(p, static_cast<const void*>(&log_entry.term), sizeof(int64_t));
}


void BinLogger::LoadBinLogEntry(const std::string& buf, BinLogEntry* log_entry) {
    assert(log_entry);  
    const char* p = buf.data();
    int32_t key_size = 0;
    int32_t value_size = 0;
    uint8_t opcode = 0;
    memcpy(static_cast<void*>(&opcode), p, sizeof(uint8_t));
    log_entry->op = static_cast<BinLogOperation>(opcode);
    p += sizeof(uint8_t);
    memcpy(static_cast<void*>(&key_size), p, sizeof(int32_t));
    log_entry->key.resize(key_size);
    p += sizeof(int32_t);
    memcpy(static_cast<void*>(&log_entry->key[0]), p, key_size);
    p += key_size;
    memcpy(static_cast<void*>(&value_size), p, sizeof(int32_t));
    log_entry->value.resize(value_size);
    p += sizeof(int32_t);
    memcpy(static_cast<void*>(&log_entry->value[0]), p, value_size);
    p += value_size;
    memcpy(static_cast<void*>(&log_entry->term), p , sizeof(int64_t));
}



int BinLogger::GetLastLogIndexAndTerm(int64_t* last_log_index, int64_t* last_log_term) {
    MutexLock lock(&mu_);
    *last_log_index = length_ - 1;
    *last_log_term = last_log_term_;
    return 0;
}

int64_t BinLogger::GetLength() {
    MutexLock lock(&mu_);
    return length_;
}

void BinLogger::WriteEntry(const BinLogEntry& log_entry) {
    //if not lick is bug
    std::string log_str;
    DumpBinLogEntry(log_entry, &log_str);
    {
        //MutexLock lock(&mu_);
        std::string cur_index = IntToString(length_);
        std::string next_index = IntToString(length_ + 1);

        leveldb::WriteBatch batch;
        batch.Put(cur_index, log_str);
        batch.Put(length_tag, next_index);
        leveldb::Status status = db_->Write(leveldb::WriteOptions(), &batch);
        assert(status.ok());
        ++length_;
        last_log_term_ = log_entry.term;
    }
}

void BinLogger::WriteEntryList(const ::google::protobuf::RepeatedPtrField<Entry> &entries) {
    LOG(INFO, "====BinLogger WriteEntryList======");
    MutexLock lock(&mu_);
    leveldb::WriteBatch batch;
    int64_t cur_index = length_;
    std::string next_index = IntToString(length_ + entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        BinLogEntry log_entry;
        std::string log_str;
        log_entry.op = entries.Get(i).op();
        log_entry.key = entries.Get(i).key();
        log_entry.value = entries.Get(i).value();
        log_entry.term = entries.Get(i).term();
        last_log_term_ = log_entry.term;
        DumpBinLogEntry(log_entry, &log_str);
        batch.Put(IntToString(cur_index + i), log_str);
    }
    batch.Put(length_tag, next_index);
    leveldb::Status status = db_->Write(leveldb::WriteOptions(), &batch);
    assert(status.ok());
    length_ += entries.size();
    LOG(INFO, "==== BinLogger len[%d]", length_);
}

void BinLogger::RemoveSlotBefore(int64_t slot_gc_index) {
    db_->SetNexusGCKey(slot_gc_index);
} 

}
