// SPDX-License-Identifier: GPL-3.0-or-later

#include <QColor>
#include <QImage>
#include <QString>

#include <algorithm>
#include <cstdlib>
#include <iostream>

int main(int argc, char *argv[])
{
    if (argc != 4) {
        std::cerr << "usage: hydrogen-visual-compare BASELINE ACTUAL DIFF\n";
        return EXIT_FAILURE;
    }

    const QImage baseline(QString::fromLocal8Bit(argv[1]));
    const QImage actual(QString::fromLocal8Bit(argv[2]));
    const QString differencePath = QString::fromLocal8Bit(argv[3]);
    if (baseline.isNull() || actual.isNull()) {
        std::cerr << "could not load baseline or actual image\n";
        return EXIT_FAILURE;
    }

    const int width = std::max(baseline.width(), actual.width());
    const int height = std::max(baseline.height(), actual.height());
    QImage difference(width, height, QImage::Format_ARGB32);
    difference.fill(QColor(22, 28, 38));

    quint64 changedPixels = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool inBaseline = x < baseline.width() && y < baseline.height();
            const bool inActual = x < actual.width() && y < actual.height();
            const QRgb baselinePixel = inBaseline ? baseline.pixel(x, y) : qRgba(0, 0, 0, 0);
            const QRgb actualPixel = inActual ? actual.pixel(x, y) : qRgba(0, 0, 0, 0);
            if (baselinePixel == actualPixel) {
                const QColor unchanged(actualPixel);
                difference.setPixelColor(
                    x,
                    y,
                    QColor(unchanged.red() / 4, unchanged.green() / 4, unchanged.blue() / 4));
            } else {
                ++changedPixels;
                difference.setPixelColor(x, y, QColor(255, 45, 96));
            }
        }
    }

    if (changedPixels == 0) {
        return EXIT_SUCCESS;
    }

    if (!difference.save(differencePath)) {
        std::cerr << "could not write difference image\n";
    }
    std::cerr << changedPixels << " pixels differ; actual=" << actual.width() << 'x'
              << actual.height() << ", baseline=" << baseline.width() << 'x'
              << baseline.height() << '\n';
    return EXIT_FAILURE;
}
