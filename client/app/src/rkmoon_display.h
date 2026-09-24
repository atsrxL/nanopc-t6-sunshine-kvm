// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "backend/nvhttp.h"
#include <stdexcept>

struct RkmoonDisplay {
    QString status;
    int width = 0, height = 0, fpsX100 = 0;
    bool ready() const { return status == QStringLiteral("ready"); }
    bool operator==(const RkmoonDisplay& other) const {
        return status == other.status && width == other.width &&
               height == other.height && qAbs(fpsX100 - other.fpsX100) <= 15;
    }
    bool operator!=(const RkmoonDisplay& other) const { return !(*this == other); }
    static RkmoonDisplay parse(const QString& xml) {
        if (NvHTTP::getXmlString(xml, "RKMoonDisplayVersion") != QStringLiteral("1"))
            throw std::runtime_error("Upgrade the server: RKMoon display information version 1 is required");
        RkmoonDisplay mode;
        mode.status = NvHTTP::getXmlString(xml, "RKMoonDisplayStatus");
        if (mode.ready()) {
            auto positive = [&](const char* tag) {
                const auto value = NvHTTP::getXmlString(xml, tag);
                bool ok = false;
                int number = value.toInt(&ok);
                if (!ok || number <= 0 || QString::number(number) != value)
                    throw std::runtime_error("Invalid server display dimensions or frame rate");
                return number;
            };
            mode.width = positive("RKMoonDisplayWidth");
            mode.height = positive("RKMoonDisplayHeight");
            mode.fpsX100 = positive("RKMoonDisplayFpsX100");
            if (mode.width > 4096 || mode.height > 2160 || mode.fpsX100 > 24000)
                throw std::runtime_error("Server display mode exceeds client limits");
        } else if (mode.status != "no_signal" && mode.status != "unsupported" && mode.status != "unavailable")
            throw std::runtime_error("Invalid server display status");
        return mode;
    }
};
