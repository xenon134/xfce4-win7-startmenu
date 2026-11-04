FROM localhost/xfce4-panel-build:4.20

RUN apt-get update && apt-get install -y --no-install-recommends \
        meson ninja-build \
        libxfce4panel-2.0-dev \
        libgtk-layer-shell-dev \
        libaccountsservice-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
