FROM ubuntu:focal

ARG DEBIAN_FRONTEND=noninteractive

ENV SWIFTSHADER_LD_LIBRARY_PATH=/trees/swiftshader/build
ENV CXXFLAGS='-fno-builtin -Wno-pass-failed'

WORKDIR /trees

COPY *.diff /trees
COPY entrypoint.sh /trees

RUN apt-get update && \
    apt-get --no-install-recommends install -y \
	apt-transport-https \
	apt-utils \
	build-essential \
	cmake \
	ca-certificates \
	git \
	ninja-build \
	python \
	python3 \
	xorg-dev \
	clang-7 \
	libc++-7-dev \
	libc++abi-7-dev \
	lldb

# Ensure that clang is used instead of gcc.
RUN set -eux ;\
    update-alternatives --install /usr/bin/clang clang /usr/bin/clang-7 100 ;\
    update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-7 100 ;\
    update-alternatives --install /usr/bin/cc cc /usr/bin/clang 100 ;\
    update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++ 100

# Clone SwiftShader, apply patches, and build it.
RUN set -eux ;\
	git clone https://swiftshader.googlesource.com/SwiftShader swiftshader ;\
    cd swiftshader ;\
	git checkout 139f5c3 ;\
	git apply /trees/*.diff ;\
    cd build ;\
    cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release ;\
    ninja

ENTRYPOINT ["/trees/entrypoint.sh"]
