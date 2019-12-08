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

    @file ReceivedBlockProposalsDatabase.h
    @author Stan Kladko
    @date 2018
*/

#pragma once


class BlockProposalSet;

class PartialHashesList;

class Schain;

class BooleanProposalVector;

#include "CacheLevelDB.h"

class BlockProposalDB : public CacheLevelDB {

    Schain* sChain;

    recursive_mutex proposalMutex;

    block_id oldBlockID;

    map<block_id, ptr<BlockProposalSet>> proposedBlockSets;


    ptr<BlockProposalSet> getProposedBlockSet(block_id _blockID);

public:


    ptr<BlockProposal> getBlockProposal(block_id _blockID, schain_index _proposerIndex);

    BlockProposalDB(string &_dirName, string &_prefix, node_id _nodeId, uint64_t _maxDBSize,
                    Schain &_sChain);

    void addBlockProposal(ptr<BlockProposal> _proposal);

    bool addDAProof(ptr<DAProof> _proof);

    const string getFormatVersion();

};



