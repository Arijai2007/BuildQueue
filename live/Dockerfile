FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends build-essential coreutils python3 util-linux \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . .
RUN make -j2
ENTRYPOINT ["./buildqueue"]
