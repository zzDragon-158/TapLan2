# stop nm


# tap
TapLan --noser -c "192.168.2.1:3460"

# bridge
sudo ip link add br0 type bridge
sudo ip link set br0 up
sudo ip addr add 192.168.208.1/24 dev br0
sudo ip link set TapLan master br0

# dhcp
sudo pacman -S dnsmasq
sudo systemctl start dnsmasq

# dhcp config
interface=br0
dhcp-range=192.168.208.2,192.168.208.127,12h
dhcp-option=3,192.168.208.1
dhcp-option=6,8.8.8.8,114.114.114.114
bind-interfaces

# arp proxy
echo 1 | sudo tee /proc/sys/net/ipv4/conf/br0/proxy_arp

# nat
sudo sysctl -w net.ipv4.ip_forward=1
sudo iptables -t nat -A POSTROUTING -o ens33 -j MASQUERADE
sudo iptables -A FORWARD -i br0 -o ens33 -j ACCEPT
sudo iptables -A FORWARD -i ens33 -o br0 -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT

# ap
sudo hostapd /etc/hostapd/hostapd.conf

# WDS (4-address frame) mode with per-station virtual interfaces
# (only supported with driver=nl80211)
# This mode allows associated stations to use 4-address frames to allow layer 2
# bridging to be used.
wds_sta=1
wmm_enabled=1
uapsd_advertisement=1