#pragma once

namespace lumina::core {

// Axis-aligned rectangle in pixel coordinates with the origin at the top-left.
// `x`/`y` name the top-left corner and `width`/`height` are positive, matching the
// OpenCV/YOLO convention so downstream code never has to reinterpret the corners.
struct BoundingBox {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    [[nodiscard]] float area() const noexcept { return width * height; }
    [[nodiscard]] float centerX() const noexcept { return x + (width / 2.0F); }
    [[nodiscard]] float centerY() const noexcept { return y + (height / 2.0F); }
};

// A single object detected in one frame.
struct Detection {
    BoundingBox box;
    float score = 0.0F;  // confidence in [0, 1]
    int classId = -1;    // model class index; -1 means "unset"
};

}  // namespace lumina::core
