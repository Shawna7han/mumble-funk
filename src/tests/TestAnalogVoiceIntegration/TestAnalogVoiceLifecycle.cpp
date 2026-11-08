// Copyright 2026 Mumble-Funk contributors. BSD-3-Clause; see repository LICENSE.
#include "AnalogVoiceLifecycle.h"
#include <QtTest/QtTest>
using Service = funkvoice::AnalogVoiceService;
using Plan = SquelchPlan;
namespace {
const Service::Identity source{1, 3, 10, 0, 1}, receiver{2, 4, 11, 0, 2};
const Plan::Identity from{1, 10, 0}, to{2, 11, 0};
Service::Recipients audience() {
    Service::Recipients result; result.identities[0] = receiver; result.count = 1; return result;
}
Service::Batch batch() {
    Service::Batch result;
    result.handle = {77, 0};
    result.completed.sender = source;
    result.completed.generation = 77;
    result.completed.channelTicket = 100;
    result.completed.sourceFrame = 25;
    result.completed.samples = 960;
    result.completed.accepted = Service::Time(std::chrono::microseconds(1000000));
    result.audio.count = 1;
    result.sources[0][0] = result.completed;
    result.sourceCounts[0] = 1;
    return result;
}
}
class TestAnalogVoiceLifecycle : public QObject {
    Q_OBJECT
private slots:
    void sourceSpansAndDeferredEnd();
    void emptyAndTechnical_data();
    void emptyAndTechnical();
    void deadline_data();
    void deadline();
    void failure_data();
    void failure();
    void recipientsAndHandoff();
    void invalidationAndReceiveOnly();
};
void TestAnalogVoiceLifecycle::sourceSpansAndDeferredEnd() {
    std::int64_t now = 1000000; Plan plan(true, [&]{ return now; }); AnalogVoiceLifecycle life;
    auto b = batch();
    b.completed.channelTicket = 101; // Completed job is not the contributing ticket.
    b.completed.sourceFrame = 26;
    b.completed.samples = 480;
    b.completed.end = true;
    b.sourceCounts[0] = 3;
    b.sources[0][1] = b.sources[0][0]; // Two spans of the first 10-ms original.
    b.sources[0][2] = b.completed;
    QVERIFY(life.complete(plan, b).empty()); // Job completion alone cannot Open.
    QCOMPARE(life.observations(), std::uint64_t(0));
    const auto open = life.delivered(plan, b, 0, audience());
    QCOMPARE(open.size(), std::size_t(1));
    QCOMPARE(open[0].kind, Plan::Kind::Open);
    QVERIFY(plan.consume(open[0], from, to, true));
    QCOMPARE(life.observations(), std::uint64_t(2));
    QVERIFY(life.delivered(plan, b, 0, audience()).empty());
    QCOMPARE(life.observations(), std::uint64_t(2));
    // Lookahead/padding/PLC has no new source; final packet may have none at all.
    b.sourceCounts[0] = 0;
    QVERIFY(life.delivered(plan, b, 0, audience()).empty());
    b.terminal = true;
    auto close = life.complete(plan, b);
    QCOMPARE(close.size(), std::size_t(1));
    QCOMPARE(close[0].createdUs, std::int64_t(1000000));
    QVERIFY(plan.consume(close[0], from, to, true));
    QVERIFY(life.complete(plan, b).empty());
    QCOMPARE(life.observations(), std::uint64_t(2));
}
void TestAnalogVoiceLifecycle::emptyAndTechnical_data() {
    QTest::addColumn<bool>("technical");
    QTest::newRow("empty-original") << false;
    QTest::newRow("timeout-control") << true;
}
void TestAnalogVoiceLifecycle::emptyAndTechnical() {
    QFETCH(bool, technical);
    Plan plan(true); AnalogVoiceLifecycle life;
    auto b = batch();
    const auto open = life.delivered(plan,b,0,audience());
    QCOMPARE(open.size(),std::size_t(1));
    b.completed.channelTicket++;
    b.completed.samples = 0; b.completed.end = true; b.completed.technical = technical;
    b.audio.count = 0; b.terminal = true;
    const auto close = life.complete(plan,b); // Empty terminal batches matter.
    QCOMPARE(close.size(),std::size_t(1));
    QCOMPARE(life.observations(),std::uint64_t(technical ? 1 : 2));
    QVERIFY(life.complete(plan,b).empty());
    Plan noStart(true); AnalogVoiceLifecycle noLife;
    QVERIFY(noLife.complete(noStart,b).empty());
}
void TestAnalogVoiceLifecycle::deadline_data() {
    QTest::addColumn<bool>("technical"); QTest::addColumn<int>("age");
    for (bool technical : {false,true}) for (int age : {299999,300000,300001})
        QTest::newRow(qPrintable(QString("%1-%2").arg(technical).arg(age))) << technical << age;
}
void TestAnalogVoiceLifecycle::deadline() {
    QFETCH(bool,technical); QFETCH(int,age);
    std::int64_t now=1000000; Plan plan(true,[&]{return now;}); AnalogVoiceLifecycle life;
    auto b=batch();
    auto open=life.delivered(plan,b,0,audience()); QVERIFY(plan.consume(open[0],from,to,true));
    b.terminal=true; b.completed.end=true; b.completed.technical=technical;
    now+=age;
    auto close=life.complete(plan,b); QCOMPARE(close.size(),std::size_t(1));
    QCOMPARE(close[0].createdUs,std::int64_t(1000000));
    QCOMPARE(plan.consume(close[0],from,to,true),age<=300000);
}
void TestAnalogVoiceLifecycle::failure_data() {
    QTest::addColumn<int>("failure"); QTest::addColumn<bool>("started");
    for(int failure=1;failure<=4;++failure) for(bool started:{false,true})
        QTest::newRow(qPrintable(QString("%1-%2").arg(failure).arg(started))) << failure << started;
}
void TestAnalogVoiceLifecycle::failure() {
    QFETCH(int,failure); QFETCH(bool,started);
    Plan plan(true); AnalogVoiceLifecycle life; auto b=batch();
    if(started) QCOMPARE(life.delivered(plan,b,0,audience()).size(),std::size_t(1));
    b.failure=static_cast<Service::Failure>(failure); b.terminal=true;
    QVERIFY(life.delivered(plan,b,0,audience()).empty());
    QVERIFY(life.complete(plan,b).empty());
    QCOMPARE(plan.size(),std::size_t(0));
}
void TestAnalogVoiceLifecycle::recipientsAndHandoff() {
    std::int64_t now=1000000; Plan plan(true,[&]{return now;}); AnalogVoiceLifecycle life; auto b=batch();
    QVERIFY(life.delivered(plan,b,0,{}).empty()); // Dropped TLS or failed UDP send.
    QCOMPARE(life.observations(),std::uint64_t(0));
    auto open=life.delivered(plan,b,0,audience()); QVERIFY(plan.consume(open[0],from,to,true));
    QVERIFY(life.delivered(plan,b,0,{}).empty()); // Irrevocable delivery intersection.
    b.terminal=true; b.completed.end=true;
    QVERIFY(life.complete(plan,b).empty());
    b.handle.generation++; b.sources[0][0].generation++; b.completed.generation++;
    b.terminal=false;
    open=life.delivered(plan,b,0,audience()); QCOMPARE(open.size(),std::size_t(1));
    QVERIFY(plan.consume(open[0],from,to,true));
    b.terminal=true;
    auto oldClose=life.complete(plan,b); QCOMPARE(oldClose.size(),std::size_t(1));
    b.handle.generation++; b.completed.generation++; b.sources[0][0].generation++;
    b.completed.sender.connection=3; b.sources[0][0].sender=b.completed.sender;
    b.terminal=false;
    QCOMPARE(life.delivered(plan,b,0,audience()).size(),std::size_t(1));
    QVERIFY(!plan.consume(oldClose[0],from,to,true)); // No old Close for B.
}
void TestAnalogVoiceLifecycle::invalidationAndReceiveOnly() {
    Plan plan(true); AnalogVoiceLifecycle life; auto b=batch();
    auto open=life.delivered(plan,b,0,audience());
    plan.invalidateRecipient(source.connection); // Receive-only loss must preserve own send.
    QVERIFY(plan.current(Plan::Cue{Plan::Kind::Noise,from,to,open[0].transmission,0}));
    plan.invalidate(receiver.connection); // Out-and-back/session reuse cannot restore audience.
    QVERIFY(life.delivered(plan,b,0,audience()).empty());
    b.terminal=true; b.completed.end=true;
    QVERIFY(life.complete(plan,b).empty());
}
QTEST_MAIN(TestAnalogVoiceLifecycle)
#include "TestAnalogVoiceLifecycle.moc"
