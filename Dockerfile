FROM --platform=linux/amd64 ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y \
    gcc \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

COPY src/ src/
COPY Makefile .
COPY ubuntu-rootfs.tar .

RUN make clean && make

RUN make tar

FROM --platform=linux/amd64 ubuntu:22.04

WORKDIR /workspace

COPY --from=builder /workspace/container .
COPY --from=builder /workspace/ubuntu-rootfs ./ubuntu-rootfs

CMD ["./container", "run", "/bin/bash"]