#!/usr/bin/env python3
# Copyright (c) 2018 Bradley Denby
# Copyright (c) 2023-2023 The Navcoin Core developers
# Distributed under the MIT software license. See the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Test transaction behaviors under the Dandelion spreading policy

NOTE: check link for basis of this test:
https://github.com/digibyte/digibyte/blob/master/test/functional/p2p_dandelion.py

Loop behavior:
    Stem:  0 --> 1 --> 2 --> 0 where each node supports Dandelion++
    Probe: TestNode --> 0
    Wait ~1 second after creating the tx (this should be enough time for the
    tx to propogate through the network with a regular mempool tx), then
    Assert that Node 0 does not reply with tx
"""

import time

from test_framework.messages import (
        CInv,
        msg_getdata,
        msg_mempool,
        MSG_DTX,
)
from test_framework.p2p import P2PInterface
from test_framework.test_framework import BitcoinTestFramework
from test_framework.wallet import MiniWallet

class DandelionLoopTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        # Make sure we are whitelisted
        self.extra_args = [
            ["-whitelist=all@127.0.0.1"],
            ["-whitelist=all@127.0.0.1"],
            ["-whitelist=all@127.0.0.1"],
        ]

    def setup_network(self):
        self.setup_nodes()
        # Tests 1,2,3: 0 --> 1 --> 2 --> 0
        self.connect_nodes(0, 1)
        self.connect_nodes(1, 2)
        self.connect_nodes(2, 0)

    def run_test(self):
        self.log.info("Setting up")
        wallet = MiniWallet(self.nodes[0])

        self.log.info("Adding P2PInterface")
        peer = self.nodes[0].add_p2p_connection(P2PInterface())

        # There is a low probability that these tests will fail even if the
        # implementation is correct. Thus, these tests are repeated upon
        # failure. A true bug will result in repeated failures.
        self.log.info("Starting dandelion tests")

        self.log.info("Create the tx on node 2")
        tx = wallet.send_self_transfer(from_node=self.nodes[1])
        txid = int(tx['txid'], 16)
        self.log.info("Sent tx with txid {}".format(txid))

        # Wait for the nodes to sync mempools
        time.sleep(1)

        # Request for the mempool update
        peer.send_and_ping(msg_mempool())

        # Create and send msg_getdata for the tx
        msg = msg_getdata()
        msg.inv.append(CInv(t=MSG_DTX, h=txid))
        peer.send_and_ping(msg)

        assert peer.last_message.get("notfound")

if __name__ == "__main__":
    DandelionLoopTest().main()
