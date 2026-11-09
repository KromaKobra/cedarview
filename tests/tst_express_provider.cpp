// The Commons Express menu, against a REAL captured response.
//
// `tests/fixtures/cedarville_oncampuspos_com_api_frontend_item_category_items_the_commons_express.json`
// is a verbatim capture of the storefront's category-items endpoint, taken
// 2026-11-09 (see core/providers/express.h). It holds the menu only — no
// orders, accounts or reviews. Beside it, the restaurant record from the same
// day, cut down to its hours.

#include "testsupport.h"

#include "core/providers/express.h"

#include <algorithm>

using namespace mycu;

namespace {

const QString FIXTURE =
    QStringLiteral("cedarville_oncampuspos_com_api_frontend_item_category_items_the_commons_express.json");

const QString RESTAURANT =
    QStringLiteral("cedarville_oncampuspos_com_api_frontend_restaurant_show_the_commons_express.json");

const QDate MONDAY(2026, 11, 9);

QList<ExpressCategory> menu()
{
    return parseExpressMenu(testing::fixtureJson(FIXTURE));
}

QJsonValue parse(const char *text)
{
    const QJsonDocument doc = QJsonDocument::fromJson(text);
    return doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
}

QStringList names(const QList<ExpressCategory> &categories)
{
    QStringList out;
    for (const auto &c : categories)
        out.append(c.name);
    return out;
}

} // namespace

class TestExpressProvider : public QObject
{
    Q_OBJECT

private slots:
    // Desserts was empty in the capture, and is left out.
    void categoriesInTheStorefrontsOrder()
    {
        QCOMPARE(names(menu()), (QStringList{"Pizza by the Slice", "Nachos", "Pizza Pies", "Salads", "Sides",
                                             "Tacos", "Quesadilla"}));
    }

    void itemsCarryNamePriceAndDescription()
    {
        const ExpressCategory slices = menu().first();
        QCOMPARE(slices.items.size(), 6);
        QCOMPARE(slices.items.first(), (ExpressItem{"Pepperoni Slice", "$1.00", "Single slice of pepperoni pizza"}));
    }

    void anItemWithNoDescriptionHasAnEmptyOne()
    {
        const auto categories = menu();
        const ExpressCategory sides = categories.at(names(categories).indexOf("Sides"));
        const auto cup = std::find_if(sides.items.cbegin(), sides.items.cend(),
                                      [](const ExpressItem &i) { return i.name == u"Salsa Cup"; });
        QVERIFY(cup != sides.items.cend());
        QCOMPARE(cup->price, QStringLiteral("$0.75"));
        QVERIFY(cup->description.isEmpty());
    }

    // `description_alt` is plain text but still entity-encoded.
    void entitiesInDescriptionsAreDecoded()
    {
        const auto categories = menu();
        const ExpressItem caesar = categories.at(names(categories).indexOf("Salads")).items.first();
        QCOMPARE(caesar.name, QStringLiteral("Caesar Salad"));
        QVERIFY2(caesar.description.contains(u"Parmesan, & Caesar"), qPrintable(caesar.description));
        QVERIFY(!caesar.description.contains(u"&amp;"));
    }

    void aDiscountShowsTheReducedPrice()
    {
        const auto categories = parseExpressMenu(parse(R"({"data": [{"name": "Sides", "items": [
            {"name": "Breadsticks", "flat_price": "3.00", "currency_price": "$3.00",
             "flat_discounted_price": "2.25", "currency_discounted_price": "$2.25"},
            {"name": "Salsa Cup", "flat_price": "0.75", "currency_price": "$0.75",
             "flat_discounted_price": "0.00", "currency_discounted_price": "$0.00"}]}]})"));
        QCOMPARE(categories.first().items.at(0).price, QStringLiteral("$2.25"));
        QCOMPARE(categories.first().items.at(1).price, QStringLiteral("$0.75"));
    }

    void malformedEntriesAreSkipped()
    {
        const auto categories = parseExpressMenu(parse(R"({"data": [
            "not a category",
            {"name": "", "items": [{"name": "Orphan", "currency_price": "$1.00"}]},
            {"name": "Tacos", "items": [7, {"name": ""}, {"name": "Signature Taco", "currency_price": "$2.75"}]}]})"));
        QCOMPARE(names(categories), QStringList{"Tacos"});
        QCOMPARE(categories.first().items.size(), 1);
    }

    void theWrongShapeIsAParseError()
    {
        CV_VERIFY_THROWS_MATCHING(parseExpressMenu(parse(R"({"items": []})")), ParseError, "Commons Express");
        CV_VERIFY_THROWS_MATCHING(parseExpressMenu(parse(R"([1, 2])")), ParseError, "Commons Express");
    }

    void aMenuWithNothingOnItIsAParseError()
    {
        CV_VERIFY_THROWS_MATCHING(parseExpressMenu(parse(R"({"data": [{"name": "Desserts", "items": []}]})")),
                                  ParseError, "no items");
    }

    // ---- Hours ---------------------------------------------------------------

    // Sunday through Friday 8–11 PM, Wednesday 1–4 PM as well, Saturday closed.
    void hoursFromTheRestaurantRecord()
    {
        const ExpressHours h = parseExpressHours(testing::fixtureJson(RESTAURANT));
        const hours::Window evening{20 * 60, 23 * 60};
        QCOMPARE(h.on(MONDAY), hours::DayWindows{evening});
        QCOMPARE(h.on(MONDAY.addDays(2)), (hours::DayWindows{{13 * 60, 16 * 60}, evening})); // Wednesday
        QVERIFY(h.on(MONDAY.addDays(5)).isEmpty());                                          // Saturday
        QCOMPARE(h.on(MONDAY.addDays(6)), hours::DayWindows{evening});                       // Sunday
    }

    void aSlotPastMidnightClosesTheNextDay()
    {
        const ExpressHours h = parseExpressHours(parse(R"({"time_slots": [
            {"opening_time": "10:00 PM", "closing_time": "01:00 AM", "day": 5}]})"));
        QCOMPARE(h.on(MONDAY.addDays(4)), (hours::DayWindows{{22 * 60, 25 * 60}})); // Friday
    }

    void unreadableSlotsAreSkipped()
    {
        const ExpressHours h = parseExpressHours(parse(R"({"time_slots": [
            {"opening_time": "noon", "closing_time": "11:00 PM", "day": 1},
            {"opening_time": "08:00 PM", "closing_time": "11:00 PM", "day": 9},
            {"opening_time": "08:00 PM", "closing_time": "11:00 PM"}]})"));
        QVERIFY(h.isEmpty());
        QVERIFY(parseExpressHours(QJsonValue::Null).isEmpty());
    }

    // ---- Fetching ------------------------------------------------------------

    // The paths' fixture slugs are the captured files' names, so --demo and
    // sample-data preview serve the captures.
    void fetchesThroughTheTransport()
    {
        QCOMPARE(FixtureTransport::slug(EXPRESS_PATH) + QStringLiteral(".json"), FIXTURE);
        QCOMPARE(FixtureTransport::slug(EXPRESS_RESTAURANT_PATH) + QStringLiteral(".json"), RESTAURANT);
        const ExpressMenu fetched = ExpressProvider(std::make_shared<FixtureTransport>(testing::fixturesDir())).fetch();
        QCOMPARE(fetched.categories, menu());
        QCOMPARE(fetched.hours, parseExpressHours(testing::fixtureJson(RESTAURANT)));
    }

    // What is saved holds the hours and nothing else of the record.
    void thePayloadKeepsOnlyTheHours()
    {
        const QJsonObject payload =
            ExpressProvider(std::make_shared<FixtureTransport>(testing::fixturesDir())).fetchPayload().toObject();
        QCOMPARE(payload.value("restaurant").toObject().keys(), QStringList{"time_slots"});
        QCOMPARE(parseExpress(payload).hours, parseExpressHours(testing::fixtureJson(RESTAURANT)));
    }

    // No hours is not a failed fetch: the menu still loads.
    void theMenuLoadsWithoutTheHours()
    {
        struct MenuOnly : Transport
        {
            FixtureTransport fixtures{testing::fixturesDir()};
            Response get(const QString &path) override
            {
                if (path == EXPRESS_RESTAURANT_PATH)
                    throw TransportError(QStringLiteral("HTTP 500"), 500);
                return fixtures.get(path);
            }
        };
        const QJsonValue payload = ExpressProvider(std::make_shared<MenuOnly>()).fetchPayload();
        QVERIFY(payload.toObject().value("restaurant").isNull());
        const ExpressMenu fetched = parseExpress(payload);
        QCOMPARE(fetched.categories.size(), 7);
        QVERIFY(fetched.hours.isEmpty());
    }

    void theOrderButtonGoesToTheStorefront()
    {
        QCOMPARE(EXPRESS_ORDER_URL, QStringLiteral("https://cedarville.oncampuspos.com/restaurant/the-commons-express"));
    }
};

CEDARVIEW_TEST_MAIN(TestExpressProvider, QCoreApplication)
#include "tst_express_provider.moc"
