#include <boost/test/unit_test.hpp>

#include <eos/chain/chain_controller.hpp>
#include <eos/chain/exceptions.hpp>
#include <eos/chain/account_object.hpp>
#include <eos/chain/key_value_object.hpp>

#include <eos/native_contract/producer_objects.hpp>

#include <eos/utilities/tempdir.hpp>

#include <fc/crypto/digest.hpp>

#include <boost/test/unit_test.hpp>
#include <boost/range/algorithm/find.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <boost/range/algorithm/permutation.hpp>

#include "../common/database_fixture.hpp"

using namespace eos;
using namespace chain;

BOOST_AUTO_TEST_SUITE(native_contract_tests)

//Simple test of account creation
BOOST_FIXTURE_TEST_CASE(create_account, testing_fixture)
{ try {
      Make_Blockchain(chain);
      chain.produce_blocks(10);

      BOOST_CHECK_EQUAL(chain.get_liquid_balance("init1"), Asset(100000));

      Make_Account(chain, joe, init1, Asset(1000));

      { // test in the pending state
         BOOST_CHECK_EQUAL(chain.get_liquid_balance("joe"), Asset(1000));
         BOOST_CHECK_EQUAL(chain.get_liquid_balance("init1"), Asset(100000 - 1000));

         const auto& joe_owner_authority = chain_db.get<permission_object, by_owner>(boost::make_tuple("joe", "owner"));
         BOOST_CHECK_EQUAL(joe_owner_authority.auth.threshold, 1);
         BOOST_CHECK_EQUAL(joe_owner_authority.auth.accounts.size(), 0);
         BOOST_CHECK_EQUAL(joe_owner_authority.auth.keys.size(), 1);
         BOOST_CHECK_EQUAL(string(joe_owner_authority.auth.keys[0].key), string(joe_public_key));
         BOOST_CHECK_EQUAL(joe_owner_authority.auth.keys[0].weight, 1);

         const auto& joe_active_authority =
            chain_db.get<permission_object, by_owner>(boost::make_tuple("joe", "active"));
         BOOST_CHECK_EQUAL(joe_active_authority.auth.threshold, 1);
         BOOST_CHECK_EQUAL(joe_active_authority.auth.accounts.size(), 0);
         BOOST_CHECK_EQUAL(joe_active_authority.auth.keys.size(), 1);
         BOOST_CHECK_EQUAL(string(joe_active_authority.auth.keys[0].key), string(joe_public_key));
         BOOST_CHECK_EQUAL(joe_active_authority.auth.keys[0].weight, 1);
      }

      chain.produce_blocks(1); /// verify changes survived creating a new block
      {
         BOOST_CHECK_EQUAL(chain.get_liquid_balance("joe"), Asset(1000));
         BOOST_CHECK_EQUAL(chain.get_liquid_balance("init1"), Asset(100000 - 1000));

         const auto& joe_owner_authority = chain_db.get<permission_object, by_owner>(boost::make_tuple("joe", "owner"));
         BOOST_CHECK_EQUAL(joe_owner_authority.auth.threshold, 1);
         BOOST_CHECK_EQUAL(joe_owner_authority.auth.accounts.size(), 0);
         BOOST_CHECK_EQUAL(joe_owner_authority.auth.keys.size(), 1);
         BOOST_CHECK_EQUAL(string(joe_owner_authority.auth.keys[0].key), string(joe_public_key));
         BOOST_CHECK_EQUAL(joe_owner_authority.auth.keys[0].weight, 1);

         const auto& joe_active_authority =
         chain_db.get<permission_object, by_owner>(boost::make_tuple("joe", "active"));
         BOOST_CHECK_EQUAL(joe_active_authority.auth.threshold, 1);
         BOOST_CHECK_EQUAL(joe_active_authority.auth.accounts.size(), 0);
         BOOST_CHECK_EQUAL(joe_active_authority.auth.keys.size(), 1);
         BOOST_CHECK_EQUAL(string(joe_active_authority.auth.keys[0].key), string(joe_public_key));
         BOOST_CHECK_EQUAL(joe_active_authority.auth.keys[0].weight, 1);
      }
} FC_LOG_AND_RETHROW() }

// Verify that staking and unstaking works
BOOST_FIXTURE_TEST_CASE(stake, testing_fixture)
{ try {
   // Create account sam with default balance of 100, and stake 55 of it
   Make_Blockchain(chain);
   Make_Account(chain, sam);
   Stake_Asset(chain, sam, Asset(55).amount);
   
   // Check balances
   BOOST_CHECK_EQUAL(chain.get_staked_balance("sam"), Asset(55).amount);
   BOOST_CHECK_EQUAL(chain.get_unstaking_balance("sam"), Asset(0).amount);
   BOOST_CHECK_EQUAL(chain.get_liquid_balance("sam"), Asset(45).amount);
   
   chain.produce_blocks();
   
   // Start unstaking 20, check balances
   BOOST_CHECK_THROW(Begin_Unstake_Asset(chain, sam, Asset(56).amount), chain::message_precondition_exception);
   Begin_Unstake_Asset(chain, sam, Asset(20).amount);
   BOOST_CHECK_EQUAL(chain.get_staked_balance("sam"), Asset(35).amount);
   BOOST_CHECK_EQUAL(chain.get_unstaking_balance("sam"), Asset(20).amount);
   BOOST_CHECK_EQUAL(chain.get_liquid_balance("sam"), Asset(45).amount);
   
   // Make sure we can't liquidate early
   BOOST_CHECK_THROW(Finish_Unstake_Asset(chain, sam, Asset(10).amount), chain::message_precondition_exception);
   
   // Fast forward to when we can liquidate
   elog("Hang on, this will take a minute...");
   chain.produce_blocks(config::StakedBalanceCooldownSeconds / config::BlockIntervalSeconds + 1);
   
   BOOST_CHECK_THROW(Finish_Unstake_Asset(chain, sam, Asset(21).amount), chain::message_precondition_exception);
   BOOST_CHECK_EQUAL(chain.get_staked_balance("sam"), Asset(35).amount);
   BOOST_CHECK_EQUAL(chain.get_unstaking_balance("sam"), Asset(20).amount);
   BOOST_CHECK_EQUAL(chain.get_liquid_balance("sam"), Asset(45).amount);
   
   // Liquidate 10 of the 20 unstaking and check balances
   Finish_Unstake_Asset(chain, sam, Asset(10).amount);
   BOOST_CHECK_EQUAL(chain.get_staked_balance("sam"), Asset(35).amount);
   BOOST_CHECK_EQUAL(chain.get_unstaking_balance("sam"), Asset(10).amount);
   BOOST_CHECK_EQUAL(chain.get_liquid_balance("sam"), Asset(55).amount);
   
   // Liquidate 2 of the 10 left unstaking and check balances
   Finish_Unstake_Asset(chain, sam, Asset(2).amount);
   BOOST_CHECK_EQUAL(chain.get_staked_balance("sam"), Asset(35).amount);
   BOOST_CHECK_EQUAL(chain.get_unstaking_balance("sam"), Asset(8).amount);
   BOOST_CHECK_EQUAL(chain.get_liquid_balance("sam"), Asset(57).amount);
   
   // Ignore the 8 left in unstaking, and begin unstaking 5, which should restake the 8, and start over unstaking 5
   Begin_Unstake_Asset(chain, sam, Asset(5).amount);
   BOOST_CHECK_EQUAL(chain.get_staked_balance("sam"), Asset(38).amount);
   BOOST_CHECK_EQUAL(chain.get_unstaking_balance("sam"), Asset(5).amount);
   BOOST_CHECK_EQUAL(chain.get_liquid_balance("sam"), Asset(57).amount);
   
   // Begin unstaking 20, which should only deduct 15 from staked, since 5 was already in unstaking
   Begin_Unstake_Asset(chain, sam, Asset(20).amount);
   BOOST_CHECK_EQUAL(chain.get_staked_balance("sam"), Asset(23).amount);
   BOOST_CHECK_EQUAL(chain.get_unstaking_balance("sam"), Asset(20).amount);
   BOOST_CHECK_EQUAL(chain.get_liquid_balance("sam"), Asset(57).amount);
} FC_LOG_AND_RETHROW() }

// Simple test to verify a simple transfer transaction works
BOOST_FIXTURE_TEST_CASE(transfer, testing_fixture)
{ try {
      Make_Blockchain(chain)

      BOOST_CHECK_EQUAL(chain.head_block_num(), 0);
      chain.produce_blocks(10);
      BOOST_CHECK_EQUAL(chain.head_block_num(), 10);

      SignedTransaction trx;
      BOOST_REQUIRE_THROW(chain.push_transaction(trx), transaction_exception); // no messages
      trx.messages.resize(1);
      trx.set_reference_block(chain.head_block_id());
      trx.expiration = chain.head_block_time() + 100;
      trx.messages[0].sender = "init1";
      trx.messages[0].recipient = config::EosContractName;

      types::Transfer trans = { "init1", "init2", Asset(100), "transfer 100" };

      UInt64 value(5);
      auto packed = fc::raw::pack(value);
      auto unpacked = fc::raw::unpack<UInt64>(packed);
      BOOST_CHECK_EQUAL( value, unpacked );
      trx.messages[0].type = "Transfer";
      trx.setMessage(0, "Transfer", trans);

      auto unpack_trans = trx.messageAs<types::Transfer>(0);

      BOOST_REQUIRE_THROW(chain.push_transaction(trx), message_validate_exception); // "fail to notify receiver, init2"
      trx.messages[0].notify = {"init2"};
      trx.setMessage(0, "Transfer", trans);
      chain.push_transaction(trx);

      BOOST_CHECK_EQUAL(chain.get_liquid_balance("init1"), Asset(100000 - 100));
      BOOST_CHECK_EQUAL(chain.get_liquid_balance("init2"), Asset(100000 + 100));
      chain.produce_blocks(1);

      BOOST_REQUIRE_THROW(chain.push_transaction(trx), transaction_exception); // not unique

      Transfer_Asset(chain, init2, init1, Asset(100));
      BOOST_CHECK_EQUAL(chain.get_liquid_balance("init1"), Asset(100000));
      BOOST_CHECK_EQUAL(chain.get_liquid_balance("init2"), Asset(100000));
} FC_LOG_AND_RETHROW() }

// Simple test of creating/updating a new block producer
BOOST_FIXTURE_TEST_CASE(producer_creation, testing_fixture)
{ try {
      Make_Blockchain(chain)
      chain.produce_blocks();
      BOOST_CHECK_EQUAL(chain.head_block_num(), 1);

      Make_Account(chain, producer);
      Make_Producer(chain, producer, producer_public_key);

      while (chain.head_block_num() < 3) {
         auto& producer = chain.get_producer("producer");
         BOOST_CHECK_EQUAL(chain.get_producer(producer.owner).owner, "producer");
         BOOST_CHECK_EQUAL(producer.signing_key, producer_public_key);
         BOOST_CHECK_EQUAL(producer.last_aslot, 0);
         BOOST_CHECK_EQUAL(producer.total_missed, 0);
         BOOST_CHECK_EQUAL(producer.last_confirmed_block_num, 0);
         chain.produce_blocks();
      }

      Make_Key(signing);
      Update_Producer(chain, "producer", signing_public_key);
      auto& producer = chain.get_producer("producer");
      BOOST_CHECK_EQUAL(producer.signing_key, signing_public_key);
} FC_LOG_AND_RETHROW() }

// Test producer votes on blockchain parameters in full blockchain context
BOOST_FIXTURE_TEST_CASE(producer_voting_parameters, testing_fixture)
{ try {
      Make_Blockchain(chain)
      chain.produce_blocks(21);

      vector<BlockchainConfiguration> votes{
         {1024  , 512   , 4096  , Asset(5000   ).amount, Asset(4000   ).amount, Asset(100  ).amount, 512   },
         {10000 , 100   , 4096  , Asset(3333   ).amount, Asset(27109  ).amount, Asset(10   ).amount, 100   },
         {2048  , 1500  , 1000  , Asset(5432   ).amount, Asset(2000   ).amount, Asset(50   ).amount, 1500  },
         {100   , 25    , 1024  , Asset(90000  ).amount, Asset(0      ).amount, Asset(433  ).amount, 25    },
         {1024  , 1000  , 100   , Asset(10     ).amount, Asset(50     ).amount, Asset(200  ).amount, 1000  },
         {420   , 400   , 2710  , Asset(27599  ).amount, Asset(1177   ).amount, Asset(27720).amount, 400   },
         {271   , 200   , 66629 , Asset(2666   ).amount, Asset(99991  ).amount, Asset(277  ).amount, 200   },
         {1057  , 1000  , 2770  , Asset(972    ).amount, Asset(302716 ).amount, Asset(578  ).amount, 1000  },
         {9926  , 27    , 990   , Asset(99999  ).amount, Asset(39651  ).amount, Asset(4402 ).amount, 27    },
         {1005  , 1000  , 1917  , Asset(937111 ).amount, Asset(2734   ).amount, Asset(1    ).amount, 1000  },
         {80    , 70    , 5726  , Asset(63920  ).amount, Asset(231561 ).amount, Asset(27100).amount, 70    },
         {471617, 333333, 100   , Asset(2666   ).amount, Asset(2650   ).amount, Asset(2772 ).amount, 333333},
         {2222  , 1000  , 100   , Asset(33619  ).amount, Asset(1046   ).amount, Asset(10577).amount, 1000  },
         {8     , 7     , 100   , Asset(5757267).amount, Asset(2257   ).amount, Asset(2888 ).amount, 7     },
         {2717  , 2000  , 57797 , Asset(3366   ).amount, Asset(205    ).amount, Asset(4472 ).amount, 2000  },
         {9997  , 5000  , 27700 , Asset(29199  ).amount, Asset(100    ).amount, Asset(221  ).amount, 5000  },
         {163900, 200   , 882   , Asset(100    ).amount, Asset(5720233).amount, Asset(105  ).amount, 200   },
         {728   , 80    , 27100 , Asset(28888  ).amount, Asset(6205   ).amount, Asset(5011 ).amount, 80    },
         {91937 , 44444 , 652589, Asset(87612  ).amount, Asset(123    ).amount, Asset(2044 ).amount, 44444 },
         {171   , 96    , 123456, Asset(8402   ).amount, Asset(321    ).amount, Asset(816  ).amount, 96    },
         {17177 , 6767  , 654321, Asset(9926   ).amount, Asset(9264   ).amount, Asset(8196 ).amount, 6767  },
      };
      BlockchainConfiguration medians =
         {1057, 512, 2770, Asset(9926).amount, Asset(2650).amount, Asset(816).amount, 512};
      // If this fails, the medians variable probably needs to be updated to have the medians of the votes above
      BOOST_REQUIRE_EQUAL(BlockchainConfiguration::get_median_values(votes), medians);

      for (int i = 0; i < votes.size(); ++i) {
         auto name = std::string("init") + fc::to_string(i);
         Update_Producer(chain, name, chain.get_producer(name).signing_key, votes[i]);
      }

      BOOST_CHECK_NE(chain.get_global_properties().configuration, medians);
      chain.produce_blocks(20);
      BOOST_CHECK_NE(chain.get_global_properties().configuration, medians);
      chain.produce_blocks();
      BOOST_CHECK_EQUAL(chain.get_global_properties().configuration, medians);
} FC_LOG_AND_RETHROW() }

// Test producer votes on blockchain parameters in full blockchain context, with missed blocks
BOOST_FIXTURE_TEST_CASE(producer_voting_parameters_2, testing_fixture)
{ try {
      Make_Blockchain(chain)
      chain.produce_blocks(21);

      vector<BlockchainConfiguration> votes{
         {1024  , 512   , 4096  , Asset(5000   ).amount, Asset(4000   ).amount, Asset(100  ).amount, 512   },
         {10000 , 100   , 4096  , Asset(3333   ).amount, Asset(27109  ).amount, Asset(10   ).amount, 100   },
         {2048  , 1500  , 1000  , Asset(5432   ).amount, Asset(2000   ).amount, Asset(50   ).amount, 1500  },
         {100   , 25    , 1024  , Asset(90000  ).amount, Asset(0      ).amount, Asset(433  ).amount, 25    },
         {1024  , 1000  , 100   , Asset(10     ).amount, Asset(50     ).amount, Asset(200  ).amount, 1000  },
         {420   , 400   , 2710  , Asset(27599  ).amount, Asset(1177   ).amount, Asset(27720).amount, 400   },
         {271   , 200   , 66629 , Asset(2666   ).amount, Asset(99991  ).amount, Asset(277  ).amount, 200   },
         {1057  , 1000  , 2770  , Asset(972    ).amount, Asset(302716 ).amount, Asset(578  ).amount, 1000  },
         {9926  , 27    , 990   , Asset(99999  ).amount, Asset(39651  ).amount, Asset(4402 ).amount, 27    },
         {1005  , 1000  , 1917  , Asset(937111 ).amount, Asset(2734   ).amount, Asset(1    ).amount, 1000  },
         {80    , 70    , 5726  , Asset(63920  ).amount, Asset(231561 ).amount, Asset(27100).amount, 70    },
         {471617, 333333, 100   , Asset(2666   ).amount, Asset(2650   ).amount, Asset(2772 ).amount, 333333},
         {2222  , 1000  , 100   , Asset(33619  ).amount, Asset(1046   ).amount, Asset(10577).amount, 1000  },
         {8     , 7     , 100   , Asset(5757267).amount, Asset(2257   ).amount, Asset(2888 ).amount, 7     },
         {2717  , 2000  , 57797 , Asset(3366   ).amount, Asset(205    ).amount, Asset(4472 ).amount, 2000  },
         {9997  , 5000  , 27700 , Asset(29199  ).amount, Asset(100    ).amount, Asset(221  ).amount, 5000  },
         {163900, 200   , 882   , Asset(100    ).amount, Asset(5720233).amount, Asset(105  ).amount, 200   },
         {728   , 80    , 27100 , Asset(28888  ).amount, Asset(6205   ).amount, Asset(5011 ).amount, 80    },
         {91937 , 44444 , 652589, Asset(87612  ).amount, Asset(123    ).amount, Asset(2044 ).amount, 44444 },
         {171   , 96    , 123456, Asset(8402   ).amount, Asset(321    ).amount, Asset(816  ).amount, 96    },
         {17177 , 6767  , 654321, Asset(9926   ).amount, Asset(9264   ).amount, Asset(8196 ).amount, 6767  },
      };
      BlockchainConfiguration medians =
         {1057, 512, 2770, Asset(9926).amount, Asset(2650).amount, Asset(816).amount, 512};
      // If this fails, the medians variable probably needs to be updated to have the medians of the votes above
      BOOST_REQUIRE_EQUAL(BlockchainConfiguration::get_median_values(votes), medians);

      for (int i = 0; i < votes.size(); ++i) {
         auto name = std::string("init") + fc::to_string(i);
         Update_Producer(chain, name, chain.get_producer(name).signing_key, votes[i]);
      }

      BOOST_CHECK_NE(chain.get_global_properties().configuration, medians);
      chain.produce_blocks(2);
      chain.produce_blocks(18, 5);
      BOOST_CHECK_NE(chain.get_global_properties().configuration, medians);
      chain.produce_blocks();
      BOOST_CHECK_EQUAL(chain.get_global_properties().configuration, medians);
} FC_LOG_AND_RETHROW() }

// Test that if I create a producer and vote for him, he gets in on the next round (but not before)
BOOST_FIXTURE_TEST_CASE(producer_voting_1, testing_fixture) {
   try {
      Make_Blockchain(chain)
      chain.produce_blocks();

      Make_Account(chain, joe);
      Make_Account(chain, bob);
      Stake_Asset(chain, bob, Asset(100).amount);
      Make_Producer(chain, joe);
      Approve_Producer(chain, bob, joe, true);
      // Produce blocks up to, but not including, the last block in the round
      chain.produce_blocks(config::BlocksPerRound - chain.head_block_num() - 1);

      {
         BOOST_CHECK_EQUAL(chain.get_approved_producers("bob").count("joe"), 1);
         BOOST_CHECK_EQUAL(chain.get_staked_balance("bob"), Asset(100));
         const auto& joeVotes = chain_db.get<ProducerVotesObject, byOwnerName>("joe");
         BOOST_CHECK_EQUAL(joeVotes.getVotes(), chain.get_staked_balance("bob"));
      }

      // OK, let's go to the next round
      chain.produce_blocks();

      const auto& gpo = chain.get_global_properties();
      BOOST_REQUIRE(boost::find(gpo.active_producers, "joe") != gpo.active_producers.end());

      Approve_Producer(chain, bob, joe, false);
      chain.produce_blocks();

      {
         BOOST_CHECK_EQUAL(chain.get_approved_producers("bob").count("joe"), 0);
         const auto& joeVotes = chain_db.get<ProducerVotesObject, byOwnerName>("joe");
         BOOST_CHECK_EQUAL(joeVotes.getVotes(), 0);
      }
   } FC_LOG_AND_RETHROW()
}

// Same as producer_voting_1, except we first cast the vote for the producer, _then_ get a stake
BOOST_FIXTURE_TEST_CASE(producer_voting_2, testing_fixture) {
   try {
      Make_Blockchain(chain)
      chain.produce_blocks();

      Make_Account(chain, joe);
      Make_Account(chain, bob);
      Make_Producer(chain, joe);
      Approve_Producer(chain, bob, joe, true);
      chain.produce_blocks();

      {
         BOOST_CHECK_EQUAL(chain.get_approved_producers("bob").count("joe"), 1);
         BOOST_CHECK_EQUAL(chain.get_staked_balance("bob"), Asset(0));
         const auto& joeVotes = chain_db.get<ProducerVotesObject, byOwnerName>("joe");
         BOOST_CHECK_EQUAL(joeVotes.getVotes(), chain.get_staked_balance("bob"));
      }

      Stake_Asset(chain, bob, Asset(100).amount);
      // Produce blocks up to, but not including, the last block in the round
      chain.produce_blocks(config::BlocksPerRound - chain.head_block_num() - 1);

      {
         BOOST_CHECK_EQUAL(chain.get_approved_producers("bob").count("joe"), 1);
         BOOST_CHECK_EQUAL(chain.get_staked_balance("bob"), Asset(100));
         const auto& joeVotes = chain_db.get<ProducerVotesObject, byOwnerName>("joe");
         BOOST_CHECK_EQUAL(joeVotes.getVotes(), chain.get_staked_balance("bob"));
      }

      // OK, let's go to the next round
      chain.produce_blocks();

      const auto& gpo = chain.get_global_properties();
      BOOST_REQUIRE(boost::find(gpo.active_producers, "joe") != gpo.active_producers.end());

      Approve_Producer(chain, bob, joe, false);
      chain.produce_blocks();

      {
         BOOST_CHECK_EQUAL(chain.get_approved_producers("bob").count("joe"), 0);
         const auto& joeVotes = chain_db.get<ProducerVotesObject, byOwnerName>("joe");
         BOOST_CHECK_EQUAL(joeVotes.getVotes(), 0);
      }
   } FC_LOG_AND_RETHROW()
}

// Test voting for producers by proxy
BOOST_FIXTURE_TEST_CASE(producer_proxy_voting, testing_fixture) {
   try {
      using Action = std::function<void(testing_blockchain&)>;
      Action approve = [](auto& chain) {
         Approve_Producer(chain, proxy, producer, true);
      };
      Action setproxy = [](auto& chain) {
         Set_Proxy(chain, stakeholder, proxy);
      };
      Action allowproxy = [](auto& chain) {
         Allow_Proxy(chain, proxy, true);
      };
      Action stake = [](auto& chain) {
         Stake_Asset(chain, stakeholder, Asset(100).amount);
      };

      auto run = [this](std::vector<Action> actions) {
         Make_Blockchain(chain)
         chain.produce_blocks();

         Make_Account(chain, stakeholder);
         Make_Account(chain, proxy);
         Make_Account(chain, producer);
         Make_Producer(chain, producer);

         for (auto& action : actions)
            action(chain);

         // Produce blocks up to, but not including, the last block in the round
         chain.produce_blocks(config::BlocksPerRound - chain.head_block_num() - 1);

         {
            BOOST_CHECK_EQUAL(chain.get_approved_producers("proxy").count("producer"), 1);
            BOOST_CHECK_EQUAL(chain.get_staked_balance("stakeholder"), Asset(100));
            const auto& producerVotes = chain_db.get<ProducerVotesObject, byOwnerName>("producer");
            BOOST_CHECK_EQUAL(producerVotes.getVotes(), chain.get_staked_balance("stakeholder"));
         }

         // OK, let's go to the next round
         chain.produce_blocks();

         const auto& gpo = chain.get_global_properties();
         BOOST_REQUIRE(boost::find(gpo.active_producers, "producer") != gpo.active_producers.end());

         Approve_Producer(chain, proxy, producer, false);
         chain.produce_blocks();

         {
            BOOST_CHECK_EQUAL(chain.get_approved_producers("proxy").count("producer"), 0);
            const auto& producerVotes = chain_db.get<ProducerVotesObject, byOwnerName>("producer");
            BOOST_CHECK_EQUAL(producerVotes.getVotes(), 0);
         }
      };

      // Eh, I'm not going to try all legal permutations here... just several of them
      run({approve, allowproxy, setproxy, stake});
      run({allowproxy, approve, setproxy, stake});
      run({allowproxy, setproxy, approve, stake});
      run({allowproxy, setproxy, stake, approve});
      run({stake, allowproxy, setproxy, approve});
      run({stake, approve, allowproxy, setproxy});
      run({approve, stake, allowproxy, setproxy});
      run({approve, allowproxy, stake, setproxy});
      // Make sure it throws if I try to proxy to an account before the account accepts proxying
      BOOST_CHECK_THROW(run({setproxy, allowproxy, approve, stake}), chain::message_precondition_exception);
   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_SUITE_END()
