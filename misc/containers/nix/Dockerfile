FROM nixos/nix

RUN nix-channel --update && echo "experimental-features = nix-command flakes" > /etc/nix/nix.conf

COPY ./flake.nix ./flake.lock /doriax-cache-warmup/
COPY ./misc/nix /doriax-cache-warmup/misc/nix
COPY ./editor /doriax-cache-warmup/editor
COPY ./engine /doriax-cache-warmup/engine
COPY ./libs /doriax-cache-warmup/libs
COPY ./shadercompiler /doriax-cache-warmup/shadercompiler
COPY ./CMakeLists.txt /doriax-cache-warmup/

WORKDIR /doriax-cache-warmup/

# Add /opt/workdir as a safe directory in case the entire repository is mounted.
# See the `compose.yaml` file for more information.
RUN git config --global --add safe.directory /opt/workdir
RUN nix develop && exit

WORKDIR /opt/workdir