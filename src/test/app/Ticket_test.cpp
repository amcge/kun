

#include <test/jtx.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>

namespace ripple {

class Ticket_test : public beast::unit_test::suite
{
    static auto constexpr idOne =
      "00000000000000000000000000000000"
      "00000000000000000000000000000001";

    auto
    checkTicketMeta(
        test::jtx::Env& env,
        bool other_target = false,
        bool expiration = false)
    {
        using namespace std::string_literals;
        auto const& tx = env.tx ()->getJson (JsonOptions::none);
        bool is_cancel = tx[jss::TransactionType] == jss::TicketCancel;

        auto const& jvm = env.meta ()->getJson (JsonOptions::none);
        std::array<Json::Value, 4> retval;

        std::vector<
            std::tuple<std::size_t, std::string, Json::StaticString>
        > expected_nodes;

        if (is_cancel && other_target)
        {
            expected_nodes = {
                {0, sfModifiedNode.fieldName, jss::AccountRoot},
                {expiration ? 2: 1, sfModifiedNode.fieldName, jss::AccountRoot},
                {expiration ? 1: 2, sfDeletedNode.fieldName, jss::Ticket},
                {3, sfDeletedNode.fieldName, jss::DirectoryNode}
            };
        }
        else
        {
            expected_nodes = {
                {0, sfModifiedNode.fieldName, jss::AccountRoot},
                {1, is_cancel ? sfDeletedNode.fieldName :
                    sfCreatedNode.fieldName, jss::Ticket},
                {2, is_cancel ? sfDeletedNode.fieldName :
                    sfCreatedNode.fieldName, jss::DirectoryNode}
            };
        }

        BEAST_EXPECT(jvm.isMember (sfAffectedNodes.fieldName));
        BEAST_EXPECT(jvm[sfAffectedNodes.fieldName].isArray());
        BEAST_EXPECT(
            jvm[sfAffectedNodes.fieldName].size() == expected_nodes.size());

        for (auto const& it : expected_nodes)
        {
            auto const& idx = std::get<0>(it);
            auto const& field = std::get<1>(it);
            auto const& type = std::get<2>(it);
            BEAST_EXPECT(jvm[sfAffectedNodes.fieldName][idx].isMember(field));
            retval[idx] = jvm[sfAffectedNodes.fieldName][idx][field];
            BEAST_EXPECT(retval[idx][sfLedgerEntryType.fieldName] == type);
        }

        return retval;
    }

    void testTicketNotEnabled ()
    {
        testcase ("Feature Not Enabled");

        using namespace test::jtx;
        Env env {*this, FeatureBitset{}};

        env (ticket::create (env.master), ter(temDISABLED));
        env (ticket::cancel (env.master, idOne), ter (temDISABLED));
    }

    void testTicketCancelNonexistent ()
    {
        testcase ("Cancel Nonexistent");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};
        env (ticket::cancel (env.master, idOne), ter (tecNO_ENTRY));
    }

    void testTicketCreatePreflightFail ()
    {
        testcase ("Create/Cancel Ticket with Bad Fee, Fail Preflight");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};

        env (ticket::create (env.master), fee (XRP (-1)), ter (temBAD_FEE));
        env (ticket::cancel (env.master, idOne), fee (XRP (-1)), ter (temBAD_FEE));
    }

    void testTicketCreateNonexistent ()
    {
        testcase ("Create Tickets with Nonexistent Accounts");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};
        Account alice {"alice"};
        env.memoize (alice);

        env (ticket::create (env.master, alice), ter(tecNO_TARGET));

        env (ticket::create (alice, env.master),
            json (jss::Sequence, 1),
            ter (terNO_ACCOUNT));
    }

    void testTicketToSelf ()
    {
        testcase ("Create Tickets with Same Account and Target");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};

        env (ticket::create (env.master, env.master));
        auto cr = checkTicketMeta (env);
        auto const& jticket = cr[1];

        BEAST_EXPECT(jticket[sfLedgerIndex.fieldName] ==
            "7F58A0AE17775BA3404D55D406DD1C2E91EADD7AF3F03A26877BCE764CCB75E3");
        BEAST_EXPECT(jticket[sfNewFields.fieldName][jss::Account] ==
            env.master.human());
        BEAST_EXPECT(jticket[sfNewFields.fieldName][jss::Sequence] == 1);
        BEAST_EXPECT(! jticket[sfNewFields.fieldName].
            isMember(sfTarget.fieldName));
    }

    void testTicketCancelByCreator ()
    {
        testcase ("Create Ticket and Then Cancel by Creator");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};

        env (ticket::create (env.master));
        auto cr = checkTicketMeta (env);
        auto const& jacct = cr[0];
        auto const& jticket = cr[1];
        BEAST_EXPECT(
            jacct[sfPreviousFields.fieldName][sfOwnerCount.fieldName] == 0);
        BEAST_EXPECT(
            jacct[sfFinalFields.fieldName][sfOwnerCount.fieldName] == 1);
        BEAST_EXPECT(jticket[sfNewFields.fieldName][jss::Sequence] ==
            jacct[sfPreviousFields.fieldName][jss::Sequence]);
        BEAST_EXPECT(jticket[sfLedgerIndex.fieldName] ==
            "7F58A0AE17775BA3404D55D406DD1C2E91EADD7AF3F03A26877BCE764CCB75E3");
        BEAST_EXPECT(jticket[sfNewFields.fieldName][jss::Account] ==
            env.master.human());

        env (ticket::cancel(env.master, jticket[sfLedgerIndex.fieldName].asString()));
        auto crd = checkTicketMeta (env);
        auto const& jacctd = crd[0];
        BEAST_EXPECT(jacctd[sfFinalFields.fieldName][jss::Sequence] == 3);
        BEAST_EXPECT(
            jacctd[sfFinalFields.fieldName][sfOwnerCount.fieldName] == 0);
    }

    void testTicketInsufficientReserve ()
    {
        testcase ("Create Ticket Insufficient Reserve");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};
        Account alice {"alice"};

        env.fund (env.current ()->fees ().accountReserve (0), alice);
        env.close ();

        env (ticket::create (alice), ter (tecINSUFFICIENT_RESERVE));
    }

    void testTicketCancelByTarget ()
    {
        testcase ("Create Ticket and Then Cancel by Target");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};
        Account alice {"alice"};

        env.fund (XRP (10000), alice);
        env.close ();

        env (ticket::create (env.master, alice));
        auto cr = checkTicketMeta (env, true);
        auto const& jacct = cr[0];
        auto const& jticket = cr[1];
        BEAST_EXPECT(
            jacct[sfFinalFields.fieldName][sfOwnerCount.fieldName] == 1);
        BEAST_EXPECT(jticket[sfLedgerEntryType.fieldName] == jss::Ticket);
        BEAST_EXPECT(jticket[sfLedgerIndex.fieldName] ==
            "C231BA31A0E13A4D524A75F990CE0D6890B800FF1AE75E51A2D33559547AC1A2");
        BEAST_EXPECT(jticket[sfNewFields.fieldName][jss::Account] ==
            env.master.human());
        BEAST_EXPECT(jticket[sfNewFields.fieldName][sfTarget.fieldName] ==
            alice.human());
        BEAST_EXPECT(jticket[sfNewFields.fieldName][jss::Sequence] == 2);

        env (ticket::cancel(alice, jticket[sfLedgerIndex.fieldName].asString()));
        auto crd = checkTicketMeta (env, true);
        auto const& jacctd = crd[0];
        auto const& jdir = crd[2];
        BEAST_EXPECT(
            jacctd[sfFinalFields.fieldName][sfOwnerCount.fieldName] == 0);
        BEAST_EXPECT(jdir[sfLedgerIndex.fieldName] ==
            jticket[sfLedgerIndex.fieldName]);
        BEAST_EXPECT(jdir[sfFinalFields.fieldName][jss::Account] ==
            env.master.human());
        BEAST_EXPECT(jdir[sfFinalFields.fieldName][sfTarget.fieldName] ==
            alice.human());
        BEAST_EXPECT(jdir[sfFinalFields.fieldName][jss::Flags] == 0);
        BEAST_EXPECT(jdir[sfFinalFields.fieldName][sfOwnerNode.fieldName] ==
            "0000000000000000");
        BEAST_EXPECT(jdir[sfFinalFields.fieldName][jss::Sequence] == 2);
    }

    void testTicketWithExpiration ()
    {
        testcase ("Create Ticket with Future Expiration");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};

        using namespace std::chrono_literals;
        uint32_t expire =
            (env.timeKeeper ().closeTime () + 60s)
            .time_since_epoch ().count ();
        env (ticket::create (env.master, expire));
        auto cr = checkTicketMeta (env);
        auto const& jacct = cr[0];
        auto const& jticket = cr[1];
        BEAST_EXPECT(
            jacct[sfPreviousFields.fieldName][sfOwnerCount.fieldName] == 0);
        BEAST_EXPECT(
            jacct[sfFinalFields.fieldName][sfOwnerCount.fieldName] == 1);
        BEAST_EXPECT(jticket[sfNewFields.fieldName][jss::Sequence] ==
            jacct[sfPreviousFields.fieldName][jss::Sequence]);
        BEAST_EXPECT(
            jticket[sfNewFields.fieldName][sfExpiration.fieldName] == expire);
    }

    void testTicketZeroExpiration ()
    {
        testcase ("Create Ticket with Zero Expiration");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};

        env (ticket::create (env.master, 0u), ter (temBAD_EXPIRATION));
    }

    void testTicketWithPastExpiration ()
    {
        testcase ("Create Ticket with Past Expiration");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};

        env.timeKeeper ().adjustCloseTime (days {2});
        env.close ();

        uint32_t expire = 60;
        env (ticket::create (env.master, expire));
        auto const& jvm = env.meta ()->getJson (JsonOptions::none);
        BEAST_EXPECT(jvm.isMember(sfAffectedNodes.fieldName));
        BEAST_EXPECT(jvm[sfAffectedNodes.fieldName].isArray());
        BEAST_EXPECT(jvm[sfAffectedNodes.fieldName].size() == 1);
        BEAST_EXPECT(jvm[sfAffectedNodes.fieldName][0u].
            isMember(sfModifiedNode.fieldName));
        auto const& jacct =
            jvm[sfAffectedNodes.fieldName][0u][sfModifiedNode.fieldName];
        BEAST_EXPECT(
            jacct[sfLedgerEntryType.fieldName] == jss::AccountRoot);
        BEAST_EXPECT(jacct[sfFinalFields.fieldName][jss::Account] ==
            env.master.human());
    }

    void testTicketAllowExpiration ()
    {
        testcase ("Create Ticket and Allow to Expire");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};

        uint32_t expire =
            (env.timeKeeper ().closeTime () + std::chrono::hours {3})
            .time_since_epoch().count();
        env (ticket::create (env.master, expire));
        auto cr = checkTicketMeta (env);
        auto const& jacct = cr[0];
        auto const& jticket = cr[1];
        BEAST_EXPECT(
            jacct[sfPreviousFields.fieldName][sfOwnerCount.fieldName] == 0);
        BEAST_EXPECT(
            jacct[sfFinalFields.fieldName][sfOwnerCount.fieldName] == 1);
        BEAST_EXPECT(
            jticket[sfNewFields.fieldName][sfExpiration.fieldName] == expire);
        BEAST_EXPECT(jticket[sfLedgerIndex.fieldName] ==
            "7F58A0AE17775BA3404D55D406DD1C2E91EADD7AF3F03A26877BCE764CCB75E3");

        Account alice {"alice"};
        env.fund (XRP (10000), alice);
        env.close ();

        auto jv = ticket::cancel(alice, jticket[sfLedgerIndex.fieldName].asString());
        env (jv, ter (tecNO_PERMISSION));

        env.timeKeeper ().adjustCloseTime (days {3});
        env.close ();

        env (jv);
        auto crd = checkTicketMeta (env, true, true);
        auto const& jticketd = crd[1];
        BEAST_EXPECT(
            jticketd[sfFinalFields.fieldName][sfExpiration.fieldName] == expire);
    }

public:
    void run () override
    {
        testTicketNotEnabled ();
        testTicketCancelNonexistent ();
        testTicketCreatePreflightFail ();
        testTicketCreateNonexistent ();
        testTicketToSelf ();
        testTicketCancelByCreator ();
        testTicketInsufficientReserve ();
        testTicketCancelByTarget ();
        testTicketWithExpiration ();
        testTicketZeroExpiration ();
        testTicketWithPastExpiration ();
        testTicketAllowExpiration ();
    }
};

BEAST_DEFINE_TESTSUITE (Ticket, tx, ripple);

}  






