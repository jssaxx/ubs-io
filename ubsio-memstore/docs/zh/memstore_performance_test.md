# 性能测试指南

## 准备测试环境

**前提条件**

已安装部署环境。具体操作请参见《MemStore部署指南》。

**配置测试环境**

按顺序运行文件mmsd、mms\_console、cli\_server、cli\_client。

1. 所有节点启动mms server
    ```cmd
    [root@**** bin]# mmsd &
    ```

2. 启动mms\_console

    ```cmd
    [root@**** bin]# mms_console
    Usage: mms_console [busypoing:0/1][conn count:2][worker groups:4]
    [root@**** bin]# mms_console 1 1 2 &
    mms service state is: normal.
    mms console start success.
    ```

    >[!NOTICE] mms\_console文件只需要在分离部署场景执行，在融合场景不需要执行。

3. 启动cli\_server

    ```cmd
    [root@**** bin]# cli_server &
    ```

4. 运行文件cli\_client

    ```cmd
    [root@**** bin]# cli_client
    root:/cli>ls
    <AppId><State><AppName>
     800      1    mms_s
    root:/cli>ls
    <AppId><State><AppName>
     800      1    mms_s
     600      1    mms_c
    ```
   >[!NOTICE] 600是client的编号，分离部署起mms_console后才有，800是server的编号。

5. 绑定client端进程

    ```cmd
    root:/cli>attach 600
    Attach AppId<600> success
    ```

   >[!NOTICE] 分离部署就attach 600，融合部署attach 800。

## 测试指令

```cmd
show: 查询一些信息
    mms show [node/pt/net/multicast]

put value: 写数据
    mms put [userId] [key] [filePath] [length]

get value: 读数据
    mms get [userId] [key] [offset] [length] [filePath]

update value: 更新数据
    mms update [userId] [key] [filePath] [offset] [length]

replace value: 替换数据，数据已经存在:更新，数据不存在:写入
    mms replace [userId] [key] [filePath] [offset] [length]

delete object: 删除某个key/value
    mms delete [userId] [key]

prefix search: 前缀查询
    mms prefix [prefix]

range search: 范围查找/删除
    mms range delete/search [start] [end]

catchup: 开始数据恢复
    mms catchup

trace: 查询时延打点信息
    mms trace [open/close/show/clear]

notify: io里数据变更通知字段的填充值
    mms notify [open/close]

perf: 批量操作命令
    mms perf [put/get/update/replace/delete/mixes] [bs(Kb)] [ioDepth] [batchNum] [size(Mb)] [userId] [numaNum] [cpuNum] [cpuStart] [readRate]

perfcheck: 数据一致性校验
    mms perfcheck [lwrite/rwrite] [filePath] [bs(Kb)] [ioDepth] [batchNum] [size(Mb)] [userId] [numaNum] [cpuNum] [cpuStart]

exit
    exit console
```

>[!NOTICE]
>- 所有指令都要在运行cli\_client后的命令行中执行。
>
>- cpuStart要与已绑定核心区分。如当配置"mms.net.rpc.worker.groups.cpuset": "0-0,1-1,2-2,3-3,4-4,5-5,6-6,7-7"， cpuStart参数不能选用0-7。  
>- [ioDepth] [numaNum] [cpuNum] [cpuStart]这四个参数要搭配起来使用，ioDepth: io是多少并发，numaNum: 本次测试的io在多少个numa节点上进行，这里的个数要和mms.conf里的mms.mem.numa.id的个数对应，cpuNum: 机器上每个numa节点上有几个cpu，可以使用lscpu查看， cpuStart:cpu起始编号。举例: mms perf put 1 8 1 1024 0 2 40 12, 这里: ioDepth = 8、numaNum = 2、cpuNum = 40、cpuStart = 12， 实际绑定 CPU 顺序是：
>  线程0 -> 12;
   线程1 -> 52;
   线程2 -> 13;
   线程3 -> 53;
   线程4 -> 14;
   线程5 -> 54;
   线程6 -> 15;
   线程7 -> 55。

## 测试不同并发数

- 测试场景1：1KB I/O请求，8个并发操作，单次操作总I/O大小100MB，用户ID为0，NUMA节点个数为1，从32个CPU中的第20个开始执行。

    ```cmd
    root:/cli> mms trace open
    root:/cli> mms perf put 1 8 1 100 0 1 32 20
    root:/cli> mms perf get 1 8 1 100 0 1 32 20
    root:/cli> mms perf update 1 8 1 100 0 1 32 20
    root:/cli> mms perf delete 1 8 1 100 0 1 32 20
    root:/cli> mms trace show
    root:/cli> mms trace clear
    ```

- 测试场景2：1KB I/O请求，4个并发操作，单次操作总I/O大小100MB，用户ID为0，NUMA节点个数为1，从32个CPU中的第20个开始执行。

    ```cmd
    root:/cli> mms trace open
    root:/cli> mms perf put 1 4 1 100 0 1 32 20
    root:/cli> mms perf get 1 4 1 100 0 1 32 20
    root:/cli> mms perf update 1 4 1 100 0 1 32 20
    root:/cli> mms perf delete 1 4 1 100 0 1 32 20
    root:/cli> mms trace show
    root:/cli> mms trace clear
    ```

## 测试批量操作

测试场景：1KB I/O请求，单个并发操作，8批量操作，总I/O大小100MB，用户ID为0，NUMA节点个数为1，从32个CPU中的第20个开始执行。

```cmd
root:/cli> mms trace open
root:/cli> mms perf put 1 1 8 100 0 1 32 20
root:/cli> mms perf get 1 1 8 100 0 1 32 20
root:/cli> mms perf update 1 1 8 100 0 1 32 20
root:/cli> mms perf delete 1 1 8 100 0 1 32 20
root:/cli> mms trace show
root:/cli> mms trace clear
```

## 测试UPDATE不同size的数据

测试场景：1B I/O请求，单个并发操作，8批量操作，总I/O大小100MB，用户ID为0，NUMA节点个数为1，从32个CPU中的第20个开始执行。

```cmd
root:/cli> mms set 1
root:/cli> mms trace open
root:/cli> mms perf put 1 1 8 100 0 1 32 20
root:/cli> mms perf get 1 1 8 100 0 1 32 20
root:/cli> mms perf update 1 1 8 100 0 1 32 20
root:/cli> mms perf delete 1 1 8 100 0 1 32 20
root:/cli> mms trace show
root:/cli> mms trace clear
```

>[!NOTICE] mms perf put 1 1 8 100 0 1 32 20命令执行后，1KB IO会被mms set永久覆盖为1B。

## 测试7:3读写混合

测试场景：1KB I/O请求，8个并发操作，单次操作总I/O大小100MB，用户ID为0，NUMA节点个数为1，从32个CPU中的第20个开始执行。

```cmd
root:/cli> mms trace open
root:/cli> mms perf mixes 1 8 1 100 0 1 32 20
root:/cli> mms perf delete 1 8 1 100 0 1 32 20
root:/cli> mms trace show
root:/cli> mms trace clear
```
>[!NOTICE] perf mixes默认读写比列是7:3, 可以通过更改命令最后一个参数readRate来实现不同的比例，比如填8，那么读写比例就是8:2。