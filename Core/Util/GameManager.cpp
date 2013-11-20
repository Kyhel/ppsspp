// Copyright (c) 2013- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "file/file_util.h"
#include "native/ext/libzip/zip.h"

#include "Common/Log.h"
#include "Common/FileUtil.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Util/GameManager.h"

GameManager g_GameManager;

std::string GameManager::GetTempFilename() const {
	return g_Config.externalDirectory + "/ppsspp.dl";
}

bool GameManager::IsGameInstalled(std::string name) {
	std::string pspGame = GetSysDirectory(DIRECTORY_GAME);
	return File::Exists(pspGame + name);
}

bool GameManager::DownloadAndInstall(std::string storeZipUrl) {
	if (curDownload_.get() != 0) {
		ERROR_LOG(HLE, "Can only process one download at a time");
		return false;
	}

	// TODO: Android-compatible temp file names
	std::string filename = GetTempFilename();
	curDownload_ = downloader_.StartDownload(storeZipUrl, filename);
	return true;
}

void GameManager::Uninstall(std::string name) {
	std::string gameDir = GetSysDirectory(DIRECTORY_GAME) + name;
	INFO_LOG(HLE, "Deleting %s", gameDir.c_str());
	if (!File::Exists(gameDir)) {
		ERROR_LOG(HLE, "Game %s not installed, cannot uninstall", name.c_str());
		return;
	}

	bool success = File::DeleteDirRecursively(gameDir);
	if (success) {
		INFO_LOG(HLE, "Successfully deleted game %s", name.c_str());
		g_Config.CleanRecent();
	} else {
		ERROR_LOG(HLE, "Failed to delete game %s", name.c_str());
	}
}

void GameManager::Update() {
	if (curDownload_.get() && curDownload_->Done()) {
		INFO_LOG(HLE, "Download completed! Status = %i", curDownload_->ResultCode());
		if (curDownload_->ResultCode() == 200) {
			if (!File::Exists(curDownload_->outfile())) {
				ERROR_LOG(HLE, "Downloaded file %s does not exist :(", curDownload_->outfile().c_str());
				return;
			}
			// Install the game!
			std::string zipName = curDownload_->outfile();
			InstallGame(zipName);
			// Doesn't matter if the install succeeds or not, we delete the temp file to not squander space.
			// TODO: Handle disk full?
			deleteFile(zipName.c_str());
			curDownload_.reset();
		}
	}
}

void GameManager::InstallGame(std::string zipfile) {
	std::string pspGame = GetSysDirectory(DIRECTORY_GAME);
	INFO_LOG(HLE, "Installing %s into %s", zipfile.c_str(), pspGame.c_str());

	int error;
	struct zip *z = zip_open(zipfile.c_str(), 0, &error);
	if (!z) {
		ERROR_LOG(HLE, "Failed to open ZIP file %s, error code=%i", zipfile.c_str(), error);
		return;
	}

	int numFiles = zip_get_num_files(z);

	// First, find all the directories, and precreate them before we fill in with files.
	for (int i = 0; i < numFiles; i++) {
		const char *fn = zip_get_name(z, i, 0);
		std::string outFilename = pspGame + fn;
		bool isDir = outFilename.back() == '/';
		if (isDir) {
			File::CreateFullPath(outFilename.c_str());
		}
	}

	// Now, loop through again, writing files.
	for (int i = 0; i < numFiles; i++) {
		const char *fn = zip_get_name(z, i, 0);
		// Note that we do NOT write files that are not in a directory, to avoid random
		// README files etc.
		if (strstr(fn, "/") != 0) {
			INFO_LOG(HLE, "File: %i: %s", i, fn);
			struct zip_stat zstat;
			int x = zip_stat_index(z, i, 0, &zstat);
			size_t size = zstat.size;
			u8 *buffer = new u8[size];
			zip_file *zf = zip_fopen_index(z, i, 0);
			zip_fread(zf, buffer, size);
			zip_fclose(zf);

			std::string outFilename = pspGame + fn;
			bool isDir = outFilename.back() == '/';
			if (!isDir) {
				INFO_LOG(HLE, "Writing %i bytes to %s", (int)size, outFilename.c_str());
				FILE *f = fopen(outFilename.c_str(), "wb");
				if (f) {
					fwrite(buffer, 1, size, f);
					fclose(f);
				} else {
					ERROR_LOG(HLE, "Failed to open file for writing");
				}
				delete [] buffer;
			}
		}
	}

	zip_close(z);
}
