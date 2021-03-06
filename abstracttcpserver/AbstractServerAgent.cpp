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

    @file AbstractServerAgent.cpp
    @author Stan Kladko
    @date 2018
*/

#include "crypto/bls_include.h"
#include "SkaleCommon.h"
#include "Agent.h"
#include "Log.h"

#include "exceptions/FatalError.h"
#include "exceptions/ExitRequestedException.h"

#include "thirdparty/json.hpp"

#include "abstracttcpserver/ConnectionStatus.h"

#include "node/Node.h"
#include "chains/Schain.h"

#include "exceptions/OldBlockIDException.h"
#include "exceptions/FutureBlockIDException.h"
#include "exceptions/CouldNotReadPartialDataHashesException.h"

#include "libBLS/bls/BLSPrivateKeyShare.h"
#include "libBLS/bls/BLSSignature.h"
#include "libBLS/bls/BLSPublicKey.h"


#include "crypto/SHAHash.h"
#include "datastructures/BlockProposalSet.h"
#include "blockproposal/pusher/BlockProposalClientAgent.h"
#include "db/BlockProposalDB.h"
#include "headers/MissingTransactionsRequestHeader.h"
#include "pendingqueue/PendingTransactionsAgent.h"
#include "network/TransportNetwork.h"
#include "network/Sockets.h"
#include "network/IO.h"
#include "network/Buffer.h"
#include "network/ServerConnection.h"
#include "network/TCPServerSocket.h"
#include "datastructures/PartialHashesList.h"


#include "AbstractServerAgent.h"


void AbstractServerAgent::pushToQueueAndNotifyWorkers(ptr<ServerConnection> connectionEnvelope) {
    lock_guard<mutex> lock(incomingTCPConnectionsMutex);
    incomingTCPConnections.push(connectionEnvelope);
    incomingTCPConnectionsCond.notify_all();
}

ptr<ServerConnection> AbstractServerAgent::workerThreadWaitandPopConnection() {

    unique_lock<mutex> mlock(incomingTCPConnectionsMutex);

    while (incomingTCPConnections.empty()) {
        incomingTCPConnectionsCond.wait(mlock);
        getSchain()->getNode()->exitCheck();
    }


    ASSERT(!incomingTCPConnections.empty());

    ptr<ServerConnection> ce = incomingTCPConnections.front();

    incomingTCPConnections.pop();

    return ce;

}


void AbstractServerAgent::workerThreadConnectionProcessingLoop(void *_params) {

    AbstractServerAgent *server = (reinterpret_cast < AbstractServerAgent * > ( _params ));

    server->waitOnGlobalStartBarrier();


    LOG(trace, "Started server loop");


    while (!server->getNode()->isExitRequested()) {

        ptr<ServerConnection> connection = nullptr;
        try {

            connection = server->workerThreadWaitandPopConnection();
            server->processNextAvailableConnection(connection);;
            connection->closeConnection();
        } catch (exception &e) {
            Exception::logNested(e);
            if (connection != nullptr)
                connection->closeConnection();
        }
    }
}


void AbstractServerAgent::send(ptr<ServerConnection> _connectionEnvelope,
                               ptr<Header> _header) {


    ASSERT(_connectionEnvelope);
    ASSERT(_header);
    ASSERT(_header->isComplete());
    auto buf = _header->toBuffer();

    getSchain()->getIo()->writeBuf(_connectionEnvelope->getDescriptor(), buf);
}

AbstractServerAgent::AbstractServerAgent(const string &_name, Schain &_schain,
                                         ptr<TCPServerSocket> _socket)
        : Agent(_schain, true), name(_name), socket(_socket), networkReadThread(nullptr) {

    logThreadLocal_ = _schain.getNode()->getLog();
}

AbstractServerAgent::~AbstractServerAgent() {
    this->networkReadThread->join();
}

void AbstractServerAgent::acceptTCPConnectionsLoop() {

    setThreadName(name, getSchain()->getNode()->getConsensusEngine());

    waitOnGlobalStartBarrier();

    struct sockaddr_in clientAddress;
    socklen_t sizeOfClientAddress = sizeof(clientAddress);
    ASSERT(this->socket > 0);
    auto s = this->socket->getDescriptor();
    ASSERT(s > 0);
    try {

        while (!getSchain()->getNode()->isExitRequested()) {

            int newConnection = accept(s, (sockaddr *) &clientAddress, &sizeOfClientAddress);

            if (getSchain()->getNode()->isExitRequested()) {
                return;
            }

            if (newConnection < 0) {
                BOOST_THROW_EXCEPTION(NetworkProtocolException("accept failed:" + string(strerror(errno)), __CLASS_NAME__));
            }

            char *ip(inet_ntoa(clientAddress.sin_addr));

            this->pushToQueueAndNotifyWorkers(make_shared<ServerConnection>(newConnection, make_shared<string>(ip)));

        }
    } catch (FatalError *e) {
        getNode()->exitOnFatalError(e->getMessage());
    }
}

void AbstractServerAgent::createNetworkReadThread() {

    LOG(trace, name + " Starting TCP server network read loop");
    networkReadThread = make_shared<thread>(std::bind(&AbstractServerAgent::acceptTCPConnectionsLoop, this));
    LOG(trace, name + " Started TCP server network read loop");

}



void AbstractServerAgent::notifyAllConditionVariables() {
    Agent::notifyAllConditionVariables();
    LOG(trace, "Notifying TCP cond" + to_string((uint64_t) (void *) &incomingTCPConnectionsCond));
    incomingTCPConnectionsCond.notify_all();

}










