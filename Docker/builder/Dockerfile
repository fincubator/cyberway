FROM ubuntu:18.04

LABEL author="xiaobo <peterwillcn@gmail.com>" maintainer="Xiaobo <peterwillcn@gmail.com> Huang-Ming Huang <huangh@objectcomputing.com>" version="0.1.1" \
  description="This is a base image for building eosio/eos"

RUN echo 'APT::Install-Recommends 0;' >> /etc/apt/apt.conf.d/01norecommends \
  && echo 'APT::Install-Suggests 0;' >> /etc/apt/apt.conf.d/01norecommends \
  && apt-get update \
  && DEBIAN_FRONTEND=noninteractive apt-get install -y sudo wget curl net-tools ca-certificates unzip gnupg

RUN echo "deb http://apt.llvm.org/bionic/ llvm-toolchain-bionic-7 main" >> /etc/apt/sources.list.d/llvm.list \
  && wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key|sudo apt-key add - \
  && apt-get update \
  && DEBIAN_FRONTEND=noninteractive apt-get install -y git-core sudo ruby curl \
     llvm-4.0 llvm-4.0-dev clang-4.0 libclang-4.0-dev  \
     llvm-7 llvm-7-dev clang-7 libclang-7-dev libclang-common-7-dev libllvm7 \
     make automake autoconf libtool doxygen graphviz autotools-dev build-essential \
     python2.7 python2.7-dev python3 python3-dev \
     libbz2-dev libssl-dev libusb-1.0-0-dev libcurl4-gnutls-dev libgmp-dev libgmp3-dev libicu-dev zlib1g-dev \
     pkg-config ninja-build mpi-default-dev\
  && rm -rf /var/lib/apt/lists/*

RUN update-alternatives --install /usr/bin/clang clang /usr/lib/llvm-7/bin/clang 400 \
  && update-alternatives --install /usr/bin/clang++ clang++ /usr/lib/llvm-7/bin/clang++ 400

RUN wget https://cmake.org/files/v3.9/cmake-3.9.6-Linux-x86_64.sh \
    && bash cmake-3.9.6-Linux-x86_64.sh --prefix=/usr/local --exclude-subdir --skip-license \
    && rm cmake-3.9.6-Linux-x86_64.sh

ENV CC clang
ENV CXX clang++

RUN wget https://dl.bintray.com/boostorg/release/1.67.0/source/boost_1_67_0.tar.bz2 -O - | tar -xj \
    && cd boost_1_67_0 \
    && ./bootstrap.sh --prefix=/usr/local \
    && echo 'using clang : 7.1 : clang++-7 ;' >> project-config.jam \
    && ./b2 -d0 -j$(nproc) --with-thread --with-date_time --with-system --with-filesystem --with-program_options \
       --with-signals --with-serialization --with-chrono --with-test --with-context --with-locale --with-coroutine --with-iostreams toolset=clang link=static install \
    && cd .. && rm -rf boost_1_67_0

RUN git clone --depth 1 --single-branch --branch release_40 https://github.com/llvm-mirror/llvm.git \
    && git clone --depth 1 --single-branch --branch release_40 https://github.com/llvm-mirror/clang.git llvm/tools/clang \
    && cd llvm \
    && cmake -H. -Bbuild -GNinja -DCMAKE_INSTALL_PREFIX=/opt/wasm -DLLVM_TARGETS_TO_BUILD= -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=WebAssembly -DCMAKE_BUILD_TYPE=Release  \
    && cmake --build build --target install \
    && cd .. && rm -rf llvm

RUN wget https://github.com/mongodb/mongo-c-driver/releases/download/1.13.0/mongo-c-driver-1.13.0.tar.gz -O - | tar -xz \
    && cd mongo-c-driver-1.13.0 \
    && mkdir cmake-build && cd cmake-build \
    && cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local -DENABLE_BSON=ON \
		-DENABLE_SSL=OPENSSL -DENABLE_AUTOMATIC_INIT_AND_CLEANUP=OFF -DENABLE_STATIC=ON -DENABLE_ICU=OFF .. \
    && make -j$(nproc) \
    && make install \
    && cd ../../ && rm -rf mongo-c-driver-1.13.0

RUN git clone --depth 1 -b r3.4.0 https://github.com/mongodb/mongo-cxx-driver \
    && cd mongo-cxx-driver/build \
    && cmake -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local .. \
    && make -j$(nproc) \
    && make install \
    && cd ../../ && rm -rf mongo-cxx-driver
