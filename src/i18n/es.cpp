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
            return "carro";
        case ObjectClass::Motorcycle:
            return "moto";
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

const char* spanishPlural(ObjectClass objectClass) noexcept {
    switch (objectClass) {
        case ObjectClass::Person:
            return "personas";
        case ObjectClass::Bicycle:
            return "bicicletas";
        case ObjectClass::Car:
            return "carros";
        case ObjectClass::Motorcycle:
            return "motos";
        case ObjectClass::Bus:
            return "autobuses";
        case ObjectClass::Truck:
            return "camiones";
        case ObjectClass::Cat:
            return "gatos";
        case ObjectClass::Dog:
            return "perros";
        case ObjectClass::Backpack:
            return "mochilas";
        case ObjectClass::Chair:
            return "sillas";
        case ObjectClass::Couch:
            return "sofás";
        case ObjectClass::DiningTable:
            return "mesas";
        case ObjectClass::Unknown:
            return "";
    }
    return "";
}

const char* indefiniteArticle(ObjectClass objectClass) noexcept {
    switch (objectClass) {
        case ObjectClass::Person:
        case ObjectClass::Bicycle:
        case ObjectClass::Motorcycle:
        case ObjectClass::Backpack:
        case ObjectClass::Chair:
        case ObjectClass::DiningTable:
            return "una";
        case ObjectClass::Car:
        case ObjectClass::Bus:
        case ObjectClass::Truck:
        case ObjectClass::Cat:
        case ObjectClass::Dog:
        case ObjectClass::Couch:
            return "un";
        case ObjectClass::Unknown:
            return "";
    }
    return "";
}

const char* numberWord(int count) noexcept {
    switch (count) {
        case 2:
            return "dos";
        case 3:
            return "tres";
        case 4:
            return "cuatro";
        case 5:
            return "cinco";
        case 6:
            return "seis";
        case 7:
            return "siete";
        case 8:
            return "ocho";
        case 9:
            return "nueve";
        case 10:
            return "diez";
        default:
            return nullptr;
    }
}

const char* proximityAlertPhrase(bool veryClose) noexcept {
    // Two fixed phrasings so both can be pre-rendered into the TTS cache (INV-051).
    return veryClose ? "cuidado, obstáculo cerca." : "obstáculo cerca.";
}

std::string greeting(std::string_view name) {
    // Approved wording: "{NAME} está enfrente". No leading "Hola" and no trailing
    // punctuation (the TTS adds its own prosody at the end of an utterance).
    std::string phrase(name);
    phrase += " está enfrente";
    return phrase;
}

}  // namespace lumina::i18n
