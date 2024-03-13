#!/bin/bash
  
net_card=$(ibdev2netdev | grep mlx5_0 | awk '{print $5}')

cma_roce_tos -d mlx5_0 -t 106
#cma_roce_tos -d mlx5_1 -t 106
mlnx_qos -i ${net_card} --pfc 0,0,0,1,0,0,0,0 --trust dscp
#mlnx_qos -i ${net_card} --pfc 0,0,0,1,0,0,0,0
ifconfig ${net_card} mtu 4500

cnp_dscp=$(cat /sys/class/net/${net_card}/ecn/roce_np/cnp_dscp)
roce_np_3=$(cat /sys/class/net/${net_card}/ecn/roce_np/enable/3)
roce_rp_3=$(cat /sys/class/net/${net_card}/ecn/roce_rp/enable/3)

# 配置网卡的CNP的DSCP字段值
if [ $cnp_dscp != '48' ]; then
  echo "cnp_dscp:"$cnp_dscp
  echo 48 >/sys/class/net/${net_card}/ecn/roce_np/cnp_dscp
fi

# 网卡配置RoCEv2的DCQCN拥塞控制机制
if [ $roce_np_3 != '1' ]; then
  echo "roce_np_3:"$roce_np_3
  echo 1 >/sys/class/net/${net_card}/ecn/roce_np/enable/3
fi
if [ $roce_rp_3 != '1' ]; then
  echo "roce_rp_3:"$roce_rp_3
  echo 1 >/sys/class/net/${net_card}/ecn/roce_rp/enable/3
fi
