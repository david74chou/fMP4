FROM ubuntu:15.10

RUN apt-get update
RUN apt-get -y install \
	    iproute2 \
	    net-tools \
	    vim \
	    git \
	    tmux \
	    autoconf \
	    automake \
	    libtool \
	    g++ \
	    wget \
	    uuid-dev \
	    dbus \
	    diffstat \
	    texinfo \
	    gawk \
	    chrpath \
	    fakeroot \
	    u-boot-tools \
	    valgrind \
	    intltool \
	    libc6-i386 \
	    psmisc \
	    software-properties-common \
	    cmake \
	    pkg-config

RUN apt-get -y install \
            build-essential \
            libass-dev \
            libfreetype6-dev \
            libsdl1.2-dev \
            libtheora-dev \
            libtool \
            libva-dev \
            libvdpau-dev \
            libvorbis-dev \
            libxcb1-dev \
            libxcb-shm0-dev \
            libxcb-xfixes0-dev \
            texinfo \
            zlib1g-dev \
            yasm \
            tar

RUN cd /tmp; \
    wget https://github.com/FFmpeg/FFmpeg/releases/download/n3.0/ffmpeg-3.0.tar.bz2; \
    tar xjvf ffmpeg-3.0.tar.bz2; \
    cd ffmpeg-3.0; \
    ./configure --prefix="/usr/local" --extra-cflags="-I/usr/local/include" --extra-ldflags="-L/usr/local/lib"; \
    make -j4; \
    make install

RUN rm -rf ffmpeg-*
