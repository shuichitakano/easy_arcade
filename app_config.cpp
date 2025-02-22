/*
 * author : Shuichi TAKANO
 * since  : Sat Apr 13 2024 19:27:25
 */

#include "app_config.h"
#include "serializer.h"

void AppConfig::serialize(Serializer &s)
{
    s.append8u(VERSION);
    s.append8u(initPowerOn);
    s.append8u(dispFPS);
    s.append8u(buttonDispMode);
    s.append8u(backLight);
    s.append8u(rapidModeSynchro);
    s.append8u(synchroFetchPhase);
    s.append8u(softwareRapidSpeed);
    for (int i = 0; i < 6; ++i)
    {
        s.append8u(rapidPhase[i]);
    }
    for (auto &rs : rapidSettings)
    {
        s.append32u(rs.mask);
        s.append8u(rs.div);
    }
    for (auto &re : rotEnc)
    {
        s.append8u(re.reverse);
        s.append16u(re.scale);
        s.append8u(re.axis);
    }
    s.append8u(twinPortMode);
    s.append8u(analogMode);
    for (auto &v : analogSettings)
    {
        s.append8i(v.sensitivity);
        s.append8i(v.offset);
        s.append8u(v.scale);
    }
}

bool AppConfig::deserialize(Deserializer &s)
{
    if (s.peek8u() != VERSION)
    {
        return false;
    }

    initPowerOn = s.peek8u();
    dispFPS = s.peek8u();
    buttonDispMode = s.peek8u();
    backLight = s.peek8u();
    rapidModeSynchro = s.peek8u();
    synchroFetchPhase = s.peek8u();
    softwareRapidSpeed = s.peek8u();
    for (int i = 0; i < 6; ++i)
    {
        rapidPhase[i] = s.peek8u();
    }
    for (auto &rs : rapidSettings)
    {
        rs.mask = s.peek32u();
        rs.div = s.peek8u();
    }
    for (auto &re : rotEnc)
    {
        re.reverse = s.peek8u();
        re.scale = s.peek16u();
        re.axis = s.peek8u();
    }
    twinPortMode = s.peek8u();
    analogMode = s.peek8u();
    for (auto &v : analogSettings)
    {
        v.sensitivity = s.peek8i();
        v.offset = s.peek8i();
        v.scale = s.peek8u();
    }

    return true;
}
