#ifndef STORAGE_KV_H_
#define STORAGE_KV_H_

#include <string>
#include <map>
#include <boost/function.hpp>
#include "common/mutex.h"
#include "leveldb/db.h"
#include "leveldb/status.h"
#include "proto/airdb_node.pb.h"

using leveldb::Status;

namespace airdb {

class StorageKV {
public:
    StorageKV(const std::string& data_dir);
    ~StorageKV();

    int Get(const std::string& key, std::string* value);
    int Put(const std::string& key, const std::string& value);
    int Delete(const std::string& key);

private:
    Mutex mu_;
    std::string data_dir_;
    leveldb::DB* db_;
};

}

#endif

