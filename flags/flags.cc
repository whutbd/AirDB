#include <gflags/gflags.h>

DEFINE_string(cluster_members, "", "cluster members , e.g. abc.com:1234,def.com:3456");
DEFINE_int32(server_id, 1, "the offset in cluster members of this node");
DEFINE_int32(max_cluster_size, 10, "maximum size of ins cluster");
DEFINE_int32(vote_timeout_max, 100, "max vote timeout (ms)");
DEFINE_int32(vote_timeout_min, 200, "min vote timeout (ms)");
DEFINE_int32(heartbeat_delay, 50, "min vote timeout (ms)");
DEFINE_bool(binlog_compress, true, "min vote timeout (ms)");
DEFINE_bool(db_data_compress, true, "enable snappy compression on leveldb storage");
DEFINE_int32(db_data_block_size, 4, "for data, leveldb block_size, KB");
DEFINE_int32(db_data_write_buffer_size, 4, "for data, leveldb write_buffer_size, MB");

//binlog
DEFINE_string(binlog_dir, "binlog", "write-ahead log directory path");

//data
DEFINE_string(db_data_dir, "data", "local directory which store pesistent information");

//sdk
DEFINE_string(airdb_cmd, "show", "get or put or show");
DEFINE_string(airdb_key, "key", "key");
DEFINE_string(airdb_value, "value", "value");
DEFINE_string(airdb_target_addr, "addr", "127.0.0.1:8881");

//DEFINE_string(cluster_members, "", "cluster members , e.g. abc.com:1234,def.com:3456");
//DEFINE_string(airdb_data_dir, "data", "local directory which store pesistent information");
//DEFINE_string(airdb_log_file, "stdout", "filename of log file");
//DEFINE_int32(airdb_log_size, 1024, "max size of single log file");
//DEFINE_int32(airdb_log_total_size, 10240, "max size of all log file");
//DEFINE_int32(server_id, 1, "the offset in cluster members of this node");
//DEFINE_int32(airdb_max_throughput_in, -1, "max input throughput, MB");
//DEFINE_int32(airdb_max_throughput_out, -1, "max output throughput, MB");
//sdk
//DEFINE_string(airdb_cmd, "", "the command of inc shell");
