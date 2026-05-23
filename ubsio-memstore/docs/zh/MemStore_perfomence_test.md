# 性能测试指南

## 准备测试环境

**前提条件**

已安装部署环境。具体操作请参见《MemStore部署指南》。

**配置测试环境**

按顺序运行文件cli\_server、mms\_console、cli\_client。

1. 运行文件cli\_server。

    ```cmd
    [root@**** bin]# cli_server &
    ```

2. 运行文件mms\_console。

    ```cmd
    [root@**** bin]# mms_console
    Usage: mms_console [busypoing:0/1][conn count:2][worker groups:4]
    [root@**** bin]# mms_console 1 1 2 &
    mms service state is: normal.
    mms console start success.
    ```

    >[!NOTICE] 说明
    >mms\_console文件只需要在分离部署场景执行，在融合场景不需要执行。

3. 运行文件cli\_client。

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

4. 绑定client端进程。

    ```cmd
    root:/cli>attach 600
    Attach AppId<600> success
    ```

## 测试指令<a name="ZH-CN_TOPIC_0000002603860753"></a>

```cmd
put value: mms put [userId] [key] [filePath] [length]
get value: mms get [userId] [key] [offset] [length] [filePath]
update value: mms update [userId] [key] [filePath] [offset] [length]
replace value: mms replace [userId] [key] [filePath] [offset] [length]
delete object: mms delete [userId] [key]
catchup: mms catchup 
trace: mms trace [open/close/show/clear]
perf: mms perf [put/get/update/delete/mixes] [bs(Kb)] [ioDepth] [batchNum] [size(Mb)] [userId] [numaNum] 
[cpuNum] [cpuStart]
set: mms set [length(b)]
```

>[!NOTICE] 说明 
>
>- 所有指令都要在运行cli\_client后的命令行中执行。
>
>- cpuStart要与已绑定核心区分。如当配置"mms.net.rpc.worker.groups.cpuset": "0-0,1-1,2-2,3-3,4-4,5-5,6-6,7-7"， cpuStart参数不能选用0-7。

## 测试不同并发数<a name="ZH-CN_TOPIC_0000002573021690"></a>

- 测试场景1：2KB I/O请求，8个并发操作，单次操作总I/O大小100MB，用户ID为0，NUMA节点个数为1，从32个CPU中的第20个开始执行。

    ```cmd
    root:/cli> mms trace open
    root:/cli> mms perf put 2 8 1 100 0 1 32 20
    root:/cli> mms perf get 2 8 1 100 0 1 32 20
    root:/cli> mms perf update 2 8 1 100 0 1 32 20
    root:/cli> mms perf delete 2 8 1 100 0 1 32 20
    root:/cli> mms trace show
    root:/cli> mms trace clear
    ```

- 测试场景2：2KB I/O请求，4个并发操作，单次操作总I/O大小100MB，用户ID为0，NUMA节点个数为1，从32个CPU中的第20个开始执行。

    ```cmd
    root:/cli> mms trace open
    root:/cli> mms perf put 2 4 1 100 0 1 32 20
    root:/cli> mms perf get 2 4 1 100 0 1 32 20
    root:/cli> mms perf update 2 4 1 100 0 1 32 20
    root:/cli> mms perf delete 2 4 1 100 0 1 32 20
    root:/cli> mms trace show
    root:/cli> mms trace clear
    ```

## 测试批量操作<a name="ZH-CN_TOPIC_0000002603740809"></a>

测试场景：2KB I/O请求，单个并发操作，8批量操作，总I/O大小100MB，用户ID为0，NUMA节点个数为1，从32个CPU中的第20个开始执行。

```cmd
root:/cli> mms trace open
root:/cli> mms perf put 2 1 8 100 0 1 32 20
root:/cli> mms perf get 2 1 8 100 0 1 32 20
root:/cli> mms perf update 2 1 8 100 0 1 32 20
root:/cli> mms perf delete 2 1 8 100 0 1 32 20
root:/cli> mms trace show
root:/cli> mms trace clear
```

## 测试UPDATE不同size的数据<a name="ZH-CN_TOPIC_0000002573181328"></a>

测试场景：1B I/O请求，单个并发操作，8批量操作，总I/O大小100MB，用户ID为0，NUMA节点个数为1，从32个CPU中的第20个开始执行。

```cmd
root:/cli> mms set 1
root:/cli> mms trace open
root:/cli> mms perf put 2 1 8 100 0 1 32 20
root:/cli> mms perf get 2 1 8 100 0 1 32 20
root:/cli> mms perf update 2 1 8 100 0 1 32 20
root:/cli> mms perf delete 2 1 8 100 0 1 32 20
root:/cli> mms trace show
root:/cli> mms trace clear
```

>[!NOTICE] 说明
>mms perf put 2 1 8 100 0 1 32 20命令执行后，2KB IO会被mms set永久覆盖为1B。

## 测试7:3读写混合<a name="ZH-CN_TOPIC_0000002573021688"></a>

测试场景：2KB I/O请求，8个并发操作，单次操作总I/O大小100MB，用户ID为0，NUMA节点个数为1，从32个CPU中的第20个开始执行。

```cmd
root:/cli> mms trace open
root:/cli> mms perf mixes 2 8 1 100 0 1 32 20
root:/cli> mms perf delete 2 8 1 100 0 1 32 20
root:/cli> mms trace show
root:/cli> mms trace clear
```
