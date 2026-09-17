// exportmanager.h — video export: ffmpeg orchestration (Phase 3).
// Textually included into main.cpp; uses export options (gOptExport*),
// video/keyframe state, the device, modifier stack and pipe-mode helpers.
#pragma once

#ifdef _WIN32
#include <windows.h>
static PROCESS_INFORMATION gExportProc;
static HANDLE gExportStderrRead = NULL;
static int gExportRunning = 0;
static long gExportLogPos = 0;
static int gRemuxRunning = 0;
static PROCESS_INFORMATION gRemuxProc = {0};
static int gInExportFunc = 0;
static int gLastExportCheckpoint = 0;
static HANDLE gExportJob = NULL;

static LONG WINAPI exportVectoredHandler(EXCEPTION_POINTERS *ep)
{
	if (gInExportFunc)
	{
		DWORD code = ep->ExceptionRecord->ExceptionCode;
		const char *crashLog = "\\img2spec_crash.log";
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
		const char *base = strrchr(gVideoFilename, '\\');
		if (!base) base = strrchr(gVideoFilename, '/');
		if (base) base++; else base = gVideoFilename;
		_snprintf(gOptExportFilename, sizeof(gOptExportFilename) - 12, "%s", base);
		char *dot = strrchr(gOptExportFilename, '.');
		if (dot) *dot = 0;
		strcat(gOptExportFilename, "_spmz.mp4");
	}

#ifdef _WIN32
	// Ensure temp/ subdirectory exists in startup CWD
	char tempDir[MAX_PATH];
	_snprintf(tempDir, MAX_PATH, "%s\\temp", gStartupCwd);
	CreateDirectoryA(tempDir, NULL);

	// Get full path to this executable (has --pipe support)
	char exePath[MAX_PATH];
	GetModuleFileNameA(NULL, exePath, MAX_PATH);

	// Save current workspace (modifiers + device) to temp file
	char workspacePath[MAX_PATH];
	_snprintf(workspacePath, MAX_PATH, "%s\\img2spec_export.isw", tempDir);

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
	_snprintf(exportAbsPath, MAX_PATH, "%s\\%s", gStartupCwd, gOptExportFilename);

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
	_snprintf(progressPath, MAX_PATH + 32, "%s\\img2spec_export_progress.txt", tempDir);
	FILE *pf = fopen(progressPath, "w");
	if (pf) fclose(pf);

	// Save keyframes to temp file for pipe mode if any exist
	char keysPath[MAX_PATH] = "";
	if (gKeyframeCount > 0)
	{
		_snprintf(keysPath, MAX_PATH, "%s\\img2spec_export_keys.json", tempDir);
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
		_snprintf(_dlog, MAX_PATH, "%s\\img2spec_crash.log", gStartupCwd);
		FILE *_df = fopen(_dlog, "a");
		if (_df) {
			fprintf(_df, "DIAG args: loglevel=%p video=%p exe=%p ws=%p keys=%p fps=%p progress=%p gDevice=%p gVideoWidth=%d gVideoHeight=%d gOptExportScale=%d gOptExportFilename=%p gOptExportEncoder=%d gOptExportQuality=%d\n",
				(void*)loglevelStr, (void*)gVideoFilename, (void*)exePath, (void*)workspacePath,
				(void*)keysPath, (void*)fpsStr, (void*)progressPath, (void*)gDevice,
				gVideoWidth, gVideoHeight, gOptExportScale,
				(void*)gOptExportFilename, gOptExportEncoder, gOptExportQuality);
			fclose(_df);
		}
	}
	sprintf(cmd, "ffmpeg -loglevel %s -i \"%s\" "
		"-f rawvideo -pix_fmt rgb24 - | "
		"\"%s\" \"%s\" --pipe --width %d --height %d",
		loglevelStr, gVideoFilename,
		exePath, workspacePath,
		gVideoWidth, gVideoHeight);
	if (keysPath[0])
		sprintf(cmd + strlen(cmd), " --keys \"%s\"", keysPath);
	if (gOptInterpolateKeys)
		sprintf(cmd + strlen(cmd), " --interpolate");
	sprintf(cmd + strlen(cmd), " | "
		"ffmpeg -loglevel %s -y -sws_flags neighbor -f rawvideo -pix_fmt rgba -s %dx%d -framerate %s -i -"
		" -progress \"%s\""
		" -vf \"scale=iw*%d:-1:flags=neighbor\" ",
		loglevelStr,
		gDevice->mXRes, gDevice->mYRes,
		fpsStr,
		progressPath,
		gOptExportScale);
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

	// Encoder-specific args
	switch (gOptExportEncoder)
	{
		case 0: // NVIDIA NVENC
		{
			size_t clen = strlen(cmd);
			_snprintf(cmd + clen, sizeof(cmd) - clen - 1,
				"-c:v hevc_nvenc -profile:v main -pix_fmt yuv420p "
				"-preset fast -movflags +faststart -rc constqp -qp %d \"%s\"",
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
	_snprintf(logPath, MAX_PATH + 32, "%s\\img2spec_export_stderr.log", tempDir);

	char batchPath[MAX_PATH];
	_snprintf(batchPath, MAX_PATH, "%s\\img2spec_export.bat", tempDir);

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
	if (!gExportRunning && !gRemuxRunning) return;

	// Poll active audio remux (non-blocking)
	if (gRemuxRunning)
	{
		DWORD remuxExit = 0;
		if (GetExitCodeProcess(gRemuxProc.hProcess, &remuxExit) && remuxExit == STILL_ACTIVE)
			return; // still running, check next frame

		// Remux done
		if (remuxExit != 0)
			fprintf(stderr, "Export: audio remux failed (exit code %lu), video saved without audio\n", remuxExit);
		CloseHandle(gRemuxProc.hProcess);
		CloseHandle(gRemuxProc.hThread);
		gRemuxProc.hProcess = NULL;
		gRemuxProc.hThread = NULL;
		gRemuxRunning = 0;

		if (gOptExportCleanup)
		{
			char delPath[MAX_PATH + 32];
			_snprintf(delPath, MAX_PATH + 32, "%s\\temp\\img2spec_export_progress.txt", gStartupCwd);
			DeleteFileA(delPath);
			_snprintf(delPath, MAX_PATH + 32, "%s\\temp\\img2spec_export.bat", gStartupCwd);
			DeleteFileA(delPath);
			_snprintf(delPath, MAX_PATH + 32, "%s\\temp\\img2spec_export.isw", gStartupCwd);
			DeleteFileA(delPath);
			_snprintf(delPath, MAX_PATH + 32, "%s\\temp\\img2spec_export_keys.json", gStartupCwd);
			DeleteFileA(delPath);
			_snprintf(delPath, MAX_PATH + 32, "%s\\temp\\img2spec_export_stderr.log", gStartupCwd);
			DeleteFileA(delPath);
		}

		gVideoExportProgress = 1.0f;
		gVideoExportActive = false;
		fprintf(stderr, "Export complete: %s\n", gOptExportFilename);
		return;
	}

	DWORD exitCode = 0;
	if (GetExitCodeProcess(gExportProc.hProcess, &exitCode) && exitCode == STILL_ACTIVE)
	{
		// Read ffmpeg -progress file to extract real progress
		char progPath[MAX_PATH + 32];
		_snprintf(progPath, MAX_PATH + 32, "%s\\temp\\img2spec_export_progress.txt", gStartupCwd);

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
					int h, m;
					double s;
					if (sscanf(t, "out_time=%d:%d:%lf", &h, &m, &s) >= 3)
					{
						double secs = h * 3600.0 + m * 60.0 + s;
						if (gVideoDuration > 0.0)
						{
							float p = (float)(secs / gVideoDuration);
							if (p > 1.0f) p = 1.0f;
							gVideoExportProgress = p;
						}
					}
					else if (sscanf(t, "out_time=%lf", &s) >= 1)
					{
						if (gVideoDuration > 0.0)
						{
							float p = (float)(s / gVideoDuration);
							if (p > 1.0f) p = 1.0f;
							gVideoExportProgress = p;
						}
					}
				}
			}
			long newPos = ftell(pf);
			if (newPos >= 0) gExportLogPos = newPos;
			fclose(pf);
		}
	}
	else
	{
		// Encoding process done — close handles
		fprintf(stderr, "DIAG: poll_video_export() export process exited with code %lu\n", exitCode);
		gExportRunning = 0;
		CloseHandle(gExportProc.hProcess);
		CloseHandle(gExportProc.hThread);
		gExportProc.hProcess = NULL;
		gExportProc.hThread = NULL;
		if (gExportJob) { CloseHandle(gExportJob); gExportJob = NULL; }

		// Check if source video has an audio stream
		char probeCmd[4096];
		char probeResult[64] = "";
		_snprintf(probeCmd, sizeof(probeCmd),
			"ffprobe -v error -select_streams a:0 -show_entries stream=codec_type -of csv=p=0 \"%s\"",
			gVideoFilename);
		FILE *probe = _popen_no_window(probeCmd, "r");
		if (probe)
		{
			if (fgets(probeResult, sizeof(probeResult), probe))
			{
				size_t len = strlen(probeResult);
				if (len > 0 && probeResult[len-1] == '\n') probeResult[len-1] = 0;
			}
			// NOTE: fclose, not _pclose (custom pipe, not _popen — see videopipeline.h).
			// This whole function is _WIN32-only.
			fclose(probe);
		}

		int hasAudio = (strstr(probeResult, "audio") != NULL);

		if (hasAudio)
		{
			char tmpPath[MAX_PATH];
			_snprintf(tmpPath, sizeof(tmpPath), "%s", gOptExportFilename);
			char *dot = strrchr(tmpPath, '.');
			if (dot) *dot = 0;
			strcat(tmpPath, "_tmp.mp4");

			char exportAbsPath[MAX_PATH];
			_snprintf(exportAbsPath, MAX_PATH, "%s\\%s", gStartupCwd, gOptExportFilename);
			char tmpAbsPath[MAX_PATH];
			_snprintf(tmpAbsPath, MAX_PATH, "%s\\%s", gStartupCwd, tmpPath);

			static const char *rlognames[] = {"info", "error", "warning", "verbose", "debug"};
			int ridx = gOptExportLoglevel;
			if (ridx < 0 || ridx > 4) ridx = 0;
			const char *rlog = rlognames[ridx];
			char remuxCmd[8192];
			_snprintf(remuxCmd, sizeof(remuxCmd),
				"cmd.exe /c ffmpeg -loglevel %s -i \"%s\" -i \"%s\" "
				"-c:v copy -c:a aac -map 0:v:0 -map 1:a:0 -y \"%s\""
				"&& move /Y \"%s\" \"%s\"",
				rlog,
				exportAbsPath, gVideoFilename,
				tmpAbsPath, tmpAbsPath, exportAbsPath);

			STARTUPINFOA si2 = {0}; si2.cb = sizeof(si2);

			if (CreateProcessA(NULL, remuxCmd, NULL, NULL, FALSE,
				CREATE_NO_WINDOW, NULL, NULL, &si2, &gRemuxProc))
			{
				gRemuxRunning = 1;
				fprintf(stderr, "DIAG: poll_video_export() audio remux started\n");
			}
			else
			{
				fprintf(stderr, "Export: audio remux CreateProcess failed (error %d), video saved without audio\n", GetLastError());
			}
		}

		if (!gRemuxRunning)
		{
			if (gOptExportCleanup)
			{
				char delPath[MAX_PATH + 32];
				_snprintf(delPath, MAX_PATH + 32, "%s\\temp\\img2spec_export_progress.txt", gStartupCwd);
				DeleteFileA(delPath);
				_snprintf(delPath, MAX_PATH + 32, "%s\\temp\\img2spec_export.bat", gStartupCwd);
				DeleteFileA(delPath);
				_snprintf(delPath, MAX_PATH + 32, "%s\\temp\\img2spec_export.isw", gStartupCwd);
				DeleteFileA(delPath);
				_snprintf(delPath, MAX_PATH + 32, "%s\\temp\\img2spec_export_keys.json", gStartupCwd);
				DeleteFileA(delPath);
				_snprintf(delPath, MAX_PATH + 32, "%s\\temp\\img2spec_export_stderr.log", gStartupCwd);
				DeleteFileA(delPath);
			}

			gVideoExportProgress = 1.0f;
			gVideoExportActive = false;
			fprintf(stderr, "Export complete: %s\n", gOptExportFilename);
		}
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
	if (gRemuxRunning)
	{
		TerminateProcess(gRemuxProc.hProcess, 1);
		CloseHandle(gRemuxProc.hProcess);
		CloseHandle(gRemuxProc.hThread);
		gRemuxProc.hProcess = NULL;
		gRemuxProc.hThread = NULL;
		gRemuxRunning = 0;
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
