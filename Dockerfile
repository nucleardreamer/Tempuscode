FROM debian:jessie

ARG DEBIAN_FRONTEND=noninteractive

WORKDIR /tempus

RUN apt-get update -yqq 

RUN apt-get install -yqq build-essential postgresql autotools-dev autoconf \
    libpq-dev libxml2-dev libglib2.0-dev check git \
    autoconf-archive \
    bash

COPY . /tempus

RUN sh bootstrap

RUN ./configure

RUN make all && make install

CMD /tempus/bin/circle sample_lib
# CMD while : ; do echo "${MESSAGE=Idling...}"; sleep ${INTERVAL=600}; done
