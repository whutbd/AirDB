#ifndef BINLOG_H_
#define BINLOG_H_
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <boost/function.hpp>
#include "proto/airdb_node.pb.h"
#include "leveldb/write_batch.h"
#include "leveldb/db.h"
#include "mutex.h"

using common::Mutex;

namespace airdb {
struct BinLogEntry {
    BinLogOperation op;
    std::string key;
    std::string value;
    int64_t term;
    BinLogEntry() : op(kNop), key(""), value("") {
    }
};

class BinLogger {
public:
    BinLogger(const std::string& data_dir,
              bool compress = false,
              int32_t block_size = 32748,
              int32_t write_buffer_size = 33554432);

    ~BinLogger();
    int GetLastLogIndexAndTerm(int64_t* last_log_index, int64_t* last_log_term);
    bool ReadSlot(int64_t slot_index, BinLogEntry* log_entry);    
    int64_t GetLength();
    static int64_t StringToInt(const std::string& s);
    static std::string IntToString(int64_t num);
    void Truncate(int64_t trunc_slot_index);
    void LoadBinLogEntry(const std::string& buf, BinLogEntry* log_entry);
    void DumpBinLogEntry(const BinLogEntry& log_entry, std::string* buf);
    void RemoveSlotBefore(int64_t slot_gc_index);
    void WriteEntry(const BinLogEntry& log_entry);
    void WriteEntryList(const ::google::protobuf::RepeatedPtrField<Entry> &entries);
private:
    leveldb::DB* db_;
    int64_t length_;
    int64_t last_log_term_;
    Mutex mu_;
};

}

#endif
