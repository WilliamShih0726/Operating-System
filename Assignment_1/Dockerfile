FROM ubuntu:24.04

RUN apt-get update && apt-get upgrade -y && \
    apt-get install -y \
        openssh-server sudo qemu-system-misc \
        autoconf automake autotools-dev curl \
        libmpc-dev libmpfr-dev libgmp-dev gawk build-essential \
        bison flex texinfo gperf libtool patchutils bc zlib1g-dev \
        libexpat-dev git cpio python3 python3-pip python3-tomli \
        ninja-build cmake libglib2.0-dev libslirp-dev \
        gcc-riscv64-linux-gnu libncurses5-dev

RUN mkdir /var/run/sshd
RUN echo "ubuntu ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

EXPOSE 22
ENTRYPOINT ["/entrypoint.sh"]
