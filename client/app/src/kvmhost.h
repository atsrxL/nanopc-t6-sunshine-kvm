// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "kvmconfig.h"
#include "backend/nvcomputer.h"

#include <memory>

// Single-host adapter. Never launch or send input unless the upstream pairing state
// and the saved server certificate both match this binding.
class KvmHost
{
public:
    explicit KvmHost(KvmConfig& config) : m_config(config) {}
    void refresh();                  // throws on network / identity / pairing failure
    void pair(const QString& pin);   // throws on PIN or certificate failure
    NvApp hdmiApp();                  // only the configured HDMI app; never launches another app
    NvComputer* computer() { return m_computer.get(); }
private:
    KvmConfig& m_config;
    std::unique_ptr<NvComputer> m_computer;
};
