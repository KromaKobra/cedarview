// The on-device payload cache.
//
// What is cached is what the services sent, not the parsed models, so the
// test that matters most is the round trip through the real parsers: a saved
// chapel payload, read back, must give the same figures a fresh fetch did.

#include "testsupport.h"

#include "core/cache.h"
#include "core/providers/chapel.h"
#include "core/providers/meals.h"

using namespace mycu;

class TestCache : public QObject
{
    Q_OBJECT

private slots:
    void nothingSavedIsNothingLoaded()
    {
        QTemporaryDir dir;
        QVERIFY(!PayloadCache(dir.path()).load(cachekey::MENUS));
    }

    void aPayloadRoundTripsWithItsTimestamp()
    {
        QTemporaryDir dir;
        PayloadCache cache(dir.path());
        const QDateTime at(QDate(2026, 9, 17), QTime(9, 41, 12));
        cache.save(cachekey::SCHEDULE, QJsonObject{{"Items", QJsonArray{1, 2}}}, at);

        const auto saved = cache.load(cachekey::SCHEDULE);
        QVERIFY(saved);
        QCOMPARE(saved->payload.toObject().value("Items").toArray().size(), 2);
        QCOMPARE(saved->savedAt, at);
        QCOMPARE(saved->ageSecs(at.addSecs(90)), 90);
    }

    // A phone whose clock moved back must not see a negative age.
    void aSaveFromTheFutureIsJustSaved()
    {
        const CachedPayload saved{QJsonValue(1), QDateTime(QDate(2026, 9, 17), QTime(10, 0))};
        QCOMPARE(saved.ageSecs(QDateTime(QDate(2026, 9, 17), QTime(9, 0))), 0);
    }

    // The point of keeping payloads: the real parsers read them back.
    void aCachedChapelPayloadParsesLikeAFreshOne()
    {
        QTemporaryDir dir;
        PayloadCache cache(dir.path());
        const QJsonObject payload =
            ChapelProvider(std::make_shared<FixtureTransport>(testing::fixturesDir())).fetchPayload();
        cache.save(cachekey::CHAPEL, payload);

        const ChapelSummary restored = buildSummary(cache.load(cachekey::CHAPEL)->payload.toObject());
        QCOMPARE(restored.remaining, 16);
        QCOMPARE(restored.entries.size(), 3);
    }

    void aCachedMealPlanParsesLikeAFreshOne()
    {
        QTemporaryDir dir;
        PayloadCache cache(dir.path());
        cache.save(cachekey::MEALS,
                   MealsProvider(std::make_shared<FixtureTransport>(testing::fixturesDir())).fetchPayload());
        const MealPlan plan = parseBalance(cache.load(cachekey::MEALS)->payload.toObject());
        QCOMPARE(plan.mealsRemaining, 16);
        QCOMPARE(plan.diningDollars, 102.34);
    }

    void filesArePrivate()
    {
        QTemporaryDir dir;
        PayloadCache cache(dir.path());
        cache.save(cachekey::MEALS, QJsonObject{{"x", 1}});
        QCOMPARE(QFile::permissions(cache.pathFor(cachekey::MEALS)),
                 QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
        const QFile::Permissions folder = QFile::permissions(cache.dir());
        QVERIFY(!(folder & (QFile::ReadOther | QFile::ReadGroup)));
    }

    // Sign-out deletes someone's records and keeps the public feeds.
    void clearingPersonalKeepsThePublicFeeds()
    {
        QTemporaryDir dir;
        PayloadCache cache(dir.path());
        for (const QString &key : {cachekey::CHAPEL, cachekey::MEALS, cachekey::MENUS, cachekey::SCHEDULE})
            cache.save(key, QJsonObject{{"k", key}});

        cache.clearPersonal();

        QVERIFY(!cache.load(cachekey::CHAPEL));
        QVERIFY(!cache.load(cachekey::MEALS));
        QVERIFY(cache.load(cachekey::MENUS));
        QVERIFY(cache.load(cachekey::SCHEDULE));

        cache.clearAll();
        QVERIFY(!cache.load(cachekey::MENUS));
    }

    void aCorruptFileIsNothingNotACrash()
    {
        QTemporaryDir dir;
        PayloadCache cache(dir.path());
        cache.save(cachekey::MENUS, QJsonObject{{"x", 1}});
        QFile file(cache.pathFor(cachekey::MENUS));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("{ not json");
        file.close();
        QVERIFY(!cache.load(cachekey::MENUS));
    }

    void anotherSchemaIsIgnored()
    {
        QTemporaryDir dir;
        PayloadCache cache(dir.path());
        QDir().mkpath(cache.dir());
        QFile file(cache.pathFor(cachekey::MENUS));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"schema": 99, "saved_at": 1789650000, "payload": {"x": 1}})");
        file.close();
        QVERIFY(!cache.load(cachekey::MENUS));
    }

    void theCacheLivesBesideTheSession()
    {
        QTemporaryDir dir;
        QCOMPARE(QFileInfo(PayloadCache(dir.path()).dir()).absolutePath(), QFileInfo(dir.path()).absoluteFilePath());
    }
};

CEDARVIEW_TEST_MAIN(TestCache, QCoreApplication)
#include "tst_cache.moc"
