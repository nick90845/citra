// Copyright 2013 Dolphin Emulator Project / 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

// Directory separators, do we need this?
#define DIR_SEP "/"
#define DIR_SEP_CHR '/'

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

// The user data dir
#define ROOT_DIR "."
#define USERDATA_DIR "user"
#ifdef USER_DIR
#define EMU_DATA_DIR USER_DIR
#else
#ifdef _WIN32
#define EMU_DATA_DIR "Citra"
#else
#define EMU_DATA_DIR "citra-emu"
#endif
#endif

// Dirs in both User and Sys
#define EUR_DIR "EUR"
#define USA_DIR "USA"
#define JAP_DIR "JAP"

// Subdirs in the User dir returned by GetUserPath(UserPath::UserDir)
#define CONFIG_DIR "vvanelslande-config"
#define CACHE_DIR "cache"
#define SDMC_DIR "sdmc"
#define NAND_DIR "nand"
#define SYSDATA_DIR "sysdata"
#define LOG_DIR "log"
#define CHEATS_DIR "cheats"
#define DLL_DIR "external_dlls"
#define DUMP_DIR "dump"
#define LOAD_DIR "load"
#define SHADER_DIR "vvanelslande-shaders"

// Sys files
#define SHARED_FONT "shared_font.bin"
#define AES_KEYS "aes_keys.txt"
#define BOOTROM9 "boot9.bin"
#define SECRET_SECTOR "sector0x96.bin"
