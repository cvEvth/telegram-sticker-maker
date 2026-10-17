// 裁切框计算与时间轴视图状态（需求 6.4 / 6.6）
#include "core/CropMath.h"
#include "core/TimelineState.h"

#include <QTest>

using namespace tsm;
using namespace tsm::CropMath;

namespace {
const QSize kFrame(1920, 1080);

bool isEven(const QRect& r)
{
    return r.x() % 2 == 0 && r.y() % 2 == 0 && r.width() % 2 == 0 && r.height() % 2 == 0;
}
} // namespace

class TestEditor : public QObject {
    Q_OBJECT

private slots:
    void aspectRatios()
    {
        QCOMPARE(*aspectRatio(CropAspect::Square, kFrame), 1.0);
        QVERIFY(!aspectRatio(CropAspect::Free, kFrame).has_value());
        QCOMPARE(*aspectRatio(CropAspect::Original, kFrame), 1920.0 / 1080.0);
        QCOMPARE(*aspectRatio(CropAspect::Portrait9x16, kFrame), 9.0 / 16.0);
    }

    void centeredMaxCrop()
    {
        QCOMPARE(centeredMax(kFrame, 1.0), QRect(420, 0, 1080, 1080));
        QCOMPARE(centeredMax(kFrame, std::nullopt), QRect(0, 0, 1920, 1080));
        const QRect portrait = centeredMax(kFrame, 9.0 / 16.0);
        QCOMPARE(portrait.height(), 1080);
        QVERIFY(qAbs(portrait.width() - 607) <= 2);
        QVERIFY(isEven(portrait));
        QVERIFY(QRect(QPoint(0, 0), kFrame).contains(portrait));
    }

    void evenAlignAndClamp()
    {
        QCOMPARE(evenAligned(QRectF(101.7, 3.2, 513.9, 511.1), kFrame), QRect(100, 2, 512, 510));
        QCOMPARE(evenAligned(QRectF(1800, 1000, 512, 512), kFrame), QRect(1408, 568, 512, 512));
        QCOMPARE(evenAligned(QRectF(0, 0, 3, 3), kFrame), QRect(0, 0, 16, 16));
        QCOMPARE(evenAligned(QRectF(-50, -50, 5000, 5000), kFrame), QRect(0, 0, 1920, 1080));
    }

    void mirrorKeepsSameContent()
    {
        // 镜像后框住的仍是同一块内容：x' = W - x - w
        QCOMPARE(mirrored(QRect(100, 20, 512, 512), kFrame), QRect(1308, 20, 512, 512));
        QCOMPARE(mirrored(mirrored(QRect(100, 20, 512, 512), kFrame), kFrame), QRect(100, 20, 512, 512));
    }

    void withSizeKeepsPositionAndClamps()
    {
        QCOMPARE(withSize(QRect(100, 100, 200, 200), QSize(512, 512), kFrame), QRect(100, 100, 512, 512));
        QCOMPARE(withSize(QRect(1800, 900, 100, 100), QSize(512, 512), kFrame), QRect(1408, 568, 512, 512));
    }

    void moveStaysInside()
    {
        QCOMPARE(moved(QRectF(100, 100, 500, 500), QPointF(-300, 5000), kFrame), QRectF(0, 580, 500, 500));
    }

    void resizeFree()
    {
        const QRectF r = resized(QRectF(100, 100, 400, 300), Handle::BottomRight, QPointF(50, -20), std::nullopt, kFrame);
        QCOMPARE(r, QRectF(100, 100, 450, 280));
        // 不能小于 16，不能越过对边
        const QRectF tiny = resized(QRectF(100, 100, 400, 300), Handle::Left, QPointF(1000, 0), std::nullopt, kFrame);
        QCOMPARE(tiny.width(), 16.0);
        QCOMPARE(tiny.right(), 500.0);
        // 不能超出画面
        const QRectF edge = resized(QRectF(100, 100, 400, 300), Handle::Top, QPointF(0, -500), std::nullopt, kFrame);
        QCOMPARE(edge.top(), 0.0);
    }

    void resizeCornerKeepsAspect()
    {
        const QRectF r = resized(QRectF(100, 100, 400, 400), Handle::BottomRight, QPointF(100, 20), 1.0, kFrame);
        QCOMPARE(r.topLeft(), QPointF(100, 100));
        QCOMPARE(r.width(), r.height());
        QCOMPARE(r.width(), 500.0);
        // 往画面外拖：受高度限制
        const QRectF big = resized(QRectF(100, 100, 400, 400), Handle::BottomRight, QPointF(5000, 5000), 1.0, kFrame);
        QCOMPARE(big.bottom(), 1080.0);
        QCOMPARE(big.width(), 980.0);
        // 左上角：右下角固定
        const QRectF tl = resized(QRectF(400, 400, 400, 400), Handle::TopLeft, QPointF(-100, -50), 1.0, kFrame);
        QCOMPARE(tl.bottomRight(), QPointF(800, 800));
        QCOMPARE(tl.width(), 500.0);
    }

    void resizeEdgeKeepsAspect()
    {
        const double a = 16.0 / 9.0;
        const QRectF r = resized(QRectF(100, 100, 320, 180), Handle::Right, QPointF(160, 0), a, kFrame);
        QCOMPARE(r.left(), 100.0);
        QCOMPARE(r.width(), 480.0);
        QCOMPARE(r.height(), 270.0);
        QCOMPARE(r.center().y(), 190.0); // 垂直方向以原中心扩展
        const QRectF b = resized(QRectF(100, 100, 400, 400), Handle::Bottom, QPointF(0, 2000), 1.0, kFrame);
        QCOMPARE(b.top(), 100.0);
        QCOMPARE(b.height(), 980.0);
        QVERIFY(b.left() >= 0 && b.right() <= 1920);
    }

    void tickSteps()
    {
        QCOMPARE(TimelineState::tickStep(60), 10.0);
        QCOMPARE(TimelineState::tickStep(8), 1.0);
        QCOMPARE(TimelineState::tickStep(0.12), 0.02);
        QCOMPARE(TimelineState::tickStep(3600), 600.0);
        QCOMPARE(TimelineState::tickStep(100000), 600.0);
    }

    void viewClampAndZoom()
    {
        TimelineState s;
        s.setDuration(60);
        QVERIFY(!s.isZoomed());
        s.zoomAround(30, 0.5);
        QCOMPARE(s.viewStart(), 15.0);
        QCOMPARE(s.viewEnd(), 45.0);
        QVERIFY(s.isZoomed());
        s.panBy(100);
        QCOMPARE(s.viewEnd(), 60.0);
        QCOMPARE(s.viewStart(), 30.0);
        s.zoomAround(50, 0.0001);
        QVERIFY(qAbs(s.viewSpan() - TimelineState::kMinSpan) < 1e-9);
        s.showAll();
        QCOMPARE(s.viewSpan(), 60.0);
        s.fitRange(10, 20);
        QCOMPARE(s.viewStart(), 9.5);
        QCOMPARE(s.viewEnd(), 20.5);
    }

    void spanHitTest()
    {
        TimelineState s;
        s.setDuration(10);
        s.setSpans({{1, 3, {}}, {2, 5, {}}, {7, std::nullopt, {}}});
        QCOMPARE(s.spanAt(1.5), 0);
        QCOMPARE(s.spanAt(2.5), 1); // 重叠时后添加的优先
        QCOMPARE(s.spanAt(6), -1);
        QCOMPARE(s.spanAt(7), 2);   // 未设终点的片段只命中起点
    }

    void followPlayhead()
    {
        TimelineState s;
        s.setDuration(60);
        s.setView(0, 10);
        s.setPlayhead(12);
        s.followPlayhead();
        QVERIFY(s.viewStart() <= 12 && s.viewEnd() >= 12);
        s.notifyManualPan();
        s.setView(40, 50);
        s.followPlayhead(); // 手动平移后暂停跟随
        QCOMPARE(s.viewStart(), 40.0);
    }
};

QTEST_GUILESS_MAIN(TestEditor)
#include "tst_editor.moc"
