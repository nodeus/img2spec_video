// videopipeline.h — video decode / navigation (Phase 3 lite).
// Textually included into main.cpp after globals and keyframe forward
// declarations; uses gDevice, bitmaps, textures, gDirty flags, run_pipe().
#pragma once

void get_video_frame(int frameNum)
{
	gDirty = 1;
	gDirtyPic = 1;
	// Clamp to the valid range (defensive: seeking past EOF yields no output)
	if (frameNum < 0) frameNum = 0;
	if (gVideoTotalFrames > 0 && frameNum >= gVideoTotalFrames)
		frameNum = gVideoTotalFrames - 1;
	gVideoCurrentFrame = frameNum;

	if (gVideoWidth == 0 || gVideoHeight == 0) return;

	double sec = (double)frameNum / gVideoFps;
	int vw = gVideoWidth;
	int vh = gVideoHeight;

	char cmd[4096];
#ifdef _WIN32
	sprintf(cmd,
		"ffmpeg -nostdin -ss %.3f -i \"%s\" -vframes 1 -f rawvideo -pix_fmt rgb24 "
		"-s %dx%d -v quiet -",
		sec, gVideoFilename, vw, vh);
	FILE *pipe = _popen_no_window(cmd, "rb");
#else
	sprintf(cmd,
		"ffmpeg -nostdin -ss %.3f -i \"%s\" -vframes 1 -f rawvideo -pix_fmt rgb24 "
		"-s %dx%d -v quiet -",
		sec, gVideoFilename, vw, vh);
	FILE *pipe = popen(cmd, "r");
#endif
	if (!pipe)
	{
#ifdef _WIN32
		fprintf(stderr, "DIAG: get_video_frame(%d) pipe open failed (error %lu)\n",
			frameNum, GetLastError());
#else
		fprintf(stderr, "DIAG: get_video_frame(%d) pipe open failed\n", frameNum);
#endif
		return;
	}

	unsigned char *buf = new unsigned char[vw * vh * 3];
	size_t read = fread(buf, 1, vw * vh * 3, pipe);
#ifdef _WIN32
	// NOTE: fclose, not _pclose: the stream comes from _open_osfhandle+_fdopen,
	// not _popen. _pclose leaks one CRT fd per call here (fd table exhausts
	// at 512 -> decoding dies after ~500 frames). fclose releases the fd.
	fclose(pipe);
#else
	pclose(pipe);
#endif

	if (read != (size_t)(vw * vh * 3))
	{
		fprintf(stderr, "DIAG: get_video_frame(%d) short read %lu/%d\n",
			frameNum, (unsigned long)read, vw * vh * 3);
		delete[] buf;
		return;
	}

	// Store original frame as RGBA in gSourceImageData (for ScalePosModifier)
	if (gSourceImageData)
		stbi_image_free(gSourceImageData);
	gSourceImageData = (unsigned int *)malloc(vw * vh * 4);
	gSourceImageX = vw;
	gSourceImageY = vh;

	if (gSourceImageData)
	{
		for (int i = 0; i < vw * vh; i++)
		{
			int r = buf[i * 3 + 0];
			int g = buf[i * 3 + 1];
			int b = buf[i * 3 + 2];
			gSourceImageData[i] = r | (g << 8) | (b << 16) | 0xff000000;
		}
	}

	// Copy into device buffer (centered, clipped — triggers modifiers + filter)
	for (int y = 0; y < gDevice->mYRes; y++)
	{
		for (int x = 0; x < gDevice->mXRes; x++)
		{
			int pix = 0xff000000;
			if (x < vw && y < vh)
			{
				int r = buf[(y * vw + x) * 3 + 0];
				int g = buf[(y * vw + x) * 3 + 1];
				int b = buf[(y * vw + x) * 3 + 2];
				pix = r | (g << 8) | (b << 16) | 0xff000000;
			}
			gBitmapOrig[y * gDevice->mXRes + x] = pix;
		}
	}

	delete[] buf;

	// Apply effective keyframe if it changed
	if (gKeyframesLoaded && gKeyframeCount > 0)
		keyframe_apply(frameNum);

	// Update Original texture so the window shows the current scrubbed frame
	update_texture(gTextureOrig, gBitmapOrig);
}

// Parse combined ffprobe output (2.4):
//   line 1: "width,height,r_frame_rate" (e.g. "640,480,25/1")
//   line 2: "duration" (e.g. "60.000000")
static bool parse_ffprobe_combined(const char *out, int *w, int *h, int *num, int *den, double *dur)
{
	if (!out || !w || !h || !num || !den || !dur) return false;

	char line1[256];
	const char *nl = strchr(out, '\n');
	size_t n = nl ? (size_t)(nl - out) : strlen(out);
	if (n == 0 || n >= sizeof(line1)) return false;
	memcpy(line1, out, n);
	line1[n] = 0;

	char fps[64] = "";
	int tw = 0, th = 0;
	if (sscanf(line1, "%d,%d,%63s", &tw, &th, fps) != 3) return false;
	if (tw <= 0 || th <= 0) return false;

	int tnum = 0, tden = 1;
	if (strchr(fps, '/'))
	{
		if (sscanf(fps, "%d/%d", &tnum, &tden) != 2 || tden <= 0) return false;
	}
	else
	{
		double f = atof(fps);
		if (f <= 0) return false;
		tnum = (int)(f * 1000 + 0.5);
		tden = 1000;
	}

	if (!nl) return false;
	double tdur = atof(nl + 1);
	if (tdur <= 0) return false;

	*w = tw; *h = th; *num = tnum; *den = tden; *dur = tdur;
	return true;
}

void load_video(const char *filename)
{
	if (!filename) return;

	// Flush any pending sidecar write for the previous video before switching
	keyframe_flush_sidecar();
	// Clear any existing keyframes from previous video
	keyframe_clear();
	gVideoPlaying = false;
	gVideoPlayLastTick = 0;

	char cmd[4096];
	const char *res;
	int vw = 0, vh = 0, num = 0, den = 1;
	double dur = 0;
	bool probed = false;

	// Single combined ffprobe call: resolution + fps + duration (2.4)
	sprintf(cmd, "ffprobe -v error -select_streams v:0 -show_entries stream=width,height,r_frame_rate -show_entries format=duration -of csv=p=0 \"%s\"", filename);
	res = run_pipe(cmd);
	if (res)
		probed = parse_ffprobe_combined(res, &vw, &vh, &num, &den, &dur);

	if (!probed)
	{
		// Fallback: three separate calls (legacy behavior)
		// ffprobe: resolution
		sprintf(cmd, "ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 \"%s\"", filename);
		res = run_pipe(cmd);
		if (!res) { printf("ffprobe error: can't get video info\n"); return; }
		if (sscanf(res, "%d,%d", &vw, &vh) != 2) return;

		// ffprobe: duration
		sprintf(cmd, "ffprobe -v error -show_entries format=duration -of csv=p=0 \"%s\"", filename);
		res = run_pipe(cmd);
		if (!res) return;
		dur = atof(res);

		// ffprobe: frame rate
		sprintf(cmd, "ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 \"%s\"", filename);
		res = run_pipe(cmd);
		if (!res) return;
		// r_frame_rate is "num/den" or "num"
		if (strchr(res, '/'))
		{
			num = 0; den = 1;
			sscanf(res, "%d/%d", &num, &den);
			if (den <= 0) { num = 25000; den = 1000; }
		}
		else
		{
			double f = atof(res);
			num = (int)(f * 1000 + 0.5);
			den = 1000;
		}
	}

	gVideoWidth = vw;
	gVideoHeight = vh;
	gVideoDuration = dur;
	gVideoFpsNum = num;
	gVideoFpsDen = den;
	gVideoFps = (den > 0) ? (double)num / den : 25.0;
	if (gVideoFps <= 0) { gVideoFps = 25.0; gVideoFpsNum = 25000; gVideoFpsDen = 1000; }

	gVideoTotalFrames = (int)(gVideoDuration * gVideoFps + 0.5);
	gVideoCurrentFrame = 0;
	gVideoMode = true;
	strcpy(gVideoFilename, filename);

	// Загружаем первый кадр
	get_video_frame(0);

	// Load keyframe sidecar for this video
	keyframe_load_sidecar();

	// Arm auto-capture: allow modifier changes to create keyframes
	// even if no sidecar existed yet
	if (!gKeyframesLoaded)
		gKeyframesLoaded = true;
}

