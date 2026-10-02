// exportmanager.h — video export: ffmpeg orchestration (Phase 3).
// Textually included into main.cpp; uses export options (gOptExport*),
// video/keyframe state, the device, modifier stack and pipe-mode helpers.
#pragma once

static int gExportRunning = 0;
static double gExportRangeSecs = 0.0;  // progress denominator (range duration)
static int gExportRangeFrames = 0;     // decode progress denominator (range frames)
static long gDecodeLogPos = 0;         // incremental read pos in decode -progress file
static Uint32 gExportStartTicks = 0;   // SDL_GetTicks() at export start (for ETA)

#ifdef _WIN32
#include <windows.h>
static PROCESS_INFORMATION gExportProc;
static HANDLE gExportStderrRead = NULL;
static long gExportLogPos = 0;
static int gInExportFunc = 0;
static int gLastExportCheckpoint = 0;
static HANDLE gExportJob = NULL;

// Delete all files inside dir (non-recursive) and remove the dir itself.
// Used by Cleanup for temp/scr and temp/png which may hold thousands frames.
static void export_delete_dir_files(const char *dir)
{
	char pattern[MAX_PATH + 16];
	_snprintf(pattern, sizeof(pattern), "%s" PATH_SEP "*", dir);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE)
		return;
	do
	{
		if (fd.cFileName[0] == '.' && (fd.cFileName[1] == 0 ||
			(fd.cFileName[1] == '.' && fd.cFileName[2] == 0)))
			continue;
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			char fp[MAX_PATH + 64];
			_snprintf(fp, sizeof(fp), "%s" PATH_SEP "%s", dir, fd.cFileName);
			DeleteFileA(fp);
		}
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	RemoveDirectoryA(dir);
}

// Remove the 6 service files plus per-frame dumps (temp/scr, temp/png).
static void export_cleanup_temp_files()
{
	char delPath[MAX_PATH + 32];
	_snprintf(delPath, MAX_PATH + 32, "%s" PATH_SEP "temp" PATH_SEP "img2spec_export_progress.txt", gStartupCwd);
	DeleteFileA(delPath);
	_snprintf(delPath, MAX_PATH + 32, "%s" PATH_SEP "temp" PATH_SEP "img2spec_export_decode_progress.txt", gStartupCwd);
	DeleteFileA(delPath);
	_snprintf(delPath, MAX_PATH + 32, "%s" PATH_SEP "temp" PATH_SEP "img2spec_export.bat", gStartupCwd);
	DeleteFileA(delPath);
	_snprintf(delPath, MAX_PATH + 32, "%s" PATH_SEP "temp" PATH_SEP "img2spec_export.isw", gStartupCwd);
	DeleteFileA(delPath);
	_snprintf(delPath, MAX_PATH + 32, "%s" PATH_SEP "temp" PATH_SEP "img2spec_export_keys.json", gStartupCwd);
	DeleteFileA(delPath);
	_snprintf(delPath, MAX_PATH + 32, "%s" PATH_SEP "temp" PATH_SEP "img2spec_export_stderr.log", gStartupCwd);
	DeleteFileA(delPath);
	char dumpDir[MAX_PATH + 32];
	_snprintf(dumpDir, MAX_PATH + 32, "%s" PATH_SEP "temp" PATH_SEP "scr", gStartupCwd);
	export_delete_dir_files(dumpDir);
	_snprintf(dumpDir, MAX_PATH + 32, "%s" PATH_SEP "temp" PATH_SEP "png", gStartupCwd);
	export_delete_dir_files(dumpDir);
}

static LONG WINAPI exportVectoredHandler(EXCEPTION_POINTERS *ep)
{
	if (gInExportFunc)
	{
		DWORD code = ep->ExceptionRecord->ExceptionCode;
		const char *crashLog = PATH_SEP "img2spec_crash.log";
		char logPath[MAX_PATH];
		_snprintf(logPath, MAX_PATH, "%s%s", gStartupCwd, crashLog);
		FILE *cf = fopen(logPath, "a");
		if (cf) {
			SYSTEMTIME st;
			GetLocalTime(&st);
			fprintf(cf, "[%04d-%02d-%02d %02d:%02d:%02d] CRASH in start_video_export() at checkpoint %d: exception code=0x%08lX addr=0x%p\n",
				st.wYear, st.wMonth, st.wDay,
				st.wHour, st.wMinute, st.wSecond,
				gLastExportCheckpoint, code,
				(void*)ep->ExceptionRecord->ExceptionAddress);
			fclose(cf);
		}
		fprintf(stderr, "CRASH at checkpoint %d: code=0x%08lX addr=0x%p\n",
			gLastExportCheckpoint, code, (void*)ep->ExceptionRecord->ExceptionAddress);
		gExportRunning = 0;
		gVideoExportActive = false;
		return EXCEPTION_EXECUTE_HANDLER;
	}
	return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void start_video_export()
{
#ifdef _WIN32
	gInExportFunc = 1;
	gLastExportCheckpoint = 0;
#endif

	if (gOptExportFilename[0] == 0)
	{
		export_set_default_filename();
	}
	export_clamp_range();

	int rangeIn = gVideoExportIn;
	int rangeOut = gVideoExportOut;
	if (gVideoTotalFrames > 1)
	{
		if (rangeIn < 0) rangeIn = 0;
		if (rangeOut >= gVideoTotalFrames) rangeOut = gVideoTotalFrames - 1;
		if (rangeIn > rangeOut) rangeIn = rangeOut;
	}
	else
	{
		rangeIn = 0; rangeOut = 0;
	}
	int rangeFrames = rangeOut - rangeIn + 1;
	if (rangeFrames < 1) rangeFrames = 1;
	double rangeSecs = (gVideoFps > 0.0) ? (double)rangeFrames / gVideoFps : 0.0;
	gExportRangeSecs = rangeSecs;
	gExportRangeFrames = rangeFrames;
	gExportStartTicks = SDL_GetTicks();
	gDecodeLogPos = 0;
	double inSec = (gVideoFps > 0.0) ? (double)rangeIn / gVideoFps : 0.0;
	bool isGif = (gOptExportFormat == 2);
#ifndef _WIN32
	// GUI export runs on Windows only; silence unused warnings elsewhere.
	(void)rangeFrames; (void)inSec; (void)isGif;
#endif

#ifdef _WIN32
	// Ensure temp/ subdirectory exists in startup CWD
	char tempDir[MAX_PATH];
	_snprintf(tempDir, MAX_PATH, "%s" PATH_SEP "temp", gStartupCwd);
	CreateDirectoryA(tempDir, NULL);

	// Per-frame dump dirs for --dump-scr / --dump-png (checkboxes in Export window)
	char dumpScrDir[MAX_PATH] = "";
	char dumpPngDir[MAX_PATH] = "";
	if (gOptExportDumpScr)
	{
		_snprintf(dumpScrDir, MAX_PATH, "%s" PATH_SEP "scr", tempDir);
		CreateDirectoryA(dumpScrDir, NULL);
	}
	if (gOptExportDumpPng)
	{
		_snprintf(dumpPngDir, MAX_PATH, "%s" PATH_SEP "png", tempDir);
		CreateDirectoryA(dumpPngDir, NULL);
	}

	// Get full path to this executable (has --pipe support)
	char exePath[MAX_PATH];
	GetModuleFileNameA(NULL, exePath, MAX_PATH);

	// Save current workspace (modifiers + device) to temp file
	char workspacePath[MAX_PATH];
	_snprintf(workspacePath, MAX_PATH, "%s" PATH_SEP "img2spec_export.isw", tempDir);

	fprintf(stderr, "DIAG: export checkpoint 1 - build_applystack\n");
	gLastExportCheckpoint = 1;
	build_applystack();
	JSON_Value *root_value = json_value_init_object();
	JSON_Object *root = json_value_get_object(root_value);
	json_object_dotset_string(root, "About.WhatIsThis", "Image Spectrumizer " VERSION " workspace file");
	json_object_dotset_string(root, "About.Magic", "0x50534D49");
	json_object_dotset_number(root, "About.Version", 4);

#define WRITECONFIG(x) json_object_dotset_number(root, "Config." #x, x);
	WRITECONFIG(gDeviceId);
#undef WRITECONFIG

	fprintf(stderr, "DIAG: export checkpoint 2 - serialize_snapshot_to_json\n");
	gLastExportCheckpoint = 2;
	serialize_snapshot_to_json(root);

	fprintf(stderr, "DIAG: export checkpoint 3 - json_serialize_to_file\n");
	gLastExportCheckpoint = 3;
	json_serialize_to_file_pretty(root_value, workspacePath);
	json_value_free(root_value);

	char exportAbsPath[MAX_PATH];
	_snprintf(exportAbsPath, MAX_PATH, "%s" PATH_SEP "%s", gStartupCwd, gOptExportFilename);

	// Framerate string: -r 60 or -r 24000/1001
	char fpsStr[32];
	if (gVideoFpsDen == 1)
		_snprintf(fpsStr, sizeof(fpsStr), "%d", gVideoFpsNum);
	else
		_snprintf(fpsStr, sizeof(fpsStr), "%d/%d", gVideoFpsNum, gVideoFpsDen);

	static const char *loglevel_names[] = {"info", "error", "warning", "verbose", "debug"};
	int loglevel_idx = gOptExportLoglevel;
	if (loglevel_idx < 0 || loglevel_idx > 4) loglevel_idx = 0;
	const char *loglevelStr = loglevel_names[loglevel_idx];

	char progressPath[MAX_PATH + 32];
	_snprintf(progressPath, MAX_PATH + 32, "%s" PATH_SEP "img2spec_export_progress.txt", tempDir);
	FILE *pf = fopen(progressPath, "w");
	if (pf) fclose(pf);

	// Decode-side progress: frame= lines, works at any loglevel. This is the
	// only progress source that moves during GIF export (palettegen buffers
	// all frames, so the encoder reports nothing until the very end).
	char decodeProgressPath[MAX_PATH + 32];
	_snprintf(decodeProgressPath, MAX_PATH + 32, "%s" PATH_SEP "img2spec_export_decode_progress.txt", tempDir);
	pf = fopen(decodeProgressPath, "w");
	if (pf) fclose(pf);

	// Save keyframes to temp file for pipe mode if any exist
	char keysPath[MAX_PATH] = "";
	if (gKeyframeCount > 0)
	{
		_snprintf(keysPath, MAX_PATH, "%s" PATH_SEP "img2spec_export_keys.json", tempDir);
		// Write keyframes to temp file
		JSON_Value *kv = json_value_init_object();
		JSON_Object *ko = json_value_get_object(kv);
		json_object_dotset_number(ko, "Video.FpsNum", gVideoFpsNum);
		json_object_dotset_number(ko, "Video.FpsDen", gVideoFpsDen);
		json_object_dotset_number(ko, "Video.TotalFrames", gVideoTotalFrames);
		json_object_set_value(ko, "Keys", json_value_init_array());
		JSON_Array *karr = json_object_get_array(ko, "Keys");
		for (int i = 0; i < gKeyframeCount; i++)
		{
			JSON_Value *entryVal = json_value_init_object();
			json_array_append_value(karr, entryVal);
			JSON_Object *entry = json_value_get_object(entryVal);
			json_object_dotset_number(entry, "frame", gKeyframes[i].frame);
			if (gKeyframes[i].snapshot)
			{
				JSON_Object *snap = json_value_get_object(gKeyframes[i].snapshot);
				size_t fieldCount = json_object_get_count(snap);
				for (size_t j = 0; j < fieldCount; j++)
				{
					const char *key = json_object_get_name(snap, j);
					JSON_Value *val = json_object_get_value(snap, key);
					json_object_set_value(entry, key, json_value_deep_copy(val));
				}
			}
		}
		fprintf(stderr, "DIAG: export checkpoint 4 - save keyframes (%d)\n", gKeyframeCount);
		gLastExportCheckpoint = 4;
		json_serialize_to_file_pretty(kv, keysPath);
		json_value_free(kv);
	}

	fprintf(stderr, "DIAG: export checkpoint 5 - build command\n");
	gLastExportCheckpoint = 5;
	static char cmd[16384];
	gLastExportCheckpoint = 51;
	{
		char _dlog[MAX_PATH];
		_snprintf(_dlog, MAX_PATH, "%s" PATH_SEP "img2spec_crash.log", gStartupCwd);
		FILE *_df = fopen(_dlog, "a");
		if (_df) {
			fprintf(_df, "DIAG args: loglevel=%p video=%p exe=%p ws=%p keys=%p fps=%p progress=%p gDevice=%p gVideoWidth=%d gVideoHeight=%d gOptExportScale=%d gOptExportFilename=%p gOptExportEncoder=%d gOptExportQuality=%d gOptExportFormat=%d range=%d-%d\n",
				(void*)loglevelStr, (void*)gVideoFilename, (void*)exePath, (void*)workspacePath,
				(void*)keysPath, (void*)fpsStr, (void*)progressPath, (void*)gDevice,
				gVideoWidth, gVideoHeight, gOptExportScale,
				(void*)gOptExportFilename, gOptExportEncoder, gOptExportQuality,
				gOptExportFormat, rangeIn, rangeOut);
			fclose(_df);
		}
	}
	sprintf(cmd, "ffmpeg -loglevel %s -ss %.3f -i \"%s\" -frames:v %d "
		"-progress \"%s\" -f rawvideo -pix_fmt rgb24 - | "
		"\"%s\" \"%s\" --pipe --width %d --height %d",
		loglevelStr, inSec, gVideoFilename, rangeFrames,
		decodeProgressPath,
		exePath, workspacePath,
		gVideoWidth, gVideoHeight);
	if (keysPath[0])
		sprintf(cmd + strlen(cmd), " --keys \"%s\"", keysPath);
	if (gOptInterpolateKeys)
		sprintf(cmd + strlen(cmd), " --interpolate");
	if (gOptExportDumpScr)
		sprintf(cmd + strlen(cmd), " --dump-scr \"%s\"", dumpScrDir);
	if (gOptExportDumpPng)
		sprintf(cmd + strlen(cmd), " --dump-png \"%s\"", dumpPngDir);
	if (isGif)
	{
		// GIF: no audio, palettegen+paletteuse in a single ffmpeg pass
		sprintf(cmd + strlen(cmd), " | "
			"ffmpeg -loglevel %s -y -sws_flags neighbor -f rawvideo -pix_fmt rgba -s %dx%d -framerate %s -i -"
			" -progress \"%s\""
			" -vf \"scale=iw*%d:-1:flags=neighbor,split[s0][s1];[s0]palettegen=max_colors=256[p];[s1][p]paletteuse=dither=bayer\" ",
			loglevelStr,
			gDevice->mXRes, gDevice->mYRes,
			fpsStr,
			progressPath,
			gOptExportScale);
	}
	else
	{
	sprintf(cmd + strlen(cmd), " | "
		"ffmpeg -loglevel %s -y -sws_flags neighbor -f rawvideo -pix_fmt rgba -s %dx%d -framerate %s -i -"
		" -ss %.3f -t %.3f -i \"%s\""
		" -progress \"%s\""
		" -vf \"scale=iw*%d:-1:flags=neighbor\" "
		// Single-pass audio: source as 2nd input (cut to the same range),
		// video from pipe (0:v), audio optional (1:a:0?) so no pre-probe
		// is needed; -shortest ends the output with the shorter stream.
		"-map 0:v:0 -map 1:a:0? -c:a aac -shortest ",
		loglevelStr,
		gDevice->mXRes, gDevice->mYRes,
		fpsStr,
		inSec, rangeSecs,
		gVideoFilename,
		progressPath,
		gOptExportScale);
	}
	gLastExportCheckpoint = 52;
	cmd[sizeof(cmd) - 1] = '\0';
	gLastExportCheckpoint = 53;

	// User extra params
	if (gOptExportExtraParams[0])
	{
		size_t clen = strlen(cmd);
		_snprintf(cmd + clen, sizeof(cmd) - clen - 1, "%s ", gOptExportExtraParams);
	}
	gLastExportCheckpoint = 54;

	// Encoder-specific args (mp4/mkv only; GIF uses palette filter above)
	if (isGif)
	{
		size_t clen = strlen(cmd);
		_snprintf(cmd + clen, sizeof(cmd) - clen - 1,
			"\"%s\"",
			exportAbsPath);
	}
	else switch (gOptExportEncoder)
	{
		case 0: // NVIDIA NVENC
		{
			size_t clen = strlen(cmd);
			_snprintf(cmd + clen, sizeof(cmd) - clen - 1,
				"-c:v hevc_nvenc -profile:v main -pix_fmt yuv420p "
				"-preset fast%s -rc constqp -qp %d \"%s\"",
				(gOptExportFormat == 0) ? " -movflags +faststart" : "",
				gOptExportQuality, exportAbsPath);
		}
		break;
	case 1: // AMD AMF
		{
			size_t clen = strlen(cmd);
			_snprintf(cmd + clen, sizeof(cmd) - clen - 1,
				"-c:v hevc_amf -rc cqp -qp_p %d -qp_i %d -pix_fmt yuv420p \"%s\"",
				gOptExportQuality, gOptExportQuality, exportAbsPath);
		}
		break;
	default: // CPU x264
		{
			size_t clen = strlen(cmd);
			_snprintf(cmd + clen, sizeof(cmd) - clen - 1,
				"-c:v libx264 -crf %d -pix_fmt yuv420p \"%s\"",
				gOptExportQuality, exportAbsPath);
		}
		break;
	}
	gLastExportCheckpoint = 55;

	fprintf(stderr, "DIAG: export checkpoint 6 - write batch file\n");
	gLastExportCheckpoint = 6;
	// Write batch file (needed for cmd.exe pipeline with |)
	// Use group redirect 2>>"log" (... ) to capture ALL stderr (cmd.exe + pipe processes)
	char logPath[MAX_PATH + 32];
	_snprintf(logPath, MAX_PATH + 32, "%s" PATH_SEP "img2spec_export_stderr.log", tempDir);

	char batchPath[MAX_PATH];
	_snprintf(batchPath, MAX_PATH, "%s" PATH_SEP "img2spec_export.bat", tempDir);

	FILE *f = fopen(batchPath, "w");
	if (!f)
	{
		gExportRunning = 0;
		gVideoExportActive = false;
		fprintf(stderr, "Export: cannot create batch file '%s'\n", batchPath);
		return;
	}
	fprintf(f, "@echo off\n");
	fprintf(f, "echo [%%DATE%% %%TIME%%] Before pipe > \"%s\"\n", logPath);
	fprintf(f, "2>>\"%s\" (\n", logPath);
	fprintf(f, "  %s\n", cmd);
	fprintf(f, ")\n");
	fprintf(f, "echo [%%DATE%% %%TIME%%] Exit=%%ERRORLEVEL%% >> \"%s\"\n", logPath);
	fclose(f);

	fprintf(stderr, "DIAG: export checkpoint 7 - CreateProcess\n");
	gLastExportCheckpoint = 7;
	// Run batch file via cmd.exe (CREATE_NO_WINDOW = no console window)
	char cmdline[MAX_PATH + 32];
	sprintf(cmdline, "cmd.exe /c \"%s\"", batchPath);

	STARTUPINFOA si = {0};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {0};

	gExportRunning = 1;
	gVideoExportProgress = 0.0f;
	gVideoExportActive = true;
	gExportLogPos = 0;

	if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
		CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
	{
		gExportRunning = 0;
		gVideoExportProgress = 0.0f;
		gVideoExportActive = false;
		fprintf(stderr, "Export: CreateProcess failed (error %d)\n", GetLastError());
	}
	else
	{
		gExportProc = pi;
		gExportJob = CreateJobObject(NULL, NULL);
		if (gExportJob)
		{
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
			jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			SetInformationJobObject(gExportJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
			AssignProcessToJobObject(gExportJob, pi.hProcess);
		}
		fprintf(stderr, "DIAG: export checkpoint 8 - CreateProcess OK\n");
		gLastExportCheckpoint = 8;
	}
#endif

#ifdef _WIN32
	gInExportFunc = 0;
#endif
}

void poll_video_export()
{
#ifdef _WIN32
	if (!gExportRunning) return;

	DWORD exitCode = 0;
	if (GetExitCodeProcess(gExportProc.hProcess, &exitCode) && exitCode == STILL_ACTIVE)
	{
		float pDec = -1.0f;
		// Decode-side progress (frame= lines): the only source that moves
		// during GIF export, since palettegen buffers all frames and the
		// encoder reports nothing until the very end.
		{
			char decPath[MAX_PATH + 32];
			_snprintf(decPath, MAX_PATH + 32, "%s" PATH_SEP "temp" PATH_SEP "img2spec_export_decode_progress.txt", gStartupCwd);
			FILE *df = fopen(decPath, "r");
			if (df)
			{
				fseek(df, gDecodeLogPos, SEEK_SET);
				char line[512];
				int lastFrame = -1;
				while (fgets(line, sizeof(line), df))
				{
					char *t = strstr(line, "frame=");
					if (t)
					{
						int fr = -1;
						if (sscanf(t, "frame=%d", &fr) == 1 && fr > lastFrame)
							lastFrame = fr;
					}
				}
				long newPos = ftell(df);
				if (newPos >= 0) gDecodeLogPos = newPos;
				fclose(df);
				if (lastFrame >= 0 && gExportRangeFrames > 0)
				{
					pDec = (float)lastFrame / (float)gExportRangeFrames;
					if (pDec > 1.0f) pDec = 1.0f;
				}
			}
		}

		// Read ffmpeg -progress file to extract real progress
		char progPath[MAX_PATH + 32];
		_snprintf(progPath, MAX_PATH + 32, "%s" PATH_SEP "temp" PATH_SEP "img2spec_export_progress.txt", gStartupCwd);

		FILE *pf = fopen(progPath, "r");
		if (pf)
		{
			fseek(pf, gExportLogPos, SEEK_SET);
			char line[512];
			while (fgets(line, sizeof(line), pf))
			{
				char *t = strstr(line, "out_time=");
				if (t)
				{
					// Progress denominator: export range duration (set at start),
					// falls back to full video duration.
					double denom = (gExportRangeSecs > 0.0) ? gExportRangeSecs : gVideoDuration;
					int h, m;
					double s;
					if (sscanf(t, "out_time=%d:%d:%lf", &h, &m, &s) >= 3)
					{
						double secs = h * 3600.0 + m * 60.0 + s;
						if (denom > 0.0)
						{
							float p = (float)(secs / denom);
							if (p > 1.0f) p = 1.0f;
							if (pDec >= 0.0f && pDec > p) p = pDec;
							gVideoExportProgress = p;
						}
					}
					else if (sscanf(t, "out_time=%lf", &s) >= 1)
					{
						if (denom > 0.0)
						{
							float p = (float)(s / denom);
							if (p > 1.0f) p = 1.0f;
							if (pDec >= 0.0f && pDec > p) p = pDec;
							gVideoExportProgress = p;
						}
					}
				}
			}
			long newPos = ftell(pf);
			if (newPos >= 0) gExportLogPos = newPos;
			fclose(pf);
		}
		// Encode file may not exist yet (GIF: nothing encoded until decode
		// ends) — decode progress alone still moves the bar.
		if (pDec >= 0.0f && pDec > gVideoExportProgress)
			gVideoExportProgress = pDec;
	}
	else
	{
		// Encoding process done — close handles. Audio (if any) was muxed
		// in the same pass (-map 1:a:0?), so export is complete.
		fprintf(stderr, "DIAG: poll_video_export() export process exited with code %lu\n", exitCode);
		gExportRunning = 0;
		CloseHandle(gExportProc.hProcess);
		CloseHandle(gExportProc.hThread);
		gExportProc.hProcess = NULL;
		gExportProc.hThread = NULL;
		if (gExportJob) { CloseHandle(gExportJob); gExportJob = NULL; }

		if (exitCode != 0)
			fprintf(stderr, "Export: encoding failed (exit code %lu)\n", exitCode);

		if (gOptExportCleanup)
			export_cleanup_temp_files();

		gVideoExportProgress = 1.0f;
		gVideoExportActive = false;
		fprintf(stderr, "Export complete: %s\n", gOptExportFilename);
	}
#endif
}

void cancel_video_export()
{
#ifdef _WIN32
	// Close job handle first — kills ALL processes in the job tree
	if (gExportJob)
	{
		CloseHandle(gExportJob);
		gExportJob = NULL;
	}
	if (gExportRunning)
	{
		TerminateProcess(gExportProc.hProcess, 1);
		CloseHandle(gExportProc.hProcess);
		CloseHandle(gExportProc.hThread);
		gExportProc.hProcess = NULL;
		gExportProc.hThread = NULL;
		gExportRunning = 0;
	}
	gVideoExportActive = false;
#endif
}
