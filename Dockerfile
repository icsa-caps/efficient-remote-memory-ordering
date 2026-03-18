FROM ubuntu:22.04

WORKDIR /top

RUN apt update
RUN apt install -y git
RUN apt install -y wget
RUN apt install -y cmake
RUN apt install -y python3
RUN apt install -y python3-pip
RUN apt install -y vim

