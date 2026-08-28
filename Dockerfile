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

# Add /opt/workdir as a safe directory in case the entire repository is mounted.
# See the `compose.yaml` file for more information.
RUN git config --global --add safe.directory /opt/workdir
RUN nix develop && exit

WORKDIR /opt/workdir