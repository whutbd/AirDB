#include "storage_kv.h"

#include <assert.h>
#include <gflags/gflags.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include "common/logging.h"
#include "leveldb/db.h"
#include "leveldb/status.h"
#include "log.h"

DECLARE_bool(db_data_compress);
DECLARE_int32(db_data_block_size);
DECLARE_int32(db_data_write_buffer_size);

using common::Mutex;
using common::MutexLock;
using leveldb::Status;
namespace airdb {

//const std::string StorageManager::anonymous_user = "";

StorageKV::StorageKV(const std::string& data_dir) : data_dir_(data_dir) {
    /*bool ok = common::Mkdirs(data_dir.c_str());
    if (!ok) {
        LOG(FATAL, "failed to create dir :%s", data_dir.c_str());
        abort();
    }*/
    mkdir(data_dir_.c_str(), 0755);
    LOG(INFO, "mkdir[%s]", data_dir_.c_str());
    // Create default database for shared namespace, i.e. anonymous user
    //std::string full_name = data_dir_ + "/@db";
    std::string full_name = data_dir_ ;
    leveldb::Options options;
    options.create_if_missing = true;
    if (FLAGS_db_data_compress) {
        options.compression = leveldb::kSnappyCompression;
        LOG(INFO, "enable snappy compress for data storage");
    }
    options.write_buffer_size = FLAGS_db_data_write_buffer_size * 1024 * 1024;
    options.block_size = FLAGS_db_data_block_size * 1024;
    LOG(INFO, "[data]: block_size: %d, writer_buffer_size: %d", 
        options.block_size,
        options.write_buffer_size);
    leveldb::DB* default_db = NULL;
    leveldb::Status status = leveldb::DB::Open(options, full_name, &default_db);
    assert(status.ok());
    db_ = default_db;
}

StorageKV::~StorageKV() {
    MutexLock lock(&mu_);
    if (db_ !=NULL) {
        delete db_;
        db_ = NULL;
    }
}

int StorageKV::Get(const std::string& key,
                           std::string* value) {
    if (value == NULL) {
        return -1;
    }
    MutexLock lock(&mu_);
    leveldb::Status status = db_->Get(leveldb::ReadOptions(), key, value);
    return (status.ok() ? 0 : -1);
}

int StorageKV::Put(const std::string& key,
                           const std::string& value) {
    MutexLock lock(&mu_);
    leveldb::Status status = db_->Put(leveldb::WriteOptions(), key, value);
    return (status.ok() ? 0 : -1);
}

int StorageKV::Delete(const std::string& key) {
    MutexLock lock(&mu_);
    leveldb::Status status = db_->Delete(leveldb::WriteOptions(), key);
    return (status.ok() ? 0 : -1);
}

}
