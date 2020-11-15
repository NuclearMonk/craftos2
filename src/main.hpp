/*
 * main.hpp
 * CraftOS-PC 2
 *
 * This file defines variables used on the command line that may be used by
 * other CraftOS-PC components.
 *
 * This code is licensed under the MIT license.
 * Copyright (c) 2019-2020 JackMacWindows.
 */

#ifndef MAIN_HPP
#define MAIN_HPP

#include <map>
#include <unordered_map>
#include <Terminal.hpp>

extern bool rawClient;
extern std::string overrideHardwareDriver;
extern std::map<uint8_t, Terminal*> rawClientTerminals;
extern std::unordered_map<unsigned, uint8_t> rawClientTerminalIDs;
extern std::string script_file;
extern std::string script_args;
extern int returnValue;

#endif