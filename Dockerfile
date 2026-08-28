FROM nixos/nix

RUN nix-channel --update && echo "experimental-features = nix-command flakes" > /etc/nix/nix.conf

COPY ./flake.nix ./flake.lock /program-cache-warmup/
COPY ./misc/nix /program-cache-warmup/misc/nix
COPY ./editor /program-cache-warmup/editor
COPY ./engine /program-cache-warmup/engine
COPY ./libs /program-cache-warmup/libs
COPY ./shadercompiler /program-cache-warmup/shadercompiler
COPY ./CMakeLists.txt /program-cache-warmup/

WORKDIR /program-cache-warmup/

RUN nix develop && exit

WORKDIR /opt/workdir