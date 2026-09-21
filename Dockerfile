# Boltzmann in C.
#
#   docker run --rm jaonoctus/ludwig --txids=<txid>      (prebuilt, 3 archs)
#   docker build -t ludwig .
#   docker run --rm ludwig --txids=<txid>
#   docker run --rm -v "$PWD:/data" ludwig --file=/data/tx.json
#
# Two stages: build against libcurl, then copy the one binary onto a bare
# Alpine image with only libcurl and the CA certificates.

FROM alpine:3.20 AS build
RUN apk add --no-cache gcc musl-dev make curl-dev
WORKDIR /src
COPY Makefile ludwig.c ./
COPY boltzmann/ boltzmann/
COPY common/ common/
COPY providers/ providers/
COPY tests/ tests/
RUN make clean && make ludwig && make check

FROM alpine:3.20
RUN apk add --no-cache libcurl ca-certificates
COPY --from=build /src/ludwig /usr/local/bin/ludwig
WORKDIR /data
ENTRYPOINT ["ludwig"]
CMD ["--help"]
