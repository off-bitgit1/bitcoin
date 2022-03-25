#!/usr/bin/env bash
#
# Copyright (c) 2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

export DOCKER_NAME_TAG=ubuntu:14.04
export PACKAGES="python3-zmq qtbase5-dev qttools5-dev-tools libicu-dev libpng-dev libevent-dev bsdmainutils libboost-filesystem-dev libboost-test-dev libboost-thread-dev libdb5.1++-dev libminiupnpc-dev libzmq3-dev libqrencode-dev"
export RUN_INTEGRATION_TESTS=false
export GOAL="install"
export BITCOIN_CONFIG="--enable-zmq --with-incompatible-bdb --with-gui=no"
