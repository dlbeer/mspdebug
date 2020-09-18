FROM gcc as builder
RUN apt-get update && apt-get install -y libusb-dev
RUN mkdir /src
COPY . /src
WORKDIR /src
RUN make && ls

FROM ubuntu
COPY --from=builder /src/mspdebug /usr/bin/
ENTRYPOINT ['/usr/bin/mspdebug']
CMD ['--help']
