/*
    Copyright (C) 2019 SKALE Labs

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

    @file DAProofDB.cpp
    @author Stan Kladko
    @date 2019
*/

#include "SkaleCommon.h"
#include "Agent.h"
#include "Log.h"
#include "exceptions/FatalError.h"
#include "thirdparty/json.hpp"

#include "crypto/ConsensusBLSSigShare.h"
#include "crypto/ConsensusBLSSignature.h"

#include "abstracttcpserver/ConnectionStatus.h"
#include "chains/Schain.h"
#include "node/Node.h"
#include "pendingqueue/PendingTransactionsAgent.h"
#include "crypto/ConsensusBLSSigShare.h"
#include "crypto/ConsensusSigShareSet.h"
#include "crypto/CryptoManager.h"
#include "crypto/SHAHash.h"
#include "datastructures/BlockProposal.h"
#include "datastructures/DAProof.h"
#include "datastructures/BooleanProposalVector.h"


#include "leveldb/db.h"

#include "SigDB.h"
#include "DAProofDB.h"
#include "BLSSigShare.h"
#include "BLSSignature.h"
#include "BLSSigShareSet.h"
#include "crypto/ThresholdSigShare.h"
#include "datastructures/DAProof.h"


using namespace std;


DAProofDB::DAProofDB(Schain *_sChain, string &_dirName, string &_prefix, node_id _nodeId, uint64_t _maxDBSize) :
        CacheLevelDB(_sChain, _dirName, _prefix, _nodeId, _maxDBSize, false) {
};

const string DAProofDB::getFormatVersion() {
    return "1.0";
}



bool DAProofDB::haveDAProof(ptr<BlockProposal> _proposal) {
    return keyExistsInSet(_proposal->getBlockID(), _proposal->getProposerIndex());
};

// return not-null if _daProof completes set, null otherwise (both if not enough and too much)
ptr<BooleanProposalVector> DAProofDB::addDAProof(ptr<DAProof> _daProof) {

    CHECK_ARGUMENT(_daProof != nullptr);


    LOCK(daProofMutex)

    LOG(trace, "Adding daProof");

    auto daProofSet = this->writeStringToSet(*_daProof->getThresholdSig()->toString(),
                                             _daProof->getBlockId(), _daProof->getProposerIndex());


    if (daProofSet == nullptr) {
        return nullptr;
    }

    CHECK_STATE(daProofSet->size() == requiredSigners);

    auto proposalVector = make_shared<BooleanProposalVector>(node_count(totalSigners), daProofSet);





    LOG(trace, "Created proposal vector");

    return proposalVector;
}

bool DAProofDB::isEnoughProofs(block_id _blockID) {
    return isEnough(_blockID);
}





