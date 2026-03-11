NodeMgr
1.查找节点 ✔
2.遍历节点 ✔
3.添加节点 ✔
4.删除节点 ✔
5.同步节点 ✔

TapLan
1.先不考虑桥接，专注实现服务端-客户端模型
2.考虑桥接，那就不要用服务端

提升性能小技巧
1.增大qlen，可以有效减小发送方丢包。
sudo ip link set TapLan qlen 10000

2.增大udp缓冲区，比如下面是增大到128M，在宿主机执行，可以有效减小接收方丢包。
sudo sysctl -w net.core.rmem_max=134217728
sudo sysctl -w net.core.rmem_default=134217728

3.执行上面两步后，目前遇到的问题猜测是发送方能力不足导致的带宽瓶颈。
ip -s link show <接口名称> # 查看指定网卡丢包
nstat -az | grep -i udp # 查看系统UDP数据包统计