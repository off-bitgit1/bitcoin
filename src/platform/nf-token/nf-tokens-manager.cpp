// Copyright (c) 2014-2018 Crown Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/transaction.h"
#include "chain.h"

#include "platform/platform-utils.h"
#include "platform/specialtx.h"
#include "platform/platform-db.h"
#include "nf-tokens-manager.h"

namespace Platform
{
    using NftIndexByProtocolAndOwnerId = NfTokensIndexSet::index<Tags::ProtocolIdOwnerId>::type;
    using NftIndexByProtocolId = NfTokensIndexSet::index<Tags::ProtocolId>::type;
    using NftIndexByOwnerId = NfTokensIndexSet::index<Tags::OwnerId>::type;

    /*static*/ std::unique_ptr<NfTokensManager> NfTokensManager::s_instance;

    NfTokensManager::NfTokensManager()
    {
        auto protoSupplyHandler = [this](uint64_t protocolId, std::size_t totalSupply) -> bool
        {
            m_protocolsTotalSupply[protocolId] = totalSupply;
            return true;
        };

        if (PlatformDb::Instance().OptimizeSpeed())
        {
            PlatformDb::Instance().ProcessPlatformDbGuts([&, this](const leveldb::Iterator & dbIt) -> bool
            {
                if (!PlatformDb::Instance().ProcessNftProtosSupply(dbIt, protoSupplyHandler))
                {
                    return false;
                }
                return PlatformDb::Instance().ProcessNftIndex(dbIt, [this](NfTokenIndex nftIndex) -> bool
                {
                    return m_nfTokensIndexSet.emplace(std::move(nftIndex)).second;
                });
            });
        }
        else /// PlatformDb::Instance().OptimizeRam() is on
        {
            PlatformDb::Instance().ProcessPlatformDbGuts([&](const leveldb::Iterator & dbIt) -> bool
            {
                PlatformDb::Instance().ProcessNftProtosSupply(dbIt, protoSupplyHandler);
                return true;
            });
        }
    }

    bool NfTokensManager::AddNfToken(const NfToken & nfToken, const CTransaction & tx, const CBlockIndex * pindex)
    {
        assert(nfToken.tokenProtocolId != NfToken::UNKNOWN_TOKEN_PROTOCOL);
        assert(!nfToken.tokenId.IsNull());
        assert(!nfToken.tokenOwnerKeyId.IsNull());
        assert(!nfToken.metadataAdminKeyId.IsNull());
        assert(pindex != nullptr);
        assert(!tx.GetHash().IsNull());

        std::shared_ptr<NfToken> nfTokenPtr(new NfToken(nfToken));
        NfTokenIndex nftIndex(pindex, tx.GetHash(), nfTokenPtr);
        auto itRes = m_nfTokensIndexSet.emplace(std::move(nftIndex));

        if (itRes.second)
        {
            NfTokenDiskIndex nftDiskIndex(*pindex->phashBlock, pindex, tx.GetHash(), nfTokenPtr);
            PlatformDb::Instance().WriteNftDiskIndex(nftDiskIndex);
            this->UpdateTotalSupply(nfTokenPtr->tokenProtocolId, true);
        }
        return itRes.second;
    }

    NfTokenIndex NfTokensManager::GetNfTokenIndex(uint64_t protocolId, const uint256 & tokenId)
    {
        assert(protocolId != NfToken::UNKNOWN_TOKEN_PROTOCOL);
        assert(!tokenId.IsNull());

        NfTokensIndexSet::const_iterator it = m_nfTokensIndexSet.find(std::make_tuple(protocolId, tokenId));
        if (it != m_nfTokensIndexSet.end())
        {
            return *it;
        }

        /// PlatformDb::Instance().OptimizeRam() is on
        return GetNftIndexFromDb(protocolId, tokenId);
    }

    NfTokenIndex NfTokensManager::GetNfTokenIndex(const uint256 & regTxId)
    {
        assert(!regTxId.IsNull());

        if (PlatformDb::Instance().OptimizeSpeed())
        {
            const auto &regTxIndex = m_nfTokensIndexSet.get<Tags::RegTxHash>();
            const auto it = regTxIndex.find(regTxId);
            if (it != regTxIndex.end())
            {
                return *it;
            }
        }
        else /// PlatformDb::Instance().OptimizeRam() is on
        {
            std::string error = std::string(__func__) + " is implemented only for speed optimized node instances. Change the conf and restart your node.";
            throw std::runtime_error(error);
        }
    }

    bool NfTokensManager::Contains(uint64_t protocolId, const uint256 & tokenId, int height)
    {
        assert(protocolId != NfToken::UNKNOWN_TOKEN_PROTOCOL);
        assert(!tokenId.IsNull());
        assert(height >= 0);

        auto nfTokenIdx = this->GetNfTokenIndex(protocolId, tokenId);
        if (!nfTokenIdx.IsNull())
            return nfTokenIdx.BlockIndex()->nHeight <= height;
        return false;
    }

    bool NfTokensManager::Contains(uint64_t protocolId, const uint256 & tokenId)
    {
        assert(protocolId != NfToken::UNKNOWN_TOKEN_PROTOCOL);
        assert(!tokenId.IsNull());
        return this->Contains(protocolId, tokenId, m_tipHeight);
    }

    CKeyID NfTokensManager::OwnerOf(uint64_t protocolId, const uint256 & tokenId)
    {
        assert(protocolId != NfToken::UNKNOWN_TOKEN_PROTOCOL);
        assert(!tokenId.IsNull());

        NfTokensIndexSet::const_iterator it = m_nfTokensIndexSet.find(std::make_tuple(protocolId, tokenId));
        if (it != m_nfTokensIndexSet.end())
        {
            return it->NfTokenPtr()->tokenOwnerKeyId;
        }

        /// PlatformDb::Instance().OptimizeRam() is on
        return GetNftIndexFromDb(protocolId, tokenId).NfTokenPtr()->tokenOwnerKeyId;
    }

    std::size_t NfTokensManager::BalanceOf(uint64_t protocolId, const CKeyID & ownerId) const
    {
        // TODO: put my addresses balance into db
        assert(protocolId != NfToken::UNKNOWN_TOKEN_PROTOCOL);
        assert(!ownerId.IsNull());

        if (PlatformDb::Instance().OptimizeRam())
        {
            std::size_t count = 0;
            PlatformDb::Instance().ProcessNftIndexGutsOnly([&](NfTokenIndex nftIndex) -> bool
            {
                if (nftIndex.NfTokenPtr()->tokenProtocolId == protocolId &&
                    nftIndex.NfTokenPtr()->tokenOwnerKeyId == ownerId)
                {
                    count++;
                }
            });
            return count;
        }

        /// PlatformDb::Instance().OptimizeSpeed() is on
        const NftIndexByProtocolAndOwnerId & protocolOwnerIndex = m_nfTokensIndexSet.get<Tags::ProtocolIdOwnerId>();
        return protocolOwnerIndex.count(std::make_tuple(protocolId, ownerId));
    }

    std::size_t NfTokensManager::BalanceOf(const CKeyID & ownerId) const
    {
        // TODO: put my addresses balance into db
        assert(!ownerId.IsNull());

        if (PlatformDb::Instance().OptimizeRam())
        {
            std::size_t count = 0;
            PlatformDb::Instance().ProcessNftIndexGutsOnly([&](NfTokenIndex nftIndex) -> bool
            {
                if (nftIndex.NfTokenPtr()->tokenOwnerKeyId == ownerId)
                    count++;
            });
            return count;
        }

        /// PlatformDb::Instance().OptimizeSpeed() is on
        const NftIndexByOwnerId & ownerIndex = m_nfTokensIndexSet.get<Tags::OwnerId>();
        return ownerIndex.count(ownerId);
    }

    std::vector<std::weak_ptr<const NfToken> > NfTokensManager::NfTokensOf(uint64_t protocolId, const CKeyID & ownerId) const
    {
        assert(protocolId != NfToken::UNKNOWN_TOKEN_PROTOCOL);
        assert(!ownerId.IsNull());

        const NftIndexByProtocolAndOwnerId & protocolOwnerIndex = m_nfTokensIndexSet.get<Tags::ProtocolIdOwnerId>();
        const auto range = protocolOwnerIndex.equal_range(std::make_tuple(protocolId, ownerId));

        std::vector<std::weak_ptr<const NfToken> > nfTokens;
        nfTokens.reserve(std::distance(range.first, range.second));
        std::for_each(range.first, range.second, [&](const NfTokenIndex & nfTokenIdx)
        {
            nfTokens.emplace_back(nfTokenIdx.NfTokenPtr());
        });

        return nfTokens;
    }

    std::vector<std::weak_ptr<const NfToken> > NfTokensManager::NfTokensOf(const CKeyID & ownerId) const
    {
        assert(!ownerId.IsNull());

        const NftIndexByOwnerId & ownerIndex = m_nfTokensIndexSet.get<Tags::OwnerId>();
        const auto range = ownerIndex.equal_range(ownerId);

        std::vector<std::weak_ptr<const NfToken> > nfTokens;
        nfTokens.reserve(std::distance(range.first, range.second));
        std::for_each(range.first, range.second, [&](const NfTokenIndex & nfTokenIdx)
        {
            nfTokens.emplace_back(nfTokenIdx.NfTokenPtr());
        });

        return nfTokens;
    }

    std::vector<uint256> NfTokensManager::NfTokenIdsOf(uint64_t protocolId, const CKeyID & ownerId) const
    {
        assert(protocolId != NfToken::UNKNOWN_TOKEN_PROTOCOL);
        assert(!ownerId.IsNull());

        const NftIndexByProtocolAndOwnerId & protocolOwnerIndex = m_nfTokensIndexSet.get<Tags::ProtocolIdOwnerId>();
        const auto range = protocolOwnerIndex.equal_range(std::make_tuple(protocolId, ownerId));

        std::vector<uint256> nfTokenIds;
        nfTokenIds.reserve(std::distance(range.first, range.second));
        std::for_each(range.first, range.second, [&](const NfTokenIndex & nfTokenIdx)
        {
            nfTokenIds.emplace_back(nfTokenIdx.NfTokenPtr()->tokenId);
        });
        return nfTokenIds;
    }

    std::vector<uint256> NfTokensManager::NfTokenIdsOf(const CKeyID & ownerId) const
    {
        assert(!ownerId.IsNull());

        const NftIndexByOwnerId & ownerIndex = m_nfTokensIndexSet.get<Tags::OwnerId>();
        const auto range = ownerIndex.equal_range(ownerId);

        std::vector<uint256> nfTokenIds;
        nfTokenIds.reserve(std::distance(range.first, range.second));
        std::for_each(range.first, range.second, [&](const NfTokenIndex & nfTokenIdx)
        {
            nfTokenIds.emplace_back(nfTokenIdx.NfTokenPtr()->tokenId);
        });
        return nfTokenIds;
    }

    std::size_t NfTokensManager::TotalSupply() const
    {
        return TotalSupply(NfToken::UNKNOWN_TOKEN_PROTOCOL);
    }

    std::size_t NfTokensManager::TotalSupply(uint64_t protocolId) const
    {
        auto it = m_protocolsTotalSupply.find(protocolId);
        if (it == m_protocolsTotalSupply.end())
        {
            if (protocolId != NfToken::UNKNOWN_TOKEN_PROTOCOL)
                throw std::runtime_error("Unknown protocol ID: " + ProtocolName(protocolId).ToString());
            return 0;
        }
        return it->second;
    }

    void NfTokensManager::ProcessFullNftIndexRange(std::function<bool(const NfTokenIndex &)> nftIndexHandler) const
    {
        if (PlatformDb::Instance().OptimizeSpeed())
        {
            for (const auto & nftIndex : m_nfTokensIndexSet)
            {
                if (!nftIndexHandler(nftIndex))
                    LogPrintf("%s: NFT index processing failed.", __func__);
            }
        }
        else /// PlatformDb::Instance().OptimizeRam() is on
        {
            auto dbHandler = [&](NfTokenIndex nftIndex) -> bool
            {
                if (!nftIndexHandler(nftIndex))
                {
                    LogPrintf("%s: NFT index processing failed.", __func__);
                    return false;
                }
                return true;
            };

            PlatformDb::Instance().ProcessNftIndexGutsOnly(dbHandler);
        }
    }

    void NfTokensManager::ProcessNftIndexRangeByHeight(std::function<bool(const NfTokenIndex &)> nftIndexHandler,
                                                       int height,
                                                       int count,
                                                       int startFrom) const
    {
        if (PlatformDb::Instance().OptimizeSpeed())
        {
            auto originalRange = m_nfTokensIndexSet.get<Tags::Height>().range(
                    bmx::unbounded,
                    [&](int curHeight) { return curHeight <= height; }
            );

            long rangeSize = std::distance(originalRange.first, originalRange.second);
            assert(rangeSize >= 0);

            long reverseBegin = rangeSize < startFrom ? rangeSize : startFrom;
            long reverseEnd = rangeSize < startFrom + count ? 0 : reverseBegin - count;
            auto begin = std::prev(originalRange.second, reverseBegin);
            auto end = std::prev(originalRange.second, reverseEnd);

            NftIndexRange finalRange(begin, end);
            for (const auto & nftIndex : finalRange)
            {
                if (!nftIndexHandler(nftIndex))
                    LogPrintf("%s: NFT index processing failed.", __func__);
            }
        }
        else /// PlatformDb::Instance().OptimizeRam() is on
        {
            std::string error = std::string(__func__) + "is implemented only for speed optimized node instances. Change the conf and restart your node.";
            throw std::runtime_error(error);
        }
    }

    void NfTokensManager::ProcessNftIndexRangeByHeight(std::function<bool(const NfTokenIndex &)> nftIndexHandler,
                                                      uint64_t nftProtoId,
                                                      int height,
                                                      int count,
                                                      int startFrom) const
    {
        if (PlatformDb::Instance().OptimizeSpeed())
        {
            auto first = m_nfTokensIndexSet.get<Tags::ProtocolIdHeight>().lower_bound(std::make_tuple(nftProtoId, 0));
            auto second = m_nfTokensIndexSet.get<Tags::ProtocolIdHeight>().upper_bound(std::make_tuple(nftProtoId, height));

            long rangeSize = std::distance(first, second);
            assert(rangeSize >= 0);

            long reverseBegin = rangeSize < startFrom ? rangeSize : startFrom;
            long reverseEnd = rangeSize < startFrom + count ? 0 : reverseBegin - count;
            auto begin = std::prev(second, reverseBegin);
            auto end = std::prev(second, reverseEnd);

            NftIndexRange finalRange(begin, end);
            for (const auto & nftIndex : finalRange)
            {
                if (!nftIndexHandler(nftIndex))
                    LogPrintf("%s: NFT index processing failed.", __func__);
            }
        }
        else /// PlatformDb::Instance().OptimizeRam() is on
        {
            std::string error = std::string(__func__) + " is implemented only for speed optimized node instances. Change the conf and restart your node.";
            throw std::runtime_error(error);
        }
    }

    void NfTokensManager::ProcessNftIndexRangeByHeight(std::function<bool(const NfTokenIndex &)> nftIndexHandler,
                                                       CKeyID keyId,
                                                       int height,
                                                       int count,
                                                       int startFrom) const
    {
        if (PlatformDb::Instance().OptimizeSpeed())
        {
            auto first = m_nfTokensIndexSet.get<Tags::OwnerId>().lower_bound(std::make_tuple(keyId, 0));
            auto second = m_nfTokensIndexSet.get<Tags::OwnerId>().upper_bound(std::make_tuple(keyId, height));

            long rangeSize = std::distance(first, second);
            assert(rangeSize >= 0);

            long reverseBegin = rangeSize < startFrom ? rangeSize : startFrom;
            long reverseEnd = rangeSize < startFrom + count ? 0 : reverseBegin - count;
            auto begin = std::prev(second, reverseBegin);
            auto end = std::prev(second, reverseEnd);

            NftIndexRange finalRange(begin, end);
            for (const auto & nftIndex : finalRange)
            {
                if (!nftIndexHandler(nftIndex))
                    LogPrintf("%s: NFT index processing failed.", __func__);
            }
        }
        else /// PlatformDb::Instance().OptimizeRam() is on
        {
            std::string error = std::string(__func__) + " is implemented only for speed optimized node instances. Change the conf and restart your node.";
            throw std::runtime_error(error);
        }
    }

    void NfTokensManager::ProcessNftIndexRangeByHeight(std::function<bool(const NfTokenIndex &)> nftIndexHandler,
                                                       uint64_t nftProtoId,
                                                       CKeyID keyId,
                                                       int height,
                                                       int count,
                                                       int startFrom) const
    {
        if (PlatformDb::Instance().OptimizeSpeed())
        {
            auto first = m_nfTokensIndexSet.get<Tags::ProtocolIdOwnerId>().lower_bound(std::make_tuple(nftProtoId, keyId, 0));
            auto second = m_nfTokensIndexSet.get<Tags::ProtocolIdOwnerId>().upper_bound(std::make_tuple(nftProtoId, keyId, height));

            long rangeSize = std::distance(first, second);
            assert(rangeSize >= 0);

            long reverseBegin = rangeSize < startFrom ? rangeSize : startFrom;
            long reverseEnd = rangeSize < startFrom + count ? 0 : reverseBegin - count;
            auto begin = std::prev(second, reverseBegin);
            auto end = std::prev(second, reverseEnd);

            NftIndexRange finalRange(begin, end);
            for (const auto & nftIndex : finalRange)
            {
                if (!nftIndexHandler(nftIndex))
                    LogPrintf("%s: NFT index processing failed.", __func__);
            }
        }
        else /// PlatformDb::Instance().OptimizeRam() is on
        {
            std::string error = std::string(__func__) + " is implemented only for speed optimized node instances. Change the conf and restart your node.";
            throw std::runtime_error(error);
        }
    }

    bool NfTokensManager::Delete(uint64_t protocolId, const uint256 & tokenId)
    {
        return Delete(protocolId, tokenId, m_tipHeight);
    }

    bool NfTokensManager::Delete(uint64_t protocolId, const uint256 & tokenId, int height)
    {
        assert(protocolId != NfToken::UNKNOWN_TOKEN_PROTOCOL);
        assert(!tokenId.IsNull());
        assert(height >= 0);

        auto it = m_nfTokensIndexSet.find(std::make_tuple(protocolId, tokenId));
        if (it != m_nfTokensIndexSet.end() && it->BlockIndex()->nHeight <= height)
        {
            m_nfTokensIndexSet.erase(it);
            PlatformDb::Instance().EraseNftDiskIndex(protocolId, tokenId);
            this->UpdateTotalSupply(protocolId, false);
            return true;
        }
        return false;
    }

    void NfTokensManager::UpdateBlockTip(const CBlockIndex * pindex)
    {
        assert(pindex != nullptr);
        if (pindex != nullptr)
        {
            m_tipHeight = pindex->nHeight;
            m_tipBlockHash = pindex->GetBlockHash();
        }
    }

    void NfTokensManager::UpdateTotalSupply(uint64_t protocolId, bool increase)
    {
        std::size_t updatedSize = increase ? ++m_protocolsTotalSupply[protocolId] : --m_protocolsTotalSupply[protocolId];
        PlatformDb::Instance().WriteTotalSupply(updatedSize, protocolId);
        if (protocolId != NfToken::UNKNOWN_TOKEN_PROTOCOL)
        {
            /// Update total supply count
            uint64_t tempTotalId = NfToken::UNKNOWN_TOKEN_PROTOCOL;
            std::size_t updatedTotalSize = increase ? ++m_protocolsTotalSupply[tempTotalId] : --m_protocolsTotalSupply[tempTotalId];
            PlatformDb::Instance().WriteTotalSupply(updatedTotalSize);
        }
    }

    NfTokenIndex NfTokensManager::GetNftIndexFromDb(uint64_t protocolId, const uint256 & tokenId)
    {
        NfTokenIndex nftIndex = PlatformDb::Instance().ReadNftIndex(protocolId, tokenId);
        if (!nftIndex.IsNull())
        {
            auto insRes = m_nfTokensIndexSet.emplace(std::move(nftIndex));
            assert(insRes.second);
            return *insRes.first;
        }
        else
        {
            LogPrintf("%s: Can't read NFT index %s:%s from the database", __func__, std::to_string(protocolId), tokenId.ToString());
            return nftIndex;
        }
    }
}
