#include <stdio.h>
#include "key.h"
#include "base58.h" // CBitcoinSecret
#include "BitcoinSecret.h"
#include "BitcoinAddress.h"

int main()
{
    // std::vector<unsigned char> v(1, 239); // 要素数 1、中身は [239] なリストができる.
    // unsigned char* p = &v[0];

    ECC_Start();

    // secret_key prefix の確認.
    {
        CKey key;

        // 9 で始まる 51 文字.
        SelectParams(NETWORK_REGTEST);
        key.MakeNewKey(false);
        printf("PRIVKEY in REGTEST: %s (%d)\n", key.GetBase58stringWithNetworkSecretKeyPrefix().c_str(), strlen(key.GetBase58stringWithNetworkSecretKeyPrefix().c_str()));

        // c で始まる 52 文字.
        SelectParams(NETWORK_REGTEST);
        key.MakeNewKey(true);
        printf("PRIVKEY in REGTEST: %s (%d)\n", key.GetBase58stringWithNetworkSecretKeyPrefix().c_str(), strlen(key.GetBase58stringWithNetworkSecretKeyPrefix().c_str()));
    }
    

    // 鍵表示.
    if(0)for(int i = 0; i < 3; i++){
        // random 32bytes binary (※ただし楕円曲線として有効な範囲) (これをそのまま表示しようとすると64charsになりそうだが.)
        CKey key;
        key.MakeNewKey(false);

        // CPrivKey is a serialized private key, with all parameters included (279 bytes)
        // ※実際には 214bytes binary っぽいが？.
        CPrivKey privkey = key.GetPrivKey();
        printf("privkey.size = %d\n", privkey.size()); // 214

                                                       // public key (65bytes binary)
        CPubKey pubkey = key.GetPubKey();
        printf("pubkey.size = %d\n", pubkey.size()); // 65bytes これを文字列にしたら 130chars (HEX) になりそう

                                                     // public key hash (CHash256による生成) (uint256 (256bits) (32bytes) binary to string)
        uint256 hash = pubkey.GetPublicKeyHash(); // 256bits = 32bytes binary
        std::string hashStr = hash.ToHexString(); // 64長HEX文字列  例: "65d5f35db59351506de1da46cfc33005d5d6e2325667ed70c42f5bcb8a944b8d"
        printf("PUBLIC HASH: %s (%d)\n", hashStr.c_str(), hashStr.length());

        // ※ CBitcoinSecret は g_pCurrentParams に依存するため、先に g_pCurrentParams の指定が必要.
        SelectParams(NETWORK_MAIN);
        printf("PRIVKEY in MAINNET: %s (%d)\n", key.GetBase58stringWithNetworkSecretKeyPrefix().c_str(), strlen(key.GetBase58stringWithNetworkSecretKeyPrefix().c_str())); // ※MAINだと値が変わる.

        SelectParams(NETWORK_TESTNET);
        printf("PRIVKEY in TESTNET: %s (%d)\n", key.GetBase58stringWithNetworkSecretKeyPrefix().c_str(), strlen(key.GetBase58stringWithNetworkSecretKeyPrefix().c_str()));

        SelectParams(NETWORK_REGTEST);
        printf("PRIVKEY in REGTEST: %s (%d)\n", key.GetBase58stringWithNetworkSecretKeyPrefix().c_str(), strlen(key.GetBase58stringWithNetworkSecretKeyPrefix().c_str()));
    }



    ECC_Stop();
    return 0;
}
