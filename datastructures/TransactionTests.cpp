//
// Created by kladko on 7/11/19.
//


#include "../SkaleCommon.h"
#include "Transaction.h"
#include "PendingTransaction.h"
#include "TransactionList.h"



#define BOOST_PENDING_INTEGER_LOG2_HPP
#include <boost/integer/integer_log2.hpp>

#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>


#include "../thirdparty/catch.hpp"

#include "Transaction.h"


void corrupt_byte_vector(ptr<vector<uint8_t> > _in, boost::random::mt19937& _gen,
                                                     boost::random::uniform_int_distribution<>& _ubyte ) {

    int randomPosition = _ubyte( _gen ) % _in->size();
    auto b = _in->at( randomPosition );
    _in->at( randomPosition ) = b + 1;

}


ptr< PendingTransaction > create_random_transaction( uint64_t _size, boost::random::mt19937& _gen,
    boost::random::uniform_int_distribution<>& _ubyte ) {
    auto sample = make_shared< vector< uint8_t > >( _size, 0 );


    for ( uint32_t j = 0; j < sample->size(); j++ ) {
        sample->at( j ) = _ubyte( _gen );
    }


    return make_shared< PendingTransaction >( sample );
};

ptr<TransactionList > create_random_transaction_list( uint64_t _size,
    boost::random::mt19937& _gen, boost::random::uniform_int_distribution<>& _ubyte ) {
    auto sample = make_shared< vector<ptr<Transaction>>>();


    for ( uint32_t j = 0; j < _size ; j++ ) {

        auto trx = create_random_transaction(_size, _gen, _ubyte);

        REQUIRE(trx != nullptr);

        sample->push_back(trx);
    }


    return make_shared<TransactionList>(sample);
};




void test_tx_serialize_deserialize( bool _fail ) {
    boost::random::mt19937 gen;

    boost::random::uniform_int_distribution<> ubyte( 0, 255 );

    for ( int k = 0; k < 10; k++ ) {
        for ( int i = 1; i < 1000; i++ ) {
            auto t = create_random_transaction( i, gen, ubyte );

            auto out = make_shared< vector< uint8_t > >();


            t->serializeInto( out, true );

            if ( _fail ) {
                corrupt_byte_vector(out, gen, ubyte);
            }

            if ( _fail ) {
                REQUIRE_THROWS(Transaction::deserialize( out, 0, out->size(), true ) );
            } else {
                auto imp = Transaction::deserialize( out, 0, out->size(), true );
                REQUIRE( imp != nullptr );
            }
        }
    }
}


void test_tx_list_serialize_deserialize( bool _fail  ) {
    boost::random::mt19937 gen;

    boost::random::uniform_int_distribution<> ubyte( 0, 255 );

    for ( int k = 0; k < 10; k++ ) {
        for ( int i = 1; i < 10; i++ ) {
            auto t = create_random_transaction_list( i, gen, ubyte );


            auto out = t->serialize( false );

            if ( _fail ) {
                corrupt_byte_vector(out, gen, ubyte);
            }


            REQUIRE(out != nullptr);

            if ( _fail ) {
                REQUIRE_THROWS( TransactionList::deserialize(
                    t->createTransactionSizesVector(), out, 0, false ) );
            } else {
                auto imp = TransactionList::deserialize(
                    t->createTransactionSizesVector(), out, 0, false );
                REQUIRE( imp != nullptr );
            }
        }
    }
}

TEST_CASE( "Serialize/deserialize transaction", "[tx-serialize]" )


{
    SECTION( "Test successful serialize/deserialize" )


    test_tx_serialize_deserialize( false );

    SECTION( "Test corrupt serialize/deserialize" )

    test_tx_serialize_deserialize( true );

    // Test successful serialize/deserialize failure
}

TEST_CASE( "Serialize/deserialize transaction list", "[tx-list-serialize]" )


{
    SECTION( "Test successful serialize/deserialize" )


        test_tx_list_serialize_deserialize( false );

  SECTION( "Test corrupt serialize/deserialize" )

        test_tx_list_serialize_deserialize( true );

    // Test successful serialize/deserialize failure
}
