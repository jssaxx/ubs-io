# UBS-IO

## Project Overview

Data volumes soar as Internet big data applications, cloud-native services, and AI applications grow quickly. The traditional storage-compute coupled architecture is difficult to scale out, and even hampers data sharing in the cloud era. To address this pain point, the storage-compute decoupled architecture has emerged as a feasible alternative. However, in the decoupled architecture, applications on the compute side must go to the peer network when accessing data on the storage side. The cross-network access reduces service I/O performance. What's worse, large-scale deployment of compute nodes reduces resource utilization on the compute side.

The BeiMing UBS-IO acceleration kit leverages the Huawei computing platform to build high-performance distributed read and write caches on the compute side. Based on the extensive application ecosystem and broad northbound compatibility of the open source distributed file system JuiceFS, BeiMing UBS-IO has presented itself as an effective solution to performance loss problems inherent in the storage-compute decoupled architecture for big data and AI applications.

In the storage-compute decoupled architecture, the Spark big data engine has the performance bottleneck of slow dataset loading, and large language models (LLMs) in AI converged computing have performance bottlenecks of slow dataset loading and checkpoint writing. BoostIO breaks those performance bottlenecks in the following ways:

* UBS-IO builds a multi-tier distributed write cache using memory media and high-speed drives on the compute side. Working with a remote direct memory access (RDMA) high-speed network and the multi-copy redundancy mechanism to ensure high data reliability, UBS-IO retains application I/Os on the compute side to reduce the data write latency.
* UBS-IO sets up a read cache and a write cache, which are independent of each other. This independent architecture design brings advantages such as independent cache configurations, flexible eviction policies, and independent resource allocation to each cache.
* UBS-IO combines the distributed read cache with intelligent data prefetch and hot/cold data identification to ensure that hot and warm data is cached in the memory and high-speed drives on the compute side, whereas cold data is stored in the back-end large-capacity storage cluster. This mechanism increases the cache hit ratio and shortens the data read latency.

In conclusion, UBS-IO alleviates performance bottlenecks in big data and AI converged computing and improves end-to-end application performance.

## Source Code Compilation

```shell
$ git clone <repo-url>
$ cd boostio
$ bash build.sh -t release
```

After the compilation is complete, the UBS-IO software package **BoostIO_1.0.0_\$(uname -s)-\$(arch)_\${BUILD_TYPE}.tar.gz** is generated in the **dist** directory.
**To generate an RPM package:**
Run the **rpm_install.sh** script in the **build** directory.

```sh
rm -rf ~/rpmbuild
rpmdev-setuptree
tar -cvzf ubs-io.tar.gz ubs-io
cp ubs-io.tar.gz ~/rpmbuild/SOURCES/

# The CLI tool package is provided. Developers need to provide the corresponding CLI so file for debugging.
rpmbuild -ba ubs-io.spec

# Standard release package, without a test package
rpmbuild -ba ubs-io.spec --define "with_cli 0"

# Debug package
rpmbuild -ba ubs-io.spec --define "build_type debug"
```

The final RPM installation package is generated in **~/rpmbuild/RPMS**.

## UT

```shell
$ cd  test/llt/
$ bash run_dt.sh
```

After the test script is executed, the compilation and test cases are automatically executed. Observe the test case execution result.

## How to Contribute

If you have any questions or want to provide feedback on feature requirements and bug reports, you can submit an issue.

## Disclaimer

This code repository contributes to the openEuler open source project. It strictly adheres to the coding style and methods, as well as security design of the native open source software. Any vulnerability and security issues of the software shall be resolved by the corresponding upstream communities according to their response mechanisms. Please pay attention to the notifications and version updates released by the upstream communities. The Kunpeng computing community does not assume any responsibility for software vulnerabilities and security issues.

## License

The UBS-IO product is licensed under Mulan Permissive Software License [http://license.coscl.org.cn/MulanPSL2](http://license.coscl.org.cn/MulanPSL2). For details, see the **LICENSE** file in the root directory.
The documents in the **doc** directory of UBS-IO are licensed under Attribution-ShareAlike 4.0 International. For details, see the **LICENSE** file in the **doc** directory.
