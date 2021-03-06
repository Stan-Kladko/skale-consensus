/*
    Copyright (C) 2018-2019 SKALE Labs

    This file is part of skale-consensus.

    skale-consensus is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    skale-consensus is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with skale-consensus.  If not, see <https://www.gnu.org/licenses/>.

    @file BlockProposal.cpp
    @author Stan Kladko
    @date 2018
*/


#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>


#include "SkaleCommon.h"
#include "Log.h"

#include <network/Utils.h>
#include "exceptions/FatalError.h"
#include "exceptions/InvalidArgumentException.h"
#include "exceptions/ParsingException.h"
#include "crypto/SHAHash.h"
#include "crypto/CryptoManager.h"
#include "network/Buffer.h"
#include "node/ConsensusEngine.h"
#include "exceptions/ExitRequestedException.h"
#include "headers/BlockProposalHeader.h"
#include "chains/Schain.h"
#include "pendingqueue/PendingTransactionsAgent.h"
#include "datastructures/BlockProposalFragment.h"
#include "datastructures/BlockProposalFragmentList.h"
#include "headers/BlockProposalRequestHeader.h"


#include "Transaction.h"
#include "TransactionList.h"
#include "PartialHashesList.h"
#include "BlockProposal.h"


using namespace std;

ptr<SHAHash> BlockProposal::getHash() {
    assert(hash);
    return hash;
}



void BlockProposal::calculateHash() {
    CryptoPP::SHA256 sha3;
    sha3.Update(reinterpret_cast < uint8_t * > ( &proposerIndex), sizeof(proposerIndex));
    sha3.Update(reinterpret_cast < uint8_t * > ( &proposerNodeID), sizeof(proposerNodeID));
    sha3.Update(reinterpret_cast < uint8_t * > ( &schainID      ), sizeof(schainID));
    sha3.Update(reinterpret_cast < uint8_t * > ( &blockID       ), sizeof(blockID));
    sha3.Update(reinterpret_cast < uint8_t * > ( &transactionCount ), sizeof(transactionCount));
    sha3.Update(reinterpret_cast < uint8_t * > ( &timeStamp ), sizeof(timeStamp));
    sha3.Update(reinterpret_cast < uint8_t * > ( &timeStampMs ), sizeof(timeStampMs));

    // export into 8-bit unsigned values, most significant bit first:

    auto sr = Utils::u256ToBigEndianArray(getStateRoot());
    auto v = Utils::carray2Hex(sr->data(),  sr->size());
    sha3.Update((unsigned char *) v->data(), v->size());
    if (transactionList->size() > 0) {
        auto merkleRoot = transactionList->calculateTopMerkleRoot();
        sha3.Update(merkleRoot->getHash()->data(), SHA_HASH_LEN);
    }
    auto buf = make_shared<array<uint8_t, SHA_HASH_LEN>>();
    sha3.Final(buf->data());
    hash = make_shared<SHAHash>(buf);
};


BlockProposal::BlockProposal(uint64_t _timeStamp, uint32_t _timeStampMs) : timeStamp(_timeStamp),
                                                                           timeStampMs(_timeStampMs) {
    proposerNodeID = 0;
};

BlockProposal::BlockProposal(schain_id _sChainId, node_id _proposerNodeId, block_id _blockID,
                             schain_index _proposerIndex, ptr<TransactionList> _transactions, u256 _stateRoot,
                             uint64_t _timeStamp, __uint32_t _timeStampMs, ptr<string> _signature,
                             ptr<CryptoManager> _cryptoManager)
        : schainID(_sChainId), proposerNodeID(_proposerNodeId), blockID(_blockID),
          proposerIndex(_proposerIndex), timeStamp(_timeStamp), timeStampMs(_timeStampMs),
          stateRoot(_stateRoot), transactionList(_transactions),  signature(_signature) {


    CHECK_ARGUMENT(_cryptoManager != nullptr || _signature != nullptr);
    CHECK_ARGUMENT(_cryptoManager == nullptr || _signature == nullptr);

    ASSERT(timeStamp > MODERN_TIME);
    transactionCount = transactionList->getItems()->size();
    calculateHash();

    if (_cryptoManager != nullptr) {
        _cryptoManager->signProposalECDSA(this);
    } else {
        signature = _signature;
    }
}


ptr<PartialHashesList> BlockProposal::createPartialHashesList() {

    auto s = (uint64_t) this->transactionCount * PARTIAL_SHA_HASH_LEN;
    auto t = transactionList->getItems();

    if (s > MAX_BUFFER_SIZE) {
        InvalidArgumentException("Buffer size too large", __CLASS_NAME__);
    }

    auto partialHashes = make_shared<vector<uint8_t>>(s);

    for (uint64_t i = 0; i < transactionCount; i++) {

        for (size_t j = 0; j < PARTIAL_SHA_HASH_LEN; j++) {
            partialHashes->at(i * PARTIAL_SHA_HASH_LEN + j) = t->at(i)->getHash()->at(j);
        }
    }

    return make_shared<PartialHashesList>((transaction_count) transactionCount, partialHashes);

}

BlockProposal::~BlockProposal() {

}

block_id BlockProposal::getBlockID() const {
    return blockID;
}


schain_index BlockProposal::getProposerIndex() const {
    return proposerIndex;
}


node_id BlockProposal::getProposerNodeID() const {
    return proposerNodeID;
}


ptr<TransactionList> BlockProposal::getTransactionList() {
    return transactionList;
}

schain_id BlockProposal::getSchainID() const {
    return schainID;
}

transaction_count BlockProposal::getTransactionCount() const {
    return transactionCount;
}

uint64_t BlockProposal::getTimeStamp() const {
    return timeStamp;
}


uint32_t BlockProposal::getTimeStampMs() const {
    return timeStampMs;
}

void BlockProposal::addSignature(ptr<string> _signature) {
    LOCK(m)
    CHECK_ARGUMENT(_signature != nullptr)
    CHECK_STATE(signature == nullptr)
    signature = _signature;
}

ptr<string> BlockProposal::getSignature() {
    LOCK(m)
    return signature;
}

ptr<BlockProposalRequestHeader> BlockProposal::createBlockProposalHeader(Schain *_sChain,
                                                                         ptr<BlockProposal> _proposal) {


    CHECK_ARGUMENT(_sChain != nullptr);
    CHECK_ARGUMENT(_proposal != nullptr);

    LOCK(_proposal->m);

    if (_proposal->header != nullptr)
        return _proposal->header;

    _proposal->header = make_shared<BlockProposalRequestHeader>(*_sChain, _proposal);

    return _proposal->header;

}


ptr<BasicHeader> BlockProposal::createHeader() {
    return make_shared<BlockProposalHeader>(*this);
}

ptr<vector<uint8_t> > BlockProposal::serialize() {


    LOCK(m)

    if (serializedProposal != nullptr)
        return serializedProposal;


    auto blockHeader = createHeader();

    auto buf = blockHeader->toBuffer();

    CHECK_STATE(buf->getBuf()->at(sizeof(uint64_t)) == '{');
    CHECK_STATE(buf->getBuf()->at(buf->getCounter() - 1) == '}');

    auto block = make_shared<vector<uint8_t> >();

    block->insert(
            block->end(), buf->getBuf()->begin(), buf->getBuf()->begin() + buf->getCounter());


    auto serializedList = transactionList->serialize(true);
    assert(serializedList->front() == '<');
    assert(serializedList->back() == '>');


    block->insert(block->end(), serializedList->begin(), serializedList->end());

    if (transactionList->size() == 0) {
        CHECK_STATE(block->size() == buf->getCounter() + 2);
    }

    serializedProposal = block;


    assert(block->at(sizeof(uint64_t)) == '{');
    assert(block->back() == '>');

    return block;
}


ptr<BlockProposal> BlockProposal::deserialize(ptr<vector<uint8_t> > _serializedProposal,
                                              ptr<CryptoManager> _manager) {

    ptr<string> headerStr = BlockProposal::extractHeader(_serializedProposal);

    ptr<BlockProposalHeader> blockHeader;

    try {
        blockHeader = parseBlockHeader(headerStr);
    } catch (ExitRequestedException &) { throw; } catch (...) {
        throw_with_nested(ParsingException(
                "Could not parse block header: \n" + *headerStr, __CLASS_NAME__));
    }

    auto list = deserializeTransactions(blockHeader, headerStr, _serializedProposal);

    auto sig = blockHeader->getSignature();

    ASSERT(sig != nullptr);

    auto proposal = make_shared<BlockProposal>(blockHeader->getSchainID(), blockHeader->getProposerNodeId(),
                                               blockHeader->getBlockID(), blockHeader->getProposerIndex(),
                                               list, blockHeader->getStateRoot(), blockHeader->getTimeStamp(),
                                               blockHeader->getTimeStampMs(),
                                               blockHeader->getSignature(), nullptr);

    _manager->verifyProposalECDSA(proposal, blockHeader->getBlockHash(), blockHeader->getSignature());

    proposal->serializedProposal = _serializedProposal;

    return proposal;
}

ptr<BlockProposal>
BlockProposal::defragment(ptr<BlockProposalFragmentList> _fragmentList, ptr<CryptoManager> _cryptoManager) {
    try {
        return deserialize(_fragmentList->serialize(), _cryptoManager);
    } catch (exception &e) {
        Exception::logNested(e);
        throw_with_nested(InvalidStateException(__FUNCTION__, __CLASS_NAME__));
    }
}

ptr<BlockProposalFragment> BlockProposal::getFragment(uint64_t _totalFragments, fragment_index _index) {

    CHECK_ARGUMENT(_totalFragments > 0);
    CHECK_ARGUMENT(_index <= _totalFragments);
    LOCK(m)

    auto sBlock = serialize();
    auto blockSize = sBlock->size();

    uint64_t fragmentStandardSize;

    if (blockSize % _totalFragments == 0) {
        fragmentStandardSize = sBlock->size() / _totalFragments;
    } else {
        fragmentStandardSize = sBlock->size() / _totalFragments + 1;
    }

    auto startIndex = fragmentStandardSize * ((uint64_t) _index - 1);


    auto fragmentData = make_shared<vector<uint8_t>>();
    fragmentData->reserve(fragmentStandardSize + 2);

    fragmentData->push_back('<');


    if (_index == _totalFragments) {
        fragmentData->insert(fragmentData->begin() + 1, sBlock->begin() + startIndex,
                             sBlock->end());

    } else {
        fragmentData->insert(fragmentData->begin() + 1, sBlock->begin() + startIndex,
                             sBlock->begin() + startIndex + fragmentStandardSize);
    }

    fragmentData->push_back('>');


    return make_shared<BlockProposalFragment>(getBlockID(), _totalFragments, _index, fragmentData,
                                              sBlock->size(), getHash()->toHex());
}

ptr<TransactionList> BlockProposal::deserializeTransactions(ptr<BlockProposalHeader> _header,
                                                            ptr<string> _headerString,
                                                            ptr<vector<uint8_t> > _serializedBlock) {

    auto headerSize = _headerString->size();

    ptr<TransactionList> list;
    try {
        list = TransactionList::deserialize(
                _header->getTransactionSizes(), _serializedBlock, headerSize + sizeof(headerSize), true);
    } catch (Exception &e) {
        throw_with_nested(
                ParsingException("Could not parse transactions after header. Header: \n" + *_headerString +
                                 " Transactions size:" + to_string(_serializedBlock->size()),
                                 __CLASS_NAME__)
        );
    }

    return list;

}


ptr<string> BlockProposal::extractHeader(ptr<vector<uint8_t> > _serializedBlock) {

    CHECK_ARGUMENT(_serializedBlock != nullptr);

    uint64_t headerSize = 0;

    auto size = _serializedBlock->size();

    CHECK_ARGUMENT2(
            size >= sizeof(headerSize) + 2, "Serialized block too small:" + to_string(size));

    using boost::iostreams::array_source;
    using boost::iostreams::stream;

    array_source src((char *) _serializedBlock->data(), _serializedBlock->size());

    stream<array_source> in(src);

    in.read((char *) &headerSize, sizeof(headerSize)); /* Flawfinder: ignore */

    CHECK_STATE2(headerSize >= 2 && headerSize + sizeof(headerSize) <= _serializedBlock->size(),
                 "Invalid header size" + to_string(headerSize));


    CHECK_STATE(headerSize <= MAX_BUFFER_SIZE);

    CHECK_STATE(_serializedBlock->at(headerSize + sizeof(headerSize)) == '<');
    CHECK_STATE(_serializedBlock->at(sizeof(headerSize)) == '{');
    CHECK_STATE(_serializedBlock->back() == '>');

    auto header = make_shared<string>(headerSize, ' ');

    in.read((char *) header->c_str(), headerSize); /* Flawfinder: ignore */

    return header;

}


ptr<BlockProposalHeader> BlockProposal::parseBlockHeader(const shared_ptr<string> &header) {
    CHECK_ARGUMENT(header != nullptr);
    CHECK_ARGUMENT(header->size() > 2);
    CHECK_ARGUMENT2(header->at(0) == '{', "Block header does not start with {");
    CHECK_ARGUMENT2(header->at(header->size() - 1) == '}', "Block header does not end with }");

    auto js = nlohmann::json::parse(*header);

    return make_shared<BlockProposalHeader>(js);

}

u256 BlockProposal::getStateRoot() const {
    return stateRoot;
}
