#!/bin/bash
# 临时调优脚本（仅当前终端生效）

echo "开始临时系统参数调优（仅当前终端生效）..."

# 增大最大文件描述符限制（当前 shell）
ulimit -n 100000
echo "ulimit -n 设置为 $(ulimit -n)"

# 临时修改内核参数（当前系统运行时生效，重启失效）
sudo sysctl -w net.core.somaxconn=65535
sudo sysctl -w net.ipv4.tcp_max_syn_backlog=65535
sudo sysctl -w net.ipv4.tcp_tw_reuse=1
sudo sysctl -w net.ipv4.tcp_fin_timeout=15
sudo sysctl -w net.ipv4.ip_local_port_range="1024 65535"
sudo sysctl -w net.core.netdev_max_backlog=250000
sudo sysctl -w net.core.rmem_max=134217728
sudo sysctl -w net.core.wmem_max=134217728
sudo sysctl -w fs.file-max=2097152

echo "临时系统参数已设置，重启后会恢复默认。"
