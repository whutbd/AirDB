# AirDB
  AirDB是一个基于Raft协议实现的高可用的分布式Key-Value数据库，支持数据增删改查和分布式锁，可用于大型分布式系统的协调工作
# 简介
* AirDB是一个类似于Google Chubby一样的组件，能提供分布式锁、寻址并能存储一定规模的元数据，AirDB采用写Binlog的方式来记录用户操作并采用追binlog的方式来追平之前遗漏的操作，采用Raft协议解决了多个节点之间的数据变更一致性同步问题，使多个节点构成一个高可用的数据存储集群
* 在原有单机存储引擎LevelDB基础上新增了按范围进行compaction接口实现binlog的过期删除防止不必要的磁盘空间浪费
# 读写性能
AirDB设计思想实现的是CP，尽最大努力提高A
* 读4.8W 
* 写9K
# 谈谈paxos, multi-paxos, raft
 * multi-paxos, raft 都是对一堆连续的问题达成一致的协议, 而paxos 是对一个问题达成一致的协议, 因此multi-paxos, raft 其实都是为了简化paxos 在多个问题上面达成一致的需要的两个阶段, 因此都简化了prepare 阶段, 提出了通过有leader 来简化这个过程. multi-paxos, raft 只是简化不一样, raft 让用户的log 必须是有序, 选主必须是有日志最全的节点, 而multi-paxos 没有这些限制. 因此raft 的实现会更简单<br>
  raft 是基于对multi paxos 的两个限制形成的,其一发送的请求的是连续的, 也就是说raft 的append 操作必须是连续的. 而paxos 可以并发的. (其实这里并发只是append log 的并发提高, 应用的state machine 还是必须是有序的);其二主是有限制的, 必须有最新, 最全的日志节点才可以当选. 而multi-paxos 是随意的 所以raft 可以看成是简化版本的multi paxos(这里multi-paxos 因为允许并发的写log, 因此不存在一个最新, 最全的日志节点, 因此只能这么做. 这样带来的麻烦就是选主以后, 需要将主里面没有的log 给补全, 并执行commit 过程)

