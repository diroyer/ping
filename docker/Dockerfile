FROM debian:12

ENV DEBIAN_FRONTEND=noninteractive

# Build dependencies + useful debugging/networking tools
RUN apt-get update && apt-get install -y \
    build-essential \
    wget \
    ca-certificates \
    autoconf \
    automake \
    libtool \
    pkg-config \
    gdb \
    strace \
    tcpdump \
    iproute2 \
    iputils-ping \
    && rm -rf /var/lib/apt/lists/*

# Download inetutils 2.0
WORKDIR /tmp

RUN wget https://ftp.gnu.org/gnu/inetutils/inetutils-2.0.tar.gz \
    && tar -xzf inetutils-2.0.tar.gz

# Build inetutils 2.0
WORKDIR /tmp/inetutils-2.0

RUN ./configure \
        --disable-all-programs \
        --enable-ping \
    && make -j"$(nproc)" \
    && make install

# Make sure /usr/local/bin is searched first
ENV PATH="/usr/local/bin:${PATH}"

# Working directory for your project
WORKDIR /ping

CMD ["/bin/bash"]
