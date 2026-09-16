// ---------------------------------------------------------------------------
// Spanish (es_MX) object-class catalog. Class ids are the COCO indices used by
// the YOLO11n model (verified against ultralytics cfg/datasets/coco.yaml,
// 2026-09-16). The label strings live here, not in pipeline logic (FR-08/INV-042).
// ---------------------------------------------------------------------------

#include "i18n/es.hpp"

namespace lumina::i18n {

ObjectClass fromCocoId(int cocoClassId) noexcept {
    switch (cocoClassId) {
        case 0:
            return ObjectClass::Person;
        case 1:
            return ObjectClass::Bicycle;
        case 2:
            return ObjectClass::Car;
        case 3:
            return ObjectClass::Motorcycle;
        case 5:
            return ObjectClass::Bus;
        case 7:
            return ObjectClass::Truck;
        case 15:
            return ObjectClass::Cat;
        case 16:
            return ObjectClass::Dog;
        case 24:
            return ObjectClass::Backpack;
        case 56:
            return ObjectClass::Chair;
        case 57:
            return ObjectClass::Couch;
        case 60:
            return ObjectClass::DiningTable;
        default:
            return ObjectClass::Unknown;
    }
}

const char* spanishLabel(ObjectClass objectClass) noexcept {
    switch (objectClass) {
        case ObjectClass::Person:
            return "persona";
        case ObjectClass::Bicycle:
            return "bicicleta";
        case ObjectClass::Car:
            return "coche";
        case ObjectClass::Motorcycle:
            return "motocicleta";
        case ObjectClass::Bus:
            return "autobús";
        case ObjectClass::Truck:
            return "camión";
        case ObjectClass::Cat:
            return "gato";
        case ObjectClass::Dog:
            return "perro";
        case ObjectClass::Backpack:
            return "mochila";
        case ObjectClass::Chair:
            return "silla";
        case ObjectClass::Couch:
            return "sofá";
        case ObjectClass::DiningTable:
            return "mesa";
        case ObjectClass::Unknown:
            return "";
    }
    return "";
}

}  // namespace lumina::i18n
