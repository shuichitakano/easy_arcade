#pragma once

#include "app_config.h"
#include "pad_translator.h"

#include <vector>

inline constexpr char CONFIG_JSON_PATH[] = "0:/ea2_config.json";

enum class ConfigJsonResult
{
    OK,
    STORAGE_NOT_READY,
    FILE_NOT_FOUND,
    FILE_TOO_LARGE,
    IO_ERROR,
    INVALID_JSON,
    INVALID_FORMAT,
    INVALID_VALUE,
};

ConfigJsonResult exportConfigJson(const char *path,
                                  const AppConfig &appConfig,
                                  const std::vector<PadConfig> &padConfigs);

ConfigJsonResult importConfigJson(const char *path,
                                  AppConfig &appConfig,
                                  std::vector<PadConfig> &padConfigs);

const char *configJsonResultText(ConfigJsonResult result);
