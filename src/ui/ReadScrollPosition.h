#pragma once

#include <QtGlobal>

// A reading location tied to Markdown source rather than a raw scrollbar
// value. The source position survives rendered-layout changes; the pixel
// offset keeps that source at the same viewport height, and the ratio is a
// fallback for content that cannot be mapped exactly (tables, images, etc.).
struct ReadScrollPosition {
    int sourcePosition = -1;
    qreal viewportOffset = 0.0;
    qreal fallbackRatio = 0.0;
};
