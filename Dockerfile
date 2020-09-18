FROM gcc as builder
RUN apt-get update && apt-get install -y libusb-dev && rm -rf /var/lib/apt/lists/*
RUN mkdir /src
COPY . /src
WORKDIR /src
RUN make && ls

FROM ubuntu:bionic
RUN apt-get update && apt-get install -y libusb-0.1 libreadline7 && rm -rf /var/lib/apt/lists/*
COPY --from=builder /src/mspdebug /usr/bin/
ENTRYPOINT ["/usr/bin/mspdebug"]
CMD ["--help"]
