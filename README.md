# 1. 简介
如果你有两台设备能直接访问，访问没有经过 NAT (比如两台设备都有公网 IPv6 地址)，那就可以使用本项目来虚拟一个局域网。

## 1.1 为什么要做这个
<ol>
<li>很多软件(比如局域网联机游戏)目前还是仅支持 IPv4 连接。
<li>IPv6 普及很久了，现在每台手机移动上网基本都有被分配一个公网 IPv6 地址池。
</ol>

# 2. 编译
<ul>
<li>本项目支持 Windows 和 Linux 平台。
<li>Windows 平台编译请使用 msys2 的 ucrt64 环境。
</ul>

```shell
# 在项目根目录下操作
$ mkdir -p build && cd build
$ cmake ..
$ make -j
# 可执行程序(TapLan)会在刚刚创建的 build 目录下生成
```

# 3. 运行
注意，在 Windows 平台运行时，请将可执行文件放到 windrivers 目录下，并进入 windrivers 目录下运行可执行文件，因为 Windows 平台需要安装 TAP 设备驱动。


运行下面命令查看可用参数。
```shell
$ ./TapLan -h
Usage as server: ./TapLan [-s <CIDR>] [-p <server port>]
Usage as client: ./TapLan -c <host:port>
Server or Client:
  -p              <port>          local port
  -m              <minutes>       cycle switching source ports with a switching interval of <minutes>
  --ll            [fewidtFEWIDT]  set log level
  --aio                           (IOCP/iouring) will be used, iouring requires Linux kernel version 6.1 or later
  --nosync                        run without sync server
  -h,--help                       print the messages you see
Server specific:
  -s  <CIDR>                      run in server mode, allocate ipv4 address within <CIDR>(e.g. 192.168.208.0/24)
Client specific:
  -c  <host:port>                 run in client mode, connect to <host:port>(e.g. 192.168.208.1:3460, [::ffff:192.168.208.1]:3460)
```

## 3.1 服务端-客户端 模式
默认运行在该模式下，服务端负责转发客户端和自己的流量，客户端所有经过TAP设备的流量会直接发送给服务端。
```shell
# 启动服务端
$ sudo ./TapLan

# 启动客户端，<host:port>替换成服务端IP地址和端口号(默认是3460)
$ sudo ./TapLan -c "192.168.0.100:3460"

# IPv6地址请使用这个格式<[IPv6]:port>，将IPv6地址使用中括号包裹起来
# 客户端成功连接后，服务端会给客户端分配一个TAP设备的IPv4地址
# 服务端的TAP设备IPv4地址默认是192.168.208.1
```

## 3.2 P2P 模式
该模式下，流量无需转发，所有经过TAP设备的流量会直接发送给对端。
```shell
# <host:port> 替换成对方的ip地址和端口号
# --nosync 添加该参数来让程序运行在该模式下
$ sudo ./TapLan -c <host:port> --nosync
```

## 3.3 部分参数说明

### 3.3.1 --aio
推荐 Linux 平台且内核版本在6.1以上添加--aio这个参数，使用异步IO可以获取更高的转发带宽。

点击这个 [转发性能测试](doc/转发性能测试/README.md) 来查看详情。

### 3.3.2 -m
<ol>
<li>如果两台设备之间丢包严重，请尝试添加这个参数。
<li>丢包严重可能是因为运营商的QOS策略。
<li>启用该参数后，程序每间隔一段指定的时间会切换并使用不同的UDP源端口号。
<li>可能对针对5元组的QOS策略有效来减少丢包。
</ul>
