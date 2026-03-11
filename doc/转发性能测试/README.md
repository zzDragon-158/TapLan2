# Linux服务端与客户端之间性能测试
测试异步IO接口(io_uring)的转发性能，测试基于下面这个版本。
```shell
$ git checkout 6a78bd4d187783f5252baca4ec2494aec9cfe6bc
```

## 1. 搭建测试环境
我的CPU是12th Gen Intel(R) Core(TM) i5-12400。  
使用Docker Desktop创建两个archlinux容器，容器共享winch wsl的linux内核。  
在两个容器中分别运行下面两条命令来启动程序的服务端和客户端。
```shell
# 启动服务端
$ sudo TapLan --aio

# 启动客户端
$ sudo TapLan --aio -c 172.17.0.2:3460
```

## 2. 测试过程

### 2.1 默认环境(不修改任何系统参数)
```shell
$ iperf3 -c 192.168.208.1 -t 10 -i 1
Connecting to host 192.168.208.1, port 5201
[  5] local 192.168.208.2 port 53424 connected to 192.168.208.1 port 5201
[ ID] Interval           Transfer     Bitrate         Retr  Cwnd
[  5]   0.00-1.00   sec   161 MBytes  1.35 Gbits/sec  260    173 KBytes
[  5]   1.00-2.00   sec   154 MBytes  1.29 Gbits/sec   59    233 KBytes
[  5]   2.00-3.00   sec   181 MBytes  1.52 Gbits/sec  219    257 KBytes
[  5]   3.00-4.00   sec   153 MBytes  1.28 Gbits/sec   75    205 KBytes
[  5]   4.00-5.00   sec   190 MBytes  1.59 Gbits/sec  245    171 KBytes
[  5]   5.00-6.00   sec   165 MBytes  1.39 Gbits/sec  157    193 KBytes
[  5]   6.00-7.00   sec   206 MBytes  1.73 Gbits/sec  248    241 KBytes
[  5]   7.00-8.00   sec   201 MBytes  1.68 Gbits/sec  153    173 KBytes
[  5]   8.00-9.00   sec   192 MBytes  1.61 Gbits/sec  142    181 KBytes
[  5]   9.00-10.00  sec   173 MBytes  1.45 Gbits/sec  162    221 KBytes
- - - - - - - - - - - - - - - - - - - - - - - - -
[ ID] Interval           Transfer     Bitrate         Retr
[  5]   0.00-10.00  sec  1.73 GBytes  1.49 Gbits/sec  1720            sender
[  5]   0.00-10.00  sec  1.73 GBytes  1.49 Gbits/sec                  receiver

iperf Done.
```

### 2.2 增大系统限制最大UDP缓存区大小为128M
```shell
# 宿主机上运行，我这里是winch wsl
$ sudo sysctl -w net.core.rmem_max=134217728
net.core.rmem_max = 134217728
```

```shell
$ iperf3 -c 192.168.208.1 -t 10 -i 1
Connecting to host 192.168.208.1, port 5201
[  5] local 192.168.208.2 port 34756 connected to 192.168.208.1 port 5201
[ ID] Interval           Transfer     Bitrate         Retr  Cwnd
[  5]   0.00-1.00   sec   384 MBytes  3.22 Gbits/sec    0   3.86 MBytes
[  5]   1.00-2.00   sec   394 MBytes  3.31 Gbits/sec    0   3.86 MBytes
[  5]   2.00-3.00   sec   378 MBytes  3.17 Gbits/sec    0   3.86 MBytes
[  5]   3.00-4.00   sec   374 MBytes  3.13 Gbits/sec    0   3.86 MBytes
[  5]   4.00-5.00   sec   384 MBytes  3.22 Gbits/sec    0   3.86 MBytes
[  5]   5.00-6.00   sec   379 MBytes  3.18 Gbits/sec    0   3.86 MBytes
[  5]   6.00-7.00   sec   373 MBytes  3.13 Gbits/sec    0   3.86 MBytes
[  5]   7.00-8.00   sec   384 MBytes  3.22 Gbits/sec    0   3.86 MBytes
[  5]   8.00-9.00   sec   392 MBytes  3.28 Gbits/sec    0   3.86 MBytes
[  5]   9.00-10.00  sec   383 MBytes  3.21 Gbits/sec    0   3.86 MBytes
- - - - - - - - - - - - - - - - - - - - - - - - -
[ ID] Interval           Transfer     Bitrate         Retr
[  5]   0.00-10.00  sec  3.74 GBytes  3.21 Gbits/sec    0            sender
[  5]   0.00-10.01  sec  3.74 GBytes  3.21 Gbits/sec                  receiver

iperf Done.
```

### 2.3 开启大页内存
```shell
# 程序申请的大页内存大小为2MB
# 在容器内运行程序前执行下面命令开启大页内存
$ echo 8 > sudo /proc/sys/vm/nr_hugepages
```

```shell
$ iperf3 -c 192.168.208.1 -t 10 -i 1
Connecting to host 192.168.208.1, port 5201
[  5] local 192.168.208.2 port 40588 connected to 192.168.208.1 port 5201
[ ID] Interval           Transfer     Bitrate         Retr  Cwnd
[  5]   0.00-1.00   sec   367 MBytes  3.08 Gbits/sec    0   3.80 MBytes
[  5]   1.00-2.00   sec   366 MBytes  3.07 Gbits/sec    0   3.80 MBytes
[  5]   2.00-3.00   sec   366 MBytes  3.07 Gbits/sec    0   3.80 MBytes
[  5]   3.00-4.00   sec   388 MBytes  3.25 Gbits/sec    0   3.80 MBytes
[  5]   4.00-5.00   sec   376 MBytes  3.15 Gbits/sec    0   3.80 MBytes
[  5]   5.00-6.00   sec   391 MBytes  3.28 Gbits/sec    0   3.80 MBytes
[  5]   6.00-7.00   sec   386 MBytes  3.24 Gbits/sec    0   3.80 MBytes
[  5]   7.00-8.00   sec   389 MBytes  3.26 Gbits/sec    0   3.80 MBytes
[  5]   8.00-9.00   sec   385 MBytes  3.23 Gbits/sec    0   3.80 MBytes
[  5]   9.00-10.00  sec   376 MBytes  3.14 Gbits/sec    0   3.80 MBytes
- - - - - - - - - - - - - - - - - - - - - - - - -
[ ID] Interval           Transfer     Bitrate         Retr
[  5]   0.00-10.00  sec  3.70 GBytes  3.18 Gbits/sec    0            sender
[  5]   0.00-10.01  sec  3.70 GBytes  3.18 Gbits/sec                  receiver

iperf Done.
```

### 2.4 顺便测一下同步IO的性能
```shell
# 启动程序的参数去掉"--aio"，程序就会使用同步IO(传统BSD套接字API)来进行转发。
$ iperf3 -c 192.168.208.1 -t 10 -i 1
Connecting to host 192.168.208.1, port 5201
[  5] local 192.168.208.2 port 46958 connected to 192.168.208.1 port 5201
[ ID] Interval           Transfer     Bitrate         Retr  Cwnd
[  5]   0.00-1.00   sec   255 MBytes  2.14 Gbits/sec    0   3.72 MBytes
[  5]   1.00-2.00   sec   260 MBytes  2.18 Gbits/sec    0   3.72 MBytes
[  5]   2.00-3.00   sec   261 MBytes  2.19 Gbits/sec    0   3.72 MBytes
[  5]   3.00-4.00   sec   261 MBytes  2.19 Gbits/sec    0   3.72 MBytes
[  5]   4.00-5.00   sec   256 MBytes  2.15 Gbits/sec    0   3.72 MBytes
[  5]   5.00-6.00   sec   261 MBytes  2.19 Gbits/sec    0   3.72 MBytes
[  5]   6.00-7.00   sec   259 MBytes  2.18 Gbits/sec    0   3.72 MBytes
[  5]   7.00-8.00   sec   260 MBytes  2.18 Gbits/sec    0   3.72 MBytes
[  5]   8.00-9.00   sec   260 MBytes  2.18 Gbits/sec    0   3.72 MBytes
[  5]   9.00-10.00  sec   262 MBytes  2.19 Gbits/sec    0   3.72 MBytes
- - - - - - - - - - - - - - - - - - - - - - - - -
[ ID] Interval           Transfer     Bitrate         Retr
[  5]   0.00-10.00  sec  2.53 GBytes  2.18 Gbits/sec    0            sender
[  5]   0.00-10.01  sec  2.53 GBytes  2.17 Gbits/sec                  receiver

iperf Done.
```

## 3. 结论
<ol>
<li>增大UDP缓冲区大小对带宽的提升很大，使用大页内存好像没什么提升。
<li>对比使用同步IO的API，使用异步IO的API提升很大。