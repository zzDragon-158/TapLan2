FROM archlinux:latest
# use mirror sources
RUN echo "\n\
"\
# install packages
&& pacman -Syu\
&& pacman -S tcpdump iperf3 iperf gdb nvim\
&& echo "docker image for testing already build complete."
