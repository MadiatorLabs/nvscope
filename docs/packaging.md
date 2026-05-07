# Packaging

`nvscope` is intentionally small for Runpod images: one shared library plus two shell tools. A package only needs to install:

```text
/usr/lib/nvscope/libnvscope.so
/usr/bin/nvscope
/usr/bin/nvscope-probe
```

## Local Install

For Runpod image builds where a package is unnecessary:

```sh
make
make install PREFIX=/usr
```

Use `DESTDIR` for staged installs:

```sh
make
make install PREFIX=/usr DESTDIR="$PWD/package-root"
```

## Debian, RPM, APK

The repository includes an `nfpm` config:

```sh
make clean
make
nfpm package --config packaging/nfpm.yaml --packager deb
nfpm package --config packaging/nfpm.yaml --packager rpm
nfpm package --config packaging/nfpm.yaml --packager apk
```

GitHub Actions runs the same package build and uploads the resulting package files as workflow artifacts.

Install the resulting package in a Runpod container image and use the wrapper:

```dockerfile
COPY nvscope_*.deb /tmp/
RUN apt-get update && apt-get install -y --no-install-recommends /tmp/nvscope_*.deb \
    && rm -f /tmp/nvscope_*.deb
```

Then:

```sh
nvscope -- ffmpeg ...
```