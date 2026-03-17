FROM archlinux:latest
# use mirror sources
RUN echo "\n\
"\
# install packages
&& pacman -Syu\
&& pacman -S tcpdump iperf3 iperf gdb nvim liburing\
&& echo "docker image bulid complete."
