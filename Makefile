# OPT ?= -O2 -DNDEBUG     # (A) Production use (optimized mode)
OPT ?= -g2 -Wall -fPIC  -std=c++0x       # (B) Debug mode, w/ full line-level debugging symbols
# OPT ?= -O2 -g2 -DNDEBUG # (C) Profiling mode: opt, but w/debugging symbols

# Thirdparty
SNAPPY_PATH=./thirdparty/
PROTOBUF_PATH=./thirdparty/
PROTOC_PATH=./thirdparty/bin/
PROTOC=$(PROTOC_PATH)protoc
PBRPC_PATH =./thirdparty/
#BOOST_PATH ?=./thirdparty/boost/
GFLAGS_PATH=./thirdparty
NEXUS_LDB_PATH=./thirdparty
GTEST_PATH=./thirdparty
PREFIX=/usr/local/
DEPENDS=./depends/

INCLUDE_PATH = -I./ -I./common/ -I./proto/ -I./sdk/ -I./src/\
	       -I$(SNAPPY_PATH)/include\
               -I$(PREFIX)/include\
               -I$(PROTOBUF_PATH)/include \
               -I$(PBRPC_PATH)/include \
               -I$(SNAPPY_PATH)/include \
               -I$(GFLAGS_PATH)/include \
               -I$(DEPENDS)/include \
	       -I./lib/log/ \
	       -I./lib/common/ \
	       -I./thirdparty/leveldb/include \
	 

LDFLAGS =  -L$(PROTOBUF_PATH)/lib \
          -L$(PBRPC_PATH)/lib -lsofa-pbrpc -lprotobuf \
          -L$(SNAPPY_PATH)/lib -lsnappy \
	  -L./lib/ -llog \
          -L$(GFLAGS_PATH)/lib -lgflags \
          -Lthirdparty/leveldb/ -lleveldb\
          -L$(NEXUS_LDB_PATH) -lleveldb -L$(PREFIX)/lib \
          -lrt -lz -lpthread

LDFLAGS_SO = -L$(DEPENDS)/lib -L$(PROTOBUF_PATH)/lib \
          -L$(PBRPC_PATH)/lib -Wl,--whole-archive -lsofa-pbrpc -lprotobuf -Wl,--no-whole-archive \
          -L$(SNAPPY_PATH)/lib -lsnappy \
          -L$(GFLAGS_PATH)/lib -Wl,--whole-archive -lgflags -Wl,--no-whole-archive \
          -L$(NEXUS_LDB_PATH) -lleveldb -L$(PREFIX)/lib \
          -lz -lpthread

CXXFLAGS += $(OPT)

PROTO_FILE = $(wildcard proto/*.proto)
PROTO_SRC = $(patsubst %.proto,%.pb.cc,$(PROTO_FILE))
PROTO_HEADER = $(patsubst %.proto,%.pb.h,$(PROTO_FILE))
PROTO_OBJ = $(patsubst %.proto,%.pb.o,$(PROTO_FILE))

SVR_SRC = $(wildcard src/*.cc)
SVR_OBJ = $(patsubst %.cc, %.o, $(SVR_SRC))
SVR_HEADER = $(wildcard src/*.h)

COMMON_SRC = $(wildcard common/*.cc)
COMMON_OBJ = $(patsubst %.cc, %.o, $(COMMON_SRC))
COMMON_HEADER = $(wildcard common/*.h)


CLI_SRC = $(wildcard sdk/*.cc)
CLI_OBJ = $(patsubst %.cc, %.o, $(CLI_SRC))
CLI_HEADER = $(wildcard sdk/*.h)

FLAGS_OBJ = $(patsubst %.cc, %.o, $(wildcard flags/flags.cc))	
COMMON_OBJ = $(patsubst %.cc, %.o, $(wildcard common/*.cc))

OBJS = $(FLAGS_OBJ) $(COMMON_OBJ) $(PROTO_OBJ)
BIN = airdb cli

all: $(BIN)

#nexus_ldb=/home/tera/ins/thirdparty/leveldb/libleveldb.a 
#nexus_ldb: 
#	cd ./thirdparty/leveldb && make
# Depends
#$(_OBJ): $(PROTO_HEADER)


cli: $(CLI_OBJ)  $(OBJS)
	#@echo "===="$<
	$(CXX) $(CLI_OBJ)  $(OBJS) -o $@ $(LDFLAGS)

# Targets
airdb: $(SVR_OBJ)  $(OBJS)
	#@echo "===="$<
	$(CXX) $(SVR_OBJ)  $(OBJS) -o $@ $(LDFLAGS)

%.o: %.cc
	#@echo "===="$<
	$(CXX) $(CXXFLAGS) $(INCLUDE_PATH) -c $< -o $@

%.pb.h %.pb.cc: %.proto
    	#@echo "=====aa==="
	$(PROTOC) --proto_path=./proto/ --proto_path=/usr/local/include --cpp_out=./proto/ $<

.PHONY:clean
clean:
	rm -rf $(BIN) $(LIB) $(TESTS) $(PY_LIB) $(OBJS)
	rm -rf $(INS_OBJ) $(INS_CLI_OBJ) $(SAMPLE_OBJ) $(SDK_OBJ) $(TEST_OBJ) $(UTIL_OBJ)
	rm -rf $(PROTO_SRC) $(PROTO_HEADER)
	cp cli ./bin/
	rm -rf output/
.PHONY:install
install:
	cp ./airdb ./bin/
	cp ./cli ./bin/

