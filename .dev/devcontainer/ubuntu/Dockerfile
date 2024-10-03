FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

ARG USERNAME=vscode

# Workaround for bug: https://github.com/devcontainers/images/issues/1056
RUN userdel -r ubuntu; usermod -u 1000 $USERNAME; groupmod -g 1000 $USERNAME

RUN apt-get -y update \
    && apt-get install -y \
        # Build tools and dependencies for OwnTone
        autoconf \
        automake \
        autotools-dev \
        bison \
        build-essential \
        flex \
        gawk \
        gettext \
        git \
        gperf \
        libasound2-dev \
        libavahi-client-dev \
        libavcodec-dev \
        libavfilter-dev \
        libavformat-dev \
        libavutil-dev \
        libconfuse-dev \
        libcurl4-openssl-dev \
        libevent-dev \
        libgcrypt20-dev \
        libjson-c-dev \
        libmxml-dev \
        libplist-dev \
        libprotobuf-c-dev \
        libsodium-dev \
        libsqlite3-dev \
        libswscale-dev \
        libtool \
        libunistring-dev \
        libwebsockets-dev \
        libxml2-dev \
        zlib1g-dev \
        # Build tools for mmkdocs (OwnTone documentation)
        python3-pip \
        # Additional runtime dependencies for dev container
        avahi-daemon \
        # Additional debug and devtools for dev container
        clang \
        clang-format \
        clang-tools \
        gdb \
        valgrind \
        # Additional terminal utility applications
        pipx \
        # bat - replacement for cat
        bat \
        # eza (https://eza.rocks/) - replacement for ls
        eza \
        # fuzzy search
        fzf \
    # Create folders and set ownership for folders that might be mounted as volumes
    && mkdir -p /home/$USERNAME/.local/share \
    && chown -R $USERNAME /home/$USERNAME/.local \
    && mkdir /commandhistory \
    && touch /commandhistory/.bash_history \
    && chown -R $USERNAME /commandhistory \
    && echo "export PROMPT_COMMAND='history -a' && export HISTFILE=/commandhistory/.bash_history" >> "/home/$USERNAME/.bashrc" \
    && echo '[[ -f /scripts/init-devcontainer-cli.sh ]] && source /scripts/init-devcontainer-cli.sh' >> "/home/$USERNAME/.bashrc" \
    # Create folders for owntone-server data
    && mkdir -p /data/logs /data/music /data/cache /data/conf \
    && chown -R $USERNAME /data \
    # Clean up
    && apt-get clean -y && rm -rf /var/lib/apt/lists/*
