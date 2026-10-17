// AI 匹配 Emoji（需求第 7 节）：回复解析、取帧拼图、请求与重试（本机假接口）
#include "app/AiMatcher.h"
#include "core/AiEmoji.h"
#include "media/AiFrameGrabber.h"
#include "media/FFmpegTools.h"
#include "media/MediaProbe.h"
#include "media/ProcessRunner.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace tsm;

// 极简 HTTP 服务：按顺序返回预设的响应，记录收到的请求
class FakeApi : public QObject {
    Q_OBJECT

public:
    struct Response {
        int status;
        QByteArray body;
    };
    struct Request {
        QByteArray head;
        QByteArray body;
    };

    QList<Response> responses;
    QList<Request> requests;

    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1/v1").arg(m_server.serverPort()); }

    static QByteArray chat(const QString& content)
    {
        const QJsonObject message{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), content}};
        return QJsonDocument(QJsonObject{{QStringLiteral("choices"), QJsonArray{QJsonObject{{QStringLiteral("message"), message}}}}})
            .toJson(QJsonDocument::Compact);
    }

    FakeApi()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                auto* buffer = new QByteArray;
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                    *buffer += socket->readAll();
                    const qsizetype headEnd = buffer->indexOf("\r\n\r\n");
                    if (headEnd < 0)
                        return;
                    const QByteArray head = buffer->left(headEnd);
                    qsizetype length = 0;
                    for (const QByteArray& line : head.split('\n')) {
                        if (line.toLower().startsWith("content-length:"))
                            length = line.mid(15).trimmed().toLongLong();
                    }
                    if (buffer->size() < headEnd + 4 + length)
                        return;
                    requests << Request{head, buffer->mid(headEnd + 4, length)};
                    buffer->clear();
                    const Response r = responses.isEmpty() ? Response{500, "{}"} : responses.takeFirst();
                    QByteArray reply = "HTTP/1.1 " + QByteArray::number(r.status) + " X\r\nContent-Type: application/json\r\n"
                                       "Connection: close\r\nContent-Length: " + QByteArray::number(r.body.size()) + "\r\n\r\n" + r.body;
                    socket->write(reply);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, [socket, buffer] {
                    delete buffer;
                    socket->deleteLater();
                });
            }
        });
    }

private:
    QTcpServer m_server;
};

class TestAi : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_video;
    QString m_image;

    bool generate(const QString& out, const QStringList& args)
    {
        QStringList full{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y")};
        full << args << out;
        return ProcessRunner::run(FFmpegTools::ffmpegPath(), full, 120000).ok();
    }

    static StickerTask taskFor(const QString& file)
    {
        const ProbeResult probe = MediaProbe::probe(file);
        return StickerTask::fromMedia(file, probe.info);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        if (FFmpegTools::ffmpegPath().isEmpty() || FFmpegTools::ffprobePath().isEmpty())
            QSKIP("ffmpeg / ffprobe not found");
        m_video = m_dir.filePath(QStringLiteral("表情.mp4"));
        QVERIFY(generate(m_video, {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                   QStringLiteral("testsrc2=size=480x360:rate=25:duration=3"), QStringLiteral("-c:v"),
                                   QStringLiteral("mpeg4")}));
        m_image = m_dir.filePath(QStringLiteral("big.png"));
        QVERIFY(generate(m_image, {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                   QStringLiteral("testsrc2=size=800x600"), QStringLiteral("-frames:v"), QStringLiteral("1")}));
    }

    void extractEmoji()
    {
        QCOMPARE(AiEmoji::extractEmoji(QStringLiteral("😂🤣")), QStringLiteral("😂🤣"));
        QCOMPARE(AiEmoji::extractEmoji(QStringLiteral("好的：😂 ✨，就这个")), QStringLiteral("😂"));
        QCOMPARE(AiEmoji::extractEmoji(QStringLiteral("✨🌟")), QStringLiteral("✨🌟")); // 只有装饰符号时保留
        QCOMPARE(AiEmoji::extractEmoji(QStringLiteral("👍🏻 ❤️")), QStringLiteral("👍🏻❤️"));
        QCOMPARE(AiEmoji::extractEmoji(QStringLiteral("👍🏻 👨‍👩‍👧")), QStringLiteral("👍🏻👨‍👩‍👧")); // 组合 Emoji 整个保留（正好 12 个字符）
        QCOMPARE(AiEmoji::extractEmoji(QStringLiteral("👍🏻 ❤️ 👨‍👩‍👧")), QStringLiteral("👍🏻❤️")); // 放不下的不截半个
        QCOMPARE(AiEmoji::extractEmoji(QStringLiteral("没有合适的")), QString());
        QCOMPARE(AiEmoji::extractEmoji(QStringLiteral("123 abc")), QString());
        const QString many = AiEmoji::extractEmoji(QStringLiteral("😂😭😡🥺😴🤔👍😂😭😡"));
        QVERIFY(many.size() <= AiEmoji::kMaxEmojiChars);
        QCOMPARE(many, QStringLiteral("😂😭😡🥺😴🤔"));
    }

    void sampleTimes()
    {
        const auto same = [](const QList<double>& a, const QList<double>& b) {
            if (a.size() != b.size())
                return false;
            for (qsizetype i = 0; i < a.size(); ++i)
                if (std::abs(a.at(i) - b.at(i)) > 1e-9)
                    return false;
            return true;
        };
        QVERIFY(same(AiEmoji::sampleTimes(3.0), {0.6, 1.5, 2.4}));
        QVERIFY(same(AiEmoji::sampleTimes(1.0), {0.1, 0.6}));
        QCOMPARE(AiEmoji::sampleTimes(0.4).size(), 1);
    }

    void requestHelpers()
    {
        QCOMPARE(AiEmoji::endpointFor(QStringLiteral("https://api.openai.com/v1/")),
                 QStringLiteral("https://api.openai.com/v1/chat/completions"));
        QCOMPARE(AiEmoji::endpointFor(QStringLiteral("http://x/v1/chat/completions")), QStringLiteral("http://x/v1/chat/completions"));
        QVERIFY(AiEmoji::isLocalBaseUrl(QStringLiteral("http://localhost:11434/v1")));
        QVERIFY(AiEmoji::isLocalBaseUrl(QStringLiteral("http://127.0.0.1:8080")));
        QVERIFY(!AiEmoji::isLocalBaseUrl(QStringLiteral("https://api.deepseek.com")));
        QVERIFY(AiEmoji::isRetryable(429, false));
        QVERIFY(AiEmoji::isRetryable(503, false));
        QVERIFY(AiEmoji::isRetryable(0, true));
        for (const int s : {400, 401, 403, 404})
            QVERIFY(!AiEmoji::isRetryable(s, false));

        const QJsonObject req = QJsonDocument::fromJson(AiEmoji::buildRequest(QStringLiteral("m"), QStringLiteral("p"), "JPG")).object();
        QCOMPARE(req.value(u"model").toString(), QStringLiteral("m"));
        const QJsonArray content = req.value(u"messages").toArray().at(0).toObject().value(u"content").toArray();
        QCOMPARE(content.at(0).toObject().value(u"text").toString(), QStringLiteral("p"));
        QCOMPARE(content.at(1).toObject().value(u"image_url").toObject().value(u"url").toString(),
                 QStringLiteral("data:image/jpeg;base64,SlBH"));

        QString error;
        QCOMPARE(AiEmoji::parseReply(FakeApi::chat(QStringLiteral("😂")), &error), QStringLiteral("😂"));
        QVERIFY(AiEmoji::parseReply(R"({"error":{"message":"bad key"}})", &error).isEmpty());
        QCOMPARE(error, QStringLiteral("bad key"));

        AiConfig config{QStringLiteral("https://api.openai.com/v1"), QStringLiteral("gpt-4o-mini"), {}, {}};
        QVERIFY(AiMatcher::validate(config).contains(QStringLiteral("API Key")));
        config.baseUrl = QStringLiteral("http://localhost:1234/v1");
        QVERIFY(AiMatcher::validate(config).isEmpty());
    }

    void composeStrip()
    {
        QImage frame(100, 200, QImage::Format_RGB32);
        frame.fill(Qt::red);
        const QImage strip = AiFrameGrabber::composeStrip({frame, frame, frame});
        QCOMPARE(strip.size(), QSize(308, 200));
        QCOMPARE(QColor(strip.pixel(101, 100)), QColor(0x80, 0x80, 0x80)); // 帧间灰色间隔
        QImage wide(400, 200, QImage::Format_RGB32);
        wide.fill(Qt::blue);
        QCOMPARE(AiFrameGrabber::composeStrip({wide, wide, wide}).width(), AiFrameGrabber::kStripMaxWidth);
    }

    void grabFinalStickerArea()
    {
        StickerTask video = taskFor(m_video);
        video.startTime = 0.5;
        video.endTime = 2.5;
        video.crop = QRect(100, 50, 240, 240);
        video.cropRadius = 1.0;
        video.mirror = true;
        const AiFrameGrabber::Result r = AiFrameGrabber::grab(video, m_dir.filePath(QStringLiteral("grab")));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.image.size(), QSize(200 * 3 + 8, 200)); // 三帧，每帧缩到高 200
        QCOMPARE(QColor(r.image.pixel(0, 0)), QColor(Qt::white)); // 圆形外铺白底
        QVERIFY(r.jpeg.startsWith("\xFF\xD8"));

        const AiFrameGrabber::Result still = AiFrameGrabber::grab(taskFor(m_image), m_dir.filePath(QStringLiteral("grab2")));
        QVERIFY2(still.ok, qPrintable(still.error));
        QCOMPARE(still.image.size(), QSize(256, 192));
    }

    void matcherRetriesAndOverwrites()
    {
        FakeApi api;
        QVERIFY(api.listen());
        api.responses = {{500, "{}"}, {200, FakeApi::chat(QStringLiteral("😂 ✨"))}, {200, FakeApi::chat(QStringLiteral("没有合适的"))}};

        StickerTask missing = taskFor(m_image);
        missing.inputPath = m_dir.filePath(QStringLiteral("gone.png"));
        const QList<StickerTask> tasks{taskFor(m_video), taskFor(m_image), missing};

        AiMatcher matcher;
        matcher.setTimings(10, 0);
        QHash<QString, QPair<QString, QString>> results;
        connect(&matcher, &AiMatcher::itemFinished, this,
                [&](const QString& id, const QString& emoji, const QString& error) { results.insert(id, {emoji, error}); });
        QSignalSpy done(&matcher, &AiMatcher::finished);
        matcher.start(tasks, AiConfig{api.baseUrl(), QStringLiteral("vision-model"), {}, {}}, m_dir.filePath(QStringLiteral("work")));
        QVERIFY(done.wait(60000));
        QCOMPARE(done.first().at(0).toInt(), 1); // matched
        QCOMPARE(done.first().at(1).toInt(), 1); // empty
        QCOMPARE(done.first().at(2).toInt(), 1); // failed
        QCOMPARE(results.value(tasks.at(0).id).first, QStringLiteral("😂"));
        QCOMPARE(results.value(tasks.at(1).id).first, QString());
        QCOMPARE(results.value(tasks.at(2).id).second, QStringLiteral("源文件不存在"));

        QCOMPARE(api.requests.size(), 3); // 500 后重试一次
        const QByteArray head = api.requests.first().head;
        QVERIFY(head.startsWith("POST /v1/chat/completions"));
        QVERIFY(!head.toLower().contains("authorization")); // 本机且没填 Key
        const QJsonObject body = QJsonDocument::fromJson(api.requests.first().body).object();
        QCOMPARE(body.value(u"model").toString(), QStringLiteral("vision-model"));
    }

    void matcherGivesUpOnClientErrors()
    {
        FakeApi api;
        QVERIFY(api.listen());
        api.responses = {{401, R"({"error":{"message":"bad key"}})"}};
        AiMatcher matcher;
        matcher.setTimings(10, 0);
        QString error;
        connect(&matcher, &AiMatcher::itemFinished, this, [&](const QString&, const QString&, const QString& e) { error = e; });
        QSignalSpy done(&matcher, &AiMatcher::finished);
        matcher.start({taskFor(m_image)}, AiConfig{api.baseUrl(), QStringLiteral("m"), QStringLiteral("sk-test"), {}},
                      m_dir.filePath(QStringLiteral("work")));
        QVERIFY(done.wait(30000));
        QCOMPARE(error, QStringLiteral("HTTP 401: bad key"));
        QCOMPARE(api.requests.size(), 1);
        QVERIFY(api.requests.first().head.contains("Bearer sk-test"));
    }

    void matcherGivesUpAfterTwoRetries()
    {
        FakeApi api;
        QVERIFY(api.listen());
        api.responses = {{429, "{}"}, {502, "{}"}, {503, "{}"}};
        AiMatcher matcher;
        matcher.setTimings(10, 0);
        QString error;
        connect(&matcher, &AiMatcher::itemFinished, this, [&](const QString&, const QString&, const QString& e) { error = e; });
        QSignalSpy done(&matcher, &AiMatcher::finished);
        matcher.start({taskFor(m_image)}, AiConfig{api.baseUrl(), QStringLiteral("m"), {}, {}}, m_dir.filePath(QStringLiteral("work")));
        QVERIFY(done.wait(30000));
        QCOMPARE(api.requests.size(), 3);
        QCOMPARE(error, QStringLiteral("HTTP 503"));
    }

    void matcherCancel()
    {
        FakeApi api;
        QVERIFY(api.listen());
        AiMatcher matcher;
        QSignalSpy done(&matcher, &AiMatcher::finished);
        matcher.start({taskFor(m_video), taskFor(m_image)}, AiConfig{api.baseUrl(), QStringLiteral("m"), {}, {}},
                      m_dir.filePath(QStringLiteral("work")));
        matcher.cancel();
        QVERIFY(done.wait(30000));
        QVERIFY(done.first().at(3).toBool());
        QVERIFY(!matcher.isRunning());
    }
};

QTEST_GUILESS_MAIN(TestAi)
#include "tst_ai.moc"
