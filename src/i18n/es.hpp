#pragma once

namespace lumina::i18n {

// The object classes Lúmina narrates in the beta (a subset of the 80 COCO
// classes the YOLO11n model knows). Keeping this as an enum (not raw ints) makes
// the pipeline self-documenting and the Spanish labels data-driven (FR-08).
// C++ note (for Java readers): `enum class` is scoped and type-safe, the closest
// C++ equivalent to a Java enum (no implicit conversion to int).
enum class ObjectClass {
    Person,
    Bicycle,
    Car,
    Motorcycle,
    Bus,
    Truck,
    Cat,
    Dog,
    Backpack,
    Chair,
    Couch,
    DiningTable,
    Unknown,  // a class the model knows but we do not narrate
};

// Map a raw COCO class id (the model's `classId`) to our subset.
// Returns ObjectClass::Unknown for anything not in the subset.
[[nodiscard]] ObjectClass fromCocoId(int cocoClassId) noexcept;

// Spanish label for a tracked class (es_MX, INV-042). Returns "" for Unknown.
[[nodiscard]] const char* spanishLabel(ObjectClass objectClass) noexcept;

}  // namespace lumina::i18n
