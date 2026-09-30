#include "pad_config_serialization.h"
#include "pad_translator.h"
#include "serializer.h"
#include "debug.h"

namespace {
void serializeConfig(const PadConfig &config, Serializer &s)
{
    s.append16u(config.getVID());
    s.append16u(config.getPID());
    s.append8u(config.getOutPortOfs());
    auto writeUnit = [&](const PadConfig::Unit &v) {
        s.append8u(static_cast<uint8_t>(v.type));
        s.append8u(v.number);
        s.append8u(static_cast<uint8_t>(v.analogOn));
        s.append8u(static_cast<uint8_t>(v.analogOff));
        s.append8u(static_cast<uint8_t>(v.hatPos));
        s.append8u(v.index);
        s.append8u(v.subIndex);
        s.append8u(v.inPortOfs);
    };
    s.append16u(config.getButtonCount());
    for (size_t i = 0; i < config.getButtonCount(); ++i)
        writeUnit(config.getButtonUnit(i));
    s.append8u(config.getAnalogCount());
    for (size_t i = 0; i < config.getAnalogCount(); ++i)
        writeUnit(config.getAnalogUnit(i));
}

PadConfig deserializeConfig(Deserializer &s)
{
    const auto vid = s.peek16u();
    const auto pid = s.peek16u();
    const auto offset = s.peek8u();
    PadConfig config(vid, pid, offset, {}, {});
    auto readUnit = [&]() {
        PadConfig::Unit v;
        v.type = static_cast<PadConfig::Type>(s.peek8u());
        v.number = s.peek8u();
        v.analogOn = static_cast<PadConfig::AnalogPos>(s.peek8u());
        v.analogOff = static_cast<PadConfig::AnalogPos>(s.peek8u());
        v.hatPos = static_cast<PadConfig::HatPos>(s.peek8u());
        v.index = s.peek8u();
        v.subIndex = s.peek8u();
        v.inPortOfs = s.peek8u();
        return v;
    };
    auto &buttons = config.getButtonUnits();
    auto n = s.peek16u();
    buttons.reserve(n);
    for (unsigned i = 0; i < n; ++i)
        buttons.push_back(readUnit());
    auto &analogs = config.getAnalogUnits();
    n = s.peek8u();
    analogs.reserve(n);
    for (unsigned i = 0; i < n; ++i)
        analogs.push_back(readUnit());
    return config;
}
}

void serializePadTranslator(const PadTranslator &translator, Serializer &s)
{
    const auto &configs = translator.getConfigs();
    s.append(configs.size());
    int n = 0;
    for (const auto &config : configs)
    {
        // Preserve the existing enabled-record encoding and limit behavior.
        if (s.exceedLimit())
            s.append8u(0);
        s.append8u(1);
        serializeConfig(config, s);
        ++n;
    }
    DPRINT(("store %d/%d configs.\n", n, configs.size()));
}

void deserializePadTranslator(PadTranslator &translator, Deserializer &s)
{
    // Release the old configs before constructing replacements, as before.
    translator.replaceConfigs({});
    std::vector<PadConfig> configs;
    const auto nn = s.peek();
    DPRINT(("%d configs...\n", nn));
    int n = 0;
    while (n < nn && s.peek8u())
    {
        configs.push_back(deserializeConfig(s));
        ++n;
    }
    DPRINT(("load %d/%d configs.\n", n, nn));
    translator.replaceConfigs(std::move(configs));
}
