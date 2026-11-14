FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends g++ make cmake python3 ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace
COPY . .
RUN make -j2 && make test
EXPOSE 8080
CMD ["./buildqueue", "--serve", "--port", "8080"]
