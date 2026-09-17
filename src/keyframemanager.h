// keyframemanager.h — video keyframes: snapshots, sidecar, interpolation (Phase 3 lite).
// Textually included into main.cpp; uses video globals, gDirty flags,
// the modifier stack, serialize/deserialize_snapshot_*() from main.cpp.
#pragma once

// Video keyframes: per-frame full snapshots (Device + Stack)
struct VideoKeyframe {
	int frame;
	JSON_Value *snapshot;  // owns a JSON value with Device + Stack
};

#define KEYFRAME_MAX 4096
VideoKeyframe gKeyframes[KEYFRAME_MAX];
int gKeyframeCount = 0;
bool gKeyframesLoaded = false;  // sidecar loaded for current video
bool gKeyframeSuspendCapture = false;  // suppress auto-capture during apply/load
int gLastAppliedKeyframeIdx = -1;  // index of last applied key (for change detection)
JSON_Value *gKeyframeClipboard = NULL;  // clipboard for copy/paste key params
bool gOptInterpolateKeys = false;  // interpolate modifier parameters between keyframes

// --- Video keyframes ---

// Build sidecar path: <video_filename>.keyframes.json
static void keyframe_sidecar_path(char *out, int outSize)
{
	const char *base = strrchr(gVideoFilename, '\\');
	if (!base) base = strrchr(gVideoFilename, '/');
	if (base) base++; else base = gVideoFilename;
	_snprintf(out, outSize, "%s\\%s.keyframes.json", gStartupCwd, base);
}

// Find effective keyframe index for a given frame (max frame <= target)
// Returns -1 if no key applies (use base state)
static int keyframe_find_effective(int frame)
{
	int best = -1;
	for (int i = 0; i < gKeyframeCount; i++)
	{
		if (gKeyframes[i].frame <= frame)
		{
			if (best < 0 || gKeyframes[i].frame > gKeyframes[best].frame)
				best = i;
		}
	}
	return best;
}

// Find the first keyframe with frame > targetFrame, returns -1 if none
static int keyframe_find_next(int targetFrame)
{
	int best = -1;
	for (int i = 0; i < gKeyframeCount; i++)
	{
		if (gKeyframes[i].frame > targetFrame)
		{
			if (best < 0 || gKeyframes[i].frame < gKeyframes[best].frame)
				best = i;
		}
	}
	return best;
}

// Recursively interpolate numeric values between two JSON objects
// Non-numeric, non-object values are kept from dst (earlier keyframe)
static void json_interpolate(JSON_Object *dst, const JSON_Object *src, double t)
{
	size_t count = json_object_get_count(dst);
	for (size_t i = 0; i < count; i++)
	{
		const char *key = json_object_get_name(dst, i);
		JSON_Value *v1 = json_object_get_value(dst, key);
		const JSON_Value *v2 = json_object_get_value(src, key);
		if (!v2) continue;

		if (json_value_get_type(v1) == JSONObject && json_value_get_type(v2) == JSONObject)
		{
			json_interpolate(json_object_get_object(dst, key),
				json_object_get_object(src, key), t);
		}
		else if (json_value_get_type(v1) == JSONNumber && json_value_get_type(v2) == JSONNumber)
		{
			double a = json_value_get_number(v1);
			double b = json_value_get_number(v2);
			json_object_set_number(dst, key, a + t * (b - a));
		}
	}
}

// Build an interpolated snapshot between two surrounding keyframes.
// Returns NULL if interpolation is not possible (fallback to step).
static JSON_Value* keyframe_build_interpolated(int frame)
{
	int prevIdx = keyframe_find_effective(frame);
	if (prevIdx < 0) return NULL;

	int nextIdx = keyframe_find_next(frame);
	if (nextIdx < 0) return NULL;

	// Exactly on a keyframe — no interpolation needed
	if (gKeyframes[prevIdx].frame == frame) return NULL;

	double t = (double)(frame - gKeyframes[prevIdx].frame) /
		(double)(gKeyframes[nextIdx].frame - gKeyframes[prevIdx].frame);

	JSON_Object *rootA = json_value_get_object(gKeyframes[prevIdx].snapshot);
	JSON_Object *rootB = json_value_get_object(gKeyframes[nextIdx].snapshot);

	// Different devices — cannot interpolate, fall back to step
	int devA = (int)json_object_dotget_number(rootA, "Config.gDeviceId");
	int devB = (int)json_object_dotget_number(rootB, "Config.gDeviceId");
	if (devA != devB) return NULL;

	// Deep copy earlier keyframe as base
	JSON_Value *result = json_value_deep_copy(gKeyframes[prevIdx].snapshot);
	JSON_Object *root = json_value_get_object(result);

	// Interpolate modifier stack parameters
	for (int n = 0; n < 32; n++)
	{
		char path[256];
		sprintf(path, "Stack.Item[%d]", n);

		JSON_Object *itemA = json_object_dotget_object(rootA, path);
		if (!itemA) break;  // no more modifiers in earlier keyframe
		JSON_Object *itemB = json_object_dotget_object(rootB, path);
		if (!itemB) break;  // modifier doesn't exist in later keyframe — stop

		// Modifier types must match
		int typeA = (int)json_object_get_number(itemA, "Type");
		int typeB = (int)json_object_get_number(itemB, "Type");
		if (typeA != typeB) break;

		JSON_Object *itemDst = json_object_dotget_object(root, path);

		// Iterate numeric fields of this modifier, skip non-interpolatable ones
		size_t fieldCount = json_object_get_count(itemA);
		for (size_t i = 0; i < fieldCount; i++)
		{
			const char *key = json_object_get_name(itemA, i);
			JSON_Value *v1 = json_object_get_value(itemA, key);
			const JSON_Value *v2 = json_object_get_value(itemB, key);
			if (!v1 || !v2) continue;

			// Skip: Name, Type, mEnabled, *_en booleans
			if (strcmp(key, "Name") == 0 || strcmp(key, "Type") == 0 ||
				strcmp(key, "mEnabled") == 0 || strstr(key, "_en") != NULL)
				continue;

			if (json_value_get_type(v1) == JSONNumber && json_value_get_type(v2) == JSONNumber)
			{
				double a = json_value_get_number(v1);
				double b = json_value_get_number(v2);
				json_object_set_number(itemDst, key, a + t * (b - a));
			}
		}
	}

	return result;
}

// Cache for interpolated keyframe (avoid re-apply on same frame)
static int gLastInterpFrame = -1;

// Apply effective keyframe's snapshot to live state (Device + Stack)
static void keyframe_apply(int frame)
{
	if (gOptInterpolateKeys)
	{
		if (frame == gLastInterpFrame) return;
		gLastInterpFrame = frame;
		gLastAppliedKeyframeIdx = -1;  // invalidate step cache

		JSON_Value *interp = keyframe_build_interpolated(frame);
		if (interp)
		{
			gKeyframeSuspendCapture = true;
			JSON_Object *root = json_value_get_object(interp);
			deserialize_snapshot_from_json(root);
			gDirty = 1;
			gDirtyPic = 1;
			gKeyframeSuspendCapture = false;
			json_value_free(interp);
			return;
		}
		// Interpolation not possible — fall through to step behavior
	}

	gLastInterpFrame = -1;  // invalidate interp cache

	int idx = keyframe_find_effective(frame);
	if (idx == gLastAppliedKeyframeIdx) return;  // no change
	gLastAppliedKeyframeIdx = idx;

	gKeyframeSuspendCapture = true;

	if (idx >= 0 && gKeyframes[idx].snapshot)
	{
		JSON_Object *root = json_value_get_object(gKeyframes[idx].snapshot);
		deserialize_snapshot_from_json(root);
	}

	gDirty = 1;
	gDirtyPic = 1;
	gKeyframeSuspendCapture = false;
}

// Mark sidecar for debounced write: the actual disk write happens in the
// main loop ~500ms after the last change, not on every slider drag (2.3)
static void keyframe_mark_sidecar_dirty()
{
	gKeyframesSidecarDirty = true;
	gKeyframesSidecarLastChange = SDL_GetTicks();
}

// Create or update a keyframe at exact frame from current live state
static void keyframe_upsert(int frame)
{
	// On first keyframe creation, mark sidecar as loaded (enables saving)
	if (!gKeyframesLoaded)
		gKeyframesLoaded = true;

	// Find existing key at this exact frame
	for (int i = 0; i < gKeyframeCount; i++)
	{
		if (gKeyframes[i].frame == frame)
		{
			// Update existing
			if (gKeyframes[i].snapshot)
				json_value_free(gKeyframes[i].snapshot);
			build_applystack();
			gKeyframes[i].snapshot = json_value_init_object();
			serialize_snapshot_to_json(json_value_get_object(gKeyframes[i].snapshot));
			keyframe_mark_sidecar_dirty();
			return;
		}
	}
	// Insert new
	if (gKeyframeCount >= KEYFRAME_MAX) return;
	build_applystack();
	gKeyframes[gKeyframeCount].frame = frame;
	gKeyframes[gKeyframeCount].snapshot = json_value_init_object();
	serialize_snapshot_to_json(json_value_get_object(gKeyframes[gKeyframeCount].snapshot));
	gKeyframeCount++;
	keyframe_mark_sidecar_dirty();
}

// Delete keyframe at exact frame
static void keyframe_delete(int frame)
{
	for (int i = 0; i < gKeyframeCount; i++)
	{
		if (gKeyframes[i].frame == frame)
		{
			if (gKeyframes[i].snapshot)
				json_value_free(gKeyframes[i].snapshot);
			// Shift remaining
			for (int j = i; j < gKeyframeCount - 1; j++)
				gKeyframes[j] = gKeyframes[j + 1];
			gKeyframeCount--;
			gLastAppliedKeyframeIdx = -1;  // force re-evaluate
			keyframe_mark_sidecar_dirty();
			return;
		}
	}
}

// Save all keyframes to sidecar JSON
static void keyframe_save_sidecar()
{
	if (!gVideoMode || !gKeyframesLoaded) return;

	char path[MAX_PATH];
	keyframe_sidecar_path(path, sizeof(path));

	JSON_Value *root_value = json_value_init_object();
	JSON_Object *root = json_value_get_object(root_value);

	json_object_dotset_string(root, "Video.File", gVideoFilename);
	json_object_dotset_number(root, "Video.FpsNum", gVideoFpsNum);
	json_object_dotset_number(root, "Video.FpsDen", gVideoFpsDen);
	json_object_dotset_number(root, "Video.TotalFrames", gVideoTotalFrames);

	json_object_set_value(root, "Keys", json_value_init_array());
	JSON_Array *keysArr = json_object_get_array(root, "Keys");

	for (int i = 0; i < gKeyframeCount; i++)
	{
		JSON_Value *entryVal = json_value_init_object();
		json_array_append_value(keysArr, entryVal);
		JSON_Object *entry = json_value_get_object(entryVal);
		json_object_dotset_number(entry, "frame", gKeyframes[i].frame);

		if (gKeyframes[i].snapshot)
		{
			JSON_Object *snap = json_value_get_object(gKeyframes[i].snapshot);
			// Copy all fields from snapshot into entry (Device.*, Stack.*)
			size_t fieldCount = json_object_get_count(snap);
			for (size_t j = 0; j < fieldCount; j++)
			{
				const char *key = json_object_get_name(snap, j);
				JSON_Value *val = json_object_get_value(snap, key);
				json_object_set_value(entry, key, json_value_deep_copy(val));
			}
		}
	}

	json_serialize_to_file_pretty(root_value, path);
	json_value_free(root_value);

	// Verify write succeeded
	FILE *ftest = fopen(path, "r");
	if (ftest)
		fclose(ftest);
	else
		fprintf(stderr, "DIAG: keyframe_save_sidecar() FAILED to write '%s'\n", path);
}

// Write pending sidecar changes immediately (debounce flush, 2.3)
static void keyframe_flush_sidecar()
{
	if (gKeyframesSidecarDirty)
	{
		keyframe_save_sidecar();
		gKeyframesSidecarDirty = false;
	}
}

// Load keyframes from sidecar JSON
static void keyframe_load_sidecar()
{
	gKeyframeCount = 0;
	gKeyframesLoaded = false;
	gLastAppliedKeyframeIdx = -1;

	char path[MAX_PATH];
	keyframe_sidecar_path(path, sizeof(path));

	JSON_Value *root_value = json_parse_file(path);
	if (!root_value) return;  // no sidecar — fine

	JSON_Object *root = json_value_get_object(root_value);

	// Validate video matches
	const char *file = json_object_dotget_string(root, "Video.File");
	if (!file || _stricmp(file, gVideoFilename) != 0)
	{
		fprintf(stderr, "DIAG: keyframe_load_sidecar() video mismatch, ignoring\n");
		json_value_free(root_value);
		return;
	}

	gKeyframesLoaded = true;

	// Load keys
	JSON_Array *keysArr = json_object_get_array(root, "Keys");
	if (keysArr)
	{
		int count = json_array_get_count(keysArr);
		for (int i = 0; i < count && gKeyframeCount < KEYFRAME_MAX; i++)
		{
			JSON_Object *entry = json_array_get_object(keysArr, i);
			int frame = (int)json_object_dotget_number(entry, "frame");

			// Reconstruct full snapshot from nested fields
			// Build a temporary JSON value with Device + Stack
			JSON_Value *snap = json_value_init_object();
			JSON_Object *snapRoot = json_value_get_object(snap);

			// Copy Device.Name
			const char *devName = json_object_dotget_string(entry, "Device.Name");
			if (devName)
				json_object_dotset_string(snapRoot, "Device.Name", devName);

			// Copy all Device.* fields
			// Copy all Stack.Item[N] fields
			// This requires iterating the entry's keys — parson supports this
			// via json_object_get_count / json_object_get_name

			// Copy entire entry content to snapRoot
			size_t count2 = json_object_get_count(entry);
			for (size_t j = 0; j < count2; j++)
			{
				const char *key = json_object_get_name(entry, j);
				JSON_Value *val = json_object_get_value(entry, key);
				// Skip "frame" — already handled
				if (strcmp(key, "frame") == 0) continue;
				json_object_set_value(snapRoot, key, json_value_deep_copy(val));
			}

			gKeyframes[gKeyframeCount].frame = frame;
			gKeyframes[gKeyframeCount].snapshot = snap;
			gKeyframeCount++;
		}
	}

	fprintf(stderr, "DIAG: keyframe_load_sidecar() loaded %d keyframes from '%s'\n", gKeyframeCount, path);
	json_value_free(root_value);
}

// Clear all keyframes and free snapshots
static void keyframe_clear()
{
	for (int i = 0; i < gKeyframeCount; i++)
	{
		if (gKeyframes[i].snapshot)
			json_value_free(gKeyframes[i].snapshot);
		gKeyframes[i].snapshot = 0;
	}
	gKeyframeCount = 0;
	gKeyframesLoaded = false;
	gLastAppliedKeyframeIdx = -1;
	if (gKeyframeClipboard) { json_value_free(gKeyframeClipboard); gKeyframeClipboard = NULL; }
}

// --- End video keyframes ---
