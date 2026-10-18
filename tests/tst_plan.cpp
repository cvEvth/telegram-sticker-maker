// 画质规划与重试规则、滤镜链拼装
#include "core/EncodePlan.h"
#include "media/FilterGraph.h"

#include <QTest>

using namespace tsm;
using namespace tsm::EncodePlan;

class TestPlan : public QObject {
    Q_OBJECT

private:
    static Input input(QSize source, double fps, double duration, OutputMode mode = OutputMode::Sticker)
    {
        Input in;
        in.source = source;
        in.sourceFps = fps;
        in.duration = duration;
        in.mode = mode;
        return in;
    }

private slots:
    void sizes()
    {
        QCOMPARE(videoContentSize({1920, 1080}, OutputMode::Sticker), QSize(512, 288));
        QCOMPARE(videoContentSize({1080, 1920}, OutputMode::Sticker), QSize(288, 512));
        QCOMPARE(videoContentSize({1000, 1000}, OutputMode::Sticker), QSize(512, 512));
        QCOMPARE(videoContentSize({640, 270}, OutputMode::Sticker), QSize(512, 224)); // 216 → 16 的倍数 224
        QCOMPARE(videoContentSize({5000, 10}, OutputMode::Sticker), QSize(512, 16));  // 至少 16
        QCOMPARE(videoContentSize({1920, 1080}, OutputMode::Emoji), QSize(100, 56));
        QCOMPARE(canvasSize({100, 56}, OutputMode::Emoji), QSize(100, 100));
        QCOMPARE(staticContentSize({800, 600}, OutputMode::Sticker), QSize(512, 384));
        QCOMPARE(staticContentSize({100, 75}, OutputMode::Sticker), QSize(512, 384)); // 小图放大
        QCOMPARE(staticContentSize({700, 300}, OutputMode::Sticker), QSize(512, 219));
    }

    void bitrate()
    {
        QCOMPARE(bitrateKbps(252 * 1024, 3.0), 688);
        QCOMPARE(bitrateKbps(252 * 1024, 0.5), 2000); // 上限
        QCOMPARE(bitrateKbps(60 * 1024, 120.0), 16);  // 下限
    }

    void fpsTiers()
    {
        QCOMPARE(nextLowerFps(30), 24.0);
        QCOMPARE(nextLowerFps(15), 12.0);
        QCOMPARE(nextLowerFps(12), 12.0);
        QCOMPARE(nextHigherFps(20, 30), 24.0);
        QCOMPARE(nextHigherFps(24, 25), 25.0);
        QCOMPARE(nextHigherFps(30, 30), 30.0);
    }

    void chooseFpsShortClipKeeps30()
    {
        // 3 秒 512x288 @30fps：bpp ≈ 0.153 ≥ 0.055
        const Plan p = plan(input({1920, 1080}, 30, 3.0));
        QCOMPARE(p.fps, 30.0);
        QVERIFY(p.constantQuality); // ≤4 秒且 bpp ≥ 0.10
        QCOMPARE(p.crf, 18);
        QVERIFY(p.twoPass);
    }

    void chooseFpsLongClipDropsTiers()
    {
        // 10 秒 512x512：30fps bpp≈0.026，各档都不够 → 15；15fps bpp≈0.053 ≥ 0.040，不进应急档
        const Plan p = plan(input({1000, 1000}, 30, 10.0));
        QCOMPARE(p.fps, 15.0);
        QVERIFY(!p.constantQuality);
    }

    void chooseFpsEmergency()
    {
        // 20 秒 512x512：15fps bpp≈0.026 < 0.040 → 应急 12fps
        QCOMPARE(plan(input({1000, 1000}, 30, 20.0)).fps, 12.0);
    }

    void chooseFpsPicksFirstAboveTarget()
    {
        // 5 秒 512x288：30fps bpp≈0.092 → 选 30
        QCOMPARE(plan(input({1920, 1080}, 30, 5.0)).fps, 30.0);
        // 6 秒 512x512：30→0.044、24→0.0547、20→0.066 → 选 20
        QCOMPARE(plan(input({1000, 1000}, 30, 6.0)).fps, 20.0);
        // 8 秒 512x512：18→0.0547 仍不足 0.055 → 15
        QCOMPARE(plan(input({1000, 1000}, 30, 8.0)).fps, 15.0);
    }

    void nativeLowFpsKept()
    {
        // GIF 10fps：不升帧
        QCOMPARE(plan(input({480, 480}, 10, 3.0)).fps, 10.0);
        // 原生 25fps：候选从 24 开始
        QCOMPARE(plan(input({1920, 1080}, 25, 3.0)).fps, 24.0);
    }

    void motionAdjustsFps()
    {
        Input in = input({1000, 1000}, 30, 8.0); // 基础选 15
        in.motion = 0.003;                        // 几乎静止 → 原帧率
        QCOMPARE(plan(in).fps, 30.0);
        in.motion = 0.010;                        // 中运动 → 升一档
        QCOMPARE(plan(in).fps, 18.0);
        in.motion = 0.030;                        // 剧烈运动 → 保持
        const Plan p = plan(in);
        QCOMPARE(p.fps, 15.0);
        QCOMPARE(p.motionLabel, QStringLiteral("高运动 0.030"));
        QVERIFY(p.summary().contains(QStringLiteral("30→15fps")));
        QVERIFY(p.summary().contains(QStringLiteral("ABR ")));
    }

    void motionIgnoredForShortClips()
    {
        Input in = input({1000, 1000}, 30, 3.0);
        in.motion = 0.0;
        QVERIFY(plan(in).motionLabel.isEmpty());
    }

    void constantQualityByPreset()
    {
        QCOMPARE(constantQualityFor(EncodePreset::Quality, OutputMode::Sticker), 18);
        QCOMPARE(constantQualityFor(EncodePreset::Quality, OutputMode::Emoji), 24);
        QCOMPARE(constantQualityFor(EncodePreset::Fast, OutputMode::Emoji), 26);
        Input in = input({1920, 1080}, 30, 3.0);
        QVERIFY(plan(in).twoPass);
        in.preset = EncodePreset::Fast;
        QVERIFY(!plan(in).twoPass);
    }

    void retryFirstOvershootScalesBitrate()
    {
        const Plan p = plan(input({1000, 1000}, 30, 8.0));
        RetryState s{p.fps, 420, 0, 0, false};
        const Decision d = evaluate(p, s, 301 * 1024);
        QCOMPARE(d.verdict, Verdict::Retry);
        // 420 × 256/301 × 0.97 = 346.5
        QCOMPARE(s.bitrateKbps, 346);
        QCOMPARE(s.fps, p.fps);
        QCOMPARE(d.note, QStringLiteral("体积 301.0KB 超限，码率 420k→346k..."));
    }

    void retrySecondOvershootDropsFps()
    {
        const Plan p = plan(input({1920, 1080}, 30, 5.0));
        RetryState s{24, 340, 1, 1, false};
        const Decision d = evaluate(p, s, qint64(270.1 * 1024));
        QCOMPARE(d.verdict, Verdict::Retry);
        QCOMPARE(s.fps, 20.0);
        QVERIFY(s.bitrateKbps < 340);
        QVERIFY(d.note.startsWith(QStringLiteral("体积 270.1KB 超限，24→20fps 重压")));
    }

    void retryGivesUpAfterFour()
    {
        const Plan p = plan(input({1920, 1080}, 30, 5.0));
        RetryState s{15, 100, 4, 4, false};
        const Decision d = evaluate(p, s, 300 * 1024);
        QCOMPARE(d.verdict, Verdict::Fail);
        QCOMPARE(d.note, QStringLiteral("无法将体积压缩至 256KB 以内"));
    }

    void retryRaisesUnderfilledAbrOnce()
    {
        const Plan p = plan(input({1000, 1000}, 30, 8.0)); // ABR，理论码率 258k
        RetryState s{p.fps, 220, 0, 0, false};
        const Decision d = evaluate(p, s, 180 * 1024);
        QCOMPARE(d.verdict, Verdict::Retry);
        QVERIFY(s.raised);
        QVERIFY(s.bitrateKbps <= int(p.theoreticalKbps * 1.08));
        QVERIFY(s.bitrateKbps >= int(220 * 1.05));
        QVERIFY(d.note.contains(QStringLiteral("偏低，上调码率 220k→")));
        // 只上调一次
        QCOMPARE(evaluate(p, s, 180 * 1024).verdict, Verdict::Accept);
    }

    void retryNoRaiseForConstantQualityOrSmallGain()
    {
        const Plan cq = plan(input({1920, 1080}, 30, 3.0));
        QVERIFY(cq.constantQuality);
        RetryState s{cq.fps, cq.bitrateKbps, 0, 0, false};
        QCOMPARE(evaluate(cq, s, 100 * 1024).verdict, Verdict::Accept);

        const Plan abr = plan(input({1000, 1000}, 30, 8.0));
        RetryState near{abr.fps, 270, 0, 0, false};
        // 上限 258k × 1.08 = 278k，比 270k 高不到 5% → 不重试
        QCOMPARE(evaluate(abr, near, 220 * 1024).verdict, Verdict::Accept);
    }

    void emojiLimits()
    {
        const Plan p = plan(input({1920, 1080}, 30, 3.0, OutputMode::Emoji));
        QCOMPARE(p.canvas, QSize(100, 100));
        QCOMPARE(p.targetBytes, qint64(60 * 1024));
        QCOMPARE(p.limitBytes, qint64(64 * 1024));
        QCOMPARE(p.crf, 24);
    }

    void evenCrop()
    {
        QCOMPARE(FilterGraph::evenCrop(QRect(101, 3, 513, 511), {1920, 1080}), QRect(100, 2, 512, 510));
        QCOMPARE(FilterGraph::evenCrop(QRect(1800, 1000, 512, 512), {1920, 1080}), QRect(1408, 568, 512, 512));
        QCOMPARE(FilterGraph::evenCrop(QRect(0, 0, 4, 4), {1920, 1080}), QRect(0, 0, 16, 16));
    }

    void filterGraphOrder()
    {
        FilterGraph::Spec s;
        s.mirror = true;
        s.crop = QRect(100, 0, 512, 512);
        s.scale = {512, 512};
        s.canvas = {512, 512};
        s.fps = 24;
        s.roundMask = true;
        s.alphaOutput = true;
        const QString g = FilterGraph::build(s);
        const qsizetype hflip = g.indexOf(QStringLiteral("hflip"));
        const qsizetype crop = g.indexOf(QStringLiteral("crop=512:512:100:0"));
        const qsizetype scale = g.indexOf(QStringLiteral("scale=512:512:flags=lanczos"));
        const qsizetype denoise = g.indexOf(QStringLiteral("atadenoise"));
        const qsizetype fps = g.indexOf(QStringLiteral("fps=24"));
        const qsizetype merge = g.indexOf(QStringLiteral("alphamerge"));
        QVERIFY(hflip >= 0 && hflip < crop && crop < scale && scale < denoise && denoise < fps && fps < merge);
        QVERIFY(g.contains(QStringLiteral("out_color_matrix=bt709:out_range=tv")));
        QVERIFY(g.contains(QStringLiteral("format=yuva420p")));
        QVERIFY(!g.contains(QStringLiteral("blend")));
        QVERIFY(g.endsWith(QStringLiteral("[out]")));
    }

    void filterGraphSourceAlphaAndPad()
    {
        FilterGraph::Spec s;
        s.scale = {100, 56};
        s.canvas = {100, 100};
        s.preset = EncodePreset::Fast;
        s.roundMask = true;
        s.sourceAlpha = true;
        s.alphaOutput = true;
        const QString g = FilterGraph::build(s);
        QVERIFY(g.contains(QStringLiteral("blend=all_mode=multiply")));
        QVERIFY(g.contains(QStringLiteral("pad=100:100:(ow-iw)/2:(oh-ih)/2:color=black@0")));
        QVERIFY(!g.contains(QStringLiteral("unsharp")));
        QVERIFY(!g.contains(QStringLiteral("atadenoise")));
    }
};

QTEST_GUILESS_MAIN(TestPlan)
#include "tst_plan.moc"
