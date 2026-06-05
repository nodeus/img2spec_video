/*
 *	Copyright (c) 2015-2016 Jari Komppa, http://iki.fi/sol
 *
 *	This software is provided 'as-is', without any express or implied
 *	warranty. In no event will the authors be held liable for any damages
 *	arising from the use of this software.
 *
 *	Permission is granted to anyone to use this software for any purpose,
 *	including commercial applications, and to alter it and redistribute it
 *	freely, subject to the following restrictions:
 *
 *	1. The origin of this software must not be misrepresented; you must not
 *	claim that you wrote the original software. If you use this software
 *	in a product, an acknowledgement in the product documentation would be
 *	appreciated but is not required.
 *	2. Altered source versions must be plainly marked as such, and must not be
 *	misrepresented as being the original software.
 *	3. This notice may not be removed or altered from any source distribution.
 */

/*
Note that this is (largely) a quick hack, so the
code quality leaves a lot to be desired..
Still, if you find it useful, great!
*/

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#endif

#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#include "platform/common.h"

#include "imgui.h"
#include "imgui_impl_sdl.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <algorithm>
#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_opengl.h>

#include "parson/parson.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

#define VERSION "5.0"

#define SERIALIZE(x) json_object_dotset_number(root, #x, x);
#define DESERIALIZE(x) if (json_object_dotget_value(root, #x) != NULL) x = json_object_dotget_number(root, #x);


char *gSourceImageName = 0;
unsigned int *gSourceImageData = 0;
int gSourceImageX = 0, gSourceImageY = 0;
int gSourceImageDate = 0;

int float_to_color(float aR, float aG, float aB);
void bitmap_to_float(unsigned int *aBitmap);
void update_texture(GLuint aTexture, unsigned int *aBitmap);

// Do we need to recalculate?
int gDirty = 1;
// Do we need to rescale etc?
int gDirtyPic = 0;


// used to avoid id collisions in ImGui
int gUniqueValueCounter = 0;

bool gWindowZoomedOutput = false;
bool gWindowZoomedInput = false;
bool gWindowZoomedModified = false;
bool gWindowAttribBitmap = false;
bool gWindowHistograms = false;
bool gWindowOptions = false;
bool gWindowModifierPalette = false;
bool gWindowAbout = false;
bool gWindowHelp = false;
bool gOptShowOriginal = true;
bool gOptShowModified = true;
bool gOptShowResult = true;
bool gOptImagesDocked = true;
int gOptTopmost = 0;
int gOptZoom = 2;
int gOptZoomStyle = 0;
int gOptTrackFile = 1;
int gDeviceId = 0;
char gStartupCwd[MAX_PATH] = "";

// Видео-режим
bool gVideoMode = false;
char gVideoFilename[1024] = "";
double gVideoDuration = 0.0;
double gVideoFps = 25.0;
int gVideoFpsNum = 25000;
int gVideoFpsDen = 1000;
int gVideoTotalFrames = 0;
int gVideoCurrentFrame = 0;
int gVideoWidth = 0;
int gVideoHeight = 0;
bool gWindowExport = false;
bool gVideoExportActive = false;
float gVideoExportProgress = 0.0f;
int gOptExportScale = 8;
int gOptExportEncoder = 0;
int gOptExportQuality = 17;
char gOptExportFilename[1024] = "";
char gOptExportExtraParams[1024] = "";

int gPipeWidth = 0;   // --width for --pipe mode
int gPipeHeight = 0;  // --height for --pipe mode

// Texture handles
GLuint gTextureOrig, gTextureProc, gTextureSpec, gTextureAttr, gTextureAttr2, gTextureBitm; 

// Bitmaps for the textures
unsigned int gBitmapOrig[1024 * 512];
unsigned int gBitmapProc[1024 * 512];
unsigned int gBitmapSpec[1024 * 512];
unsigned int gBitmapAttr[1024 * 512];
unsigned int gBitmapAttr2[1024 * 512];
unsigned int gBitmapBitm[1024 * 512];

// Floating point version of the processed bitmap, for processing
float gBitmapProcFloat[1024 * 512 * 3];

// Histogram arrays
float gHistogramR[256];
float gHistogramG[256];
float gHistogramB[256];

class Modifier;
// Modifier stack
Modifier *gModifierRoot = 0;
// Modifiers are applied in reverse order
Modifier *gModifierApplyStack = 0;

enum MODIFIERS
{
	MOD_SCALEPOS = 1,
	MOD_RGB,
	MOD_YIQ,
	MOD_HSV,
	MOD_NOISE,
	MOD_ORDEREDDITHER,
	MOD_ERRORDIFFUSION,
	MOD_CONTRAST,
	MOD_BLUR,
	MOD_EDGE,
	MOD_MINMAX,
	MOD_QUANTIZE,
	MOD_SUPERBLACK,
	MOD_CURVE
};

#include "device.h"
#include "modifier.h"

Device *gDevice = 0;

#include "zxspectrumdevice.h"
#include "zx3x64device.h"
#include "zxhalftiledevice.h"
#include "c64hiresdevice.h"
#include "c64multicolordevice.h"

#include "scaleposmodifier.h"
#include "rgbmodifier.h"
#include "yiqmodifier.h"
#include "hsvmodifier.h"
#include "noisemodifier.h"
#include "blurmodifier.h"
#include "edgemodifier.h"
#include "quantizemodifier.h"
#include "minmaxmodifier.h"
#include "ordereddithermodifier.h"
#include "errordiffusiondithermodifier.h"
#include "contrastmodifier.h"
#include "superblackmodifier.h"
#include "curvemodifier.h"

void update_texture(GLuint aTexture, unsigned int *aBitmap)
{
	glBindTexture(GL_TEXTURE_2D, aTexture);
	//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)aBitmap);
	glTexSubImage2D(
		GL_TEXTURE_2D,			// target
		0,						// level
		0,						// xofs
		0,						// yofs
		gDevice->mXRes,			// width
		gDevice->mYRes,			// height
		GL_RGBA,				// format
		GL_UNSIGNED_BYTE,		// type
		(GLvoid*)aBitmap);		// data
}

void bitmap_to_float(unsigned int *aBitmap)
{
	int i;
	for (i = 0; i < gDevice->mXRes * gDevice->mYRes; i++)
	{
		int c = aBitmap[i];
		int r = (c >> 16) & 0xff;
		int g = (c >> 8) & 0xff;
		int b = (c >> 0) & 0xff;

		gBitmapProcFloat[i * 3 + 0] = r * (1.0f / 255.0f);
		gBitmapProcFloat[i * 3 + 1] = g * (1.0f / 255.0f);
		gBitmapProcFloat[i * 3 + 2] = b * (1.0f / 255.0f);
	}
}

int float_to_color(float aR, float aG, float aB)
{
	aR = (aR < 0) ? 0 : (aR > 1) ? 1 : aR;
	aG = (aG < 0) ? 0 : (aG > 1) ? 1 : aG;
	aB = (aB < 0) ? 0 : (aB > 1) ? 1 : aB;

	return ((int)floor(aR * 255) << 16) |
		   ((int)floor(aG * 255) << 8) |
		   ((int)floor(aB * 255) << 0);
}

void float_to_bitmap()
{
	int i;
	for (i = 0; i < gDevice->mXRes * gDevice->mYRes; i++)
	{
		float r = gBitmapProcFloat[i * 3 + 0];
		float g = gBitmapProcFloat[i * 3 + 1];
		float b = gBitmapProcFloat[i * 3 + 2];

		gBitmapProc[i] = float_to_color(r, g, b) | 0xff000000;
	}
}

void modifier_ui()
{
	Modifier *walker = gModifierRoot;
	Modifier *prev = 0;

	while (walker)
	{
		int ret = walker->ui();
		if (ret == 1) // move down
		{
			if (walker->mNext)
			{
				Modifier *curr = walker;
				Modifier *next = walker->mNext;
				/*
				prev->curr->next->..
				*/
				curr->mNext = next->mNext;
				next->mNext = curr;
				if (prev == 0)
				{
					gModifierRoot = next;
				}
				else
				{
					prev->mNext = next;
				}
			}
			else
			{
				prev = walker;
				walker = walker->mNext;
			}
		}
		else
			if (ret == -1) // delete
			{
				Modifier *t = walker;
				if (prev == 0)
				{
					gModifierRoot = walker->mNext;
				}
				else
				{
					prev->mNext = walker->mNext;
				}
				walker = walker->mNext;
				delete t;
			}
			else // normal op
			{
				prev = walker;
				walker = walker->mNext;
			}
	}
}

void build_applystack()
{
	gModifierApplyStack = gModifierRoot;
	if (gModifierApplyStack)
	{
		gModifierApplyStack->mApplyNext = 0;

		Modifier *walker = gModifierRoot->mNext;
		while (walker)
		{
			walker->mApplyNext = gModifierApplyStack;
			gModifierApplyStack = walker;
			walker = walker->mNext;
		}
	}
}

void process_image()
{
	build_applystack();

	bitmap_to_float(gBitmapOrig);

	// ScalePos must be applied FIRST: it resamples from gSourceImageData into
	// gBitmapOrig and calls bitmap_to_float, resetting all float data.
	// All other modifiers work on that resampled base.
	Modifier *walker = gModifierApplyStack;
	while (walker)
	{
		if (walker->mEnabled && walker->gettype() == MOD_SCALEPOS)
		{
			walker->process();
			break;
		}
		walker = walker->mApplyNext;
	}

	// All other modifiers
	walker = gModifierApplyStack;
	while (walker)
	{
		if (walker->mEnabled && walker->gettype() != MOD_SCALEPOS)
			walker->process();
		walker = walker->mApplyNext;
	}

	float_to_bitmap();
}


int mix(int a, int b)
{
	int red = (((a >> 0) & 0xff) + ((b >> 0) & 0xff)) / 2;
	int green = (((a >> 8) & 0xff) + ((b >> 8) & 0xff)) / 2;
	int blue = (((a >> 16) & 0xff) + ((b >> 16) & 0xff)) / 2;
	return ((red << 0) | (green << 8) | (blue << 16));
}


void calc_histogram(unsigned int *src)
{
	int i;
	for (i = 0; i < 256; i++)
	{
		gHistogramR[i] = 0;
		gHistogramG[i] = 0;
		gHistogramB[i] = 0;
	}
	for (i = 0; i < gDevice->mYRes * gDevice->mXRes; i++)
	{
		gHistogramR[(src[i] >> 0) & 0xff]++;
		gHistogramG[(src[i] >> 8) & 0xff]++;
		gHistogramB[(src[i] >> 16) & 0xff]++;
	}
}

// Should probably add to the end of the list instead of beginning..
void addModifier(Modifier *aNewModifier)
{
	aNewModifier->mNext = gModifierRoot;
	gModifierRoot = aNewModifier;
	gDirty = 1;
}

void loadworkspace(char *aFilename = nullptr)
{
	gDirty = 1;

    const char *FileName;

    if(aFilename)
        FileName = aFilename;
    else if ((FileName = openDialog("Load workspace",
                                    "Image Spectrumizer Workspace (*.isw)\0*.isw\0"
                                            "All Files (" ALL_FILES ")\0" ALL_FILES "\0\0")))
        ;

	if (FileName)
	{
		fprintf(stderr, "DIAG: loadworkspace() loading '%s'\n", FileName);

		JSON_Value *root_value = json_parse_file(FileName);
		if (root_value)
		{
			JSON_Object *root = json_value_get_object(root_value);
			fprintf(stderr, "DIAG: loadworkspace() JSON OK, magic=%s ver=%.0f\n",
				json_object_dotget_string(root, "About.Magic") ? json_object_dotget_string(root, "About.Magic") : "NULL",
				json_object_dotget_number(root, "About.Version"));
			if (_stricmp(json_object_dotget_string(root, "About.Magic"), "0x50534D49") == 0 &&
				json_object_dotget_number(root, "About.Version") == 4)
			{
#define READCONFIG(x) if (json_object_dotget_value(root, "Config." #x) != NULL) x = json_object_dotget_number(root, "Config." #x);
#pragma warning(disable:4244; disable:4800)
				READCONFIG(gWindowAbout);
				READCONFIG(gWindowAttribBitmap);
				READCONFIG(gWindowHelp);
				READCONFIG(gWindowHistograms);
				READCONFIG(gWindowModifierPalette);
				READCONFIG(gWindowOptions);
				READCONFIG(gWindowZoomedOutput);
				READCONFIG(gWindowZoomedModified);
				READCONFIG(gWindowZoomedInput);
				READCONFIG(gOptShowOriginal);
				READCONFIG(gOptShowModified);
				READCONFIG(gOptShowResult);
				READCONFIG(gOptImagesDocked);
				READCONFIG(gOptTrackFile);
				READCONFIG(gOptTopmost);
				READCONFIG(gDeviceId);
#pragma warning(default:4244; default:4800)
#undef READCONFIG
				delete gDevice;
				gDevice = 0;
				switch (gDeviceId)
				{
				case 0:
					gDevice = new ZXSpectrumDevice;
					break;
				case 1:
					gDevice = new ZX3x64Device;
					break;
				case 2:
					gDevice = new ZXHalfTileDevice;
					break;
				case 3:
					gDevice = new C64HiresDevice;
					break;
				case 4:
					gDevice = new C64MulticolorDevice;
					break;
				}
				gDevice->readOptions(root);

				Modifier *walker = gModifierRoot;
				while (walker)
				{
					Modifier *last = walker;
					walker = walker->mNext;
					delete last;
				}
				gModifierRoot = 0;

				int number = 0;
				char path[256];
				sprintf(path, "Stack.Item[%d]", number);
				JSON_Object *item = json_object_dotget_object(root, path);

				while (item)
				{
					
					int m;
					if (json_object_get_value(item, "Type"))
					{
						m = (int)json_object_get_number(item, "Type");
						fprintf(stderr, "DIAG: loadworkspace() modifier[%d] type=%d\n", number, m);
						
						Modifier *n = 0;
						switch (m)
						{
						case MOD_SCALEPOS: n = new ScalePosModifier; break;
						case MOD_RGB: n = new RGBModifier; break;
						case MOD_YIQ: n = new YIQModifier; break;
						case MOD_HSV: n = new HSVModifier; break;
						case MOD_NOISE: n = new NoiseModifier; break;
						case MOD_ORDEREDDITHER: n = new OrderedDitherModifier; break;
						case MOD_ERRORDIFFUSION: n = new ErrorDiffusionDitherModifier; break;
						case MOD_CONTRAST: n = new ContrastModifier; break;
						case MOD_BLUR: n = new BlurModifier; break;
						case MOD_EDGE: n = new EdgeModifier; break;
						case MOD_MINMAX: n = new MinmaxModifier; break;
						case MOD_QUANTIZE: n = new QuantizeModifier; break;
						case MOD_SUPERBLACK: n = new SuperblackModifier; break;
						case MOD_CURVE: n = new CurveModifier; break;
						default:
							fprintf(stderr, "DIAG: loadworkspace() unknown modifier type %d\n", m);
							json_value_free(root_value);
							return;
						}
						addModifier(n);
						n->deserialize_common(item);
						n->deserialize(item);
					}
					number++;
					sprintf(path, "Stack.Item[%d]", number);
					item = json_object_dotget_object(root, path);
				}
				fprintf(stderr, "DIAG: loadworkspace() loaded %d modifiers, deviceId=%d\n", number, gDeviceId);
			}
			else
			{
				fprintf(stderr, "DIAG: loadworkspace() magic/version check failed\n");
			}
			json_value_free(root_value);
		}
		else
		{
			fprintf(stderr, "DIAG: loadworkspace() json_parse_file failed\n");
		}
	}
	else
	{
		fprintf(stderr, "DIAG: loadworkspace() FileName is NULL\n");
	}
}


void saveworkspace()
{
    const char *FileName;

	if ((FileName = saveDialog("Save workspace",
                               "Image Spectrumizer Workspace (*.isw)\0*.isw\0"
                                       "All Files (" ALL_FILES ")\0" ALL_FILES "\0\0",
                               "isw")))
	{
		JSON_Value *root_value = json_value_init_object();
		JSON_Object *root = json_value_get_object(root_value);
		json_object_dotset_string(root, "About.WhatIsThis", "Image Spectrumizer " VERSION " workspace file"); 		
		json_object_dotset_string(root, "About.Magic", "0x50534D49");
		json_object_dotset_number(root, "About.Version", 4);
			
#define WRITECONFIG(x) json_object_dotset_number(root, "Config." #x, x);
		WRITECONFIG(gWindowAbout);
		WRITECONFIG(gWindowAttribBitmap);
		WRITECONFIG(gWindowHelp);
		WRITECONFIG(gWindowHistograms);
		WRITECONFIG(gWindowModifierPalette);
		WRITECONFIG(gWindowOptions);
		WRITECONFIG(gWindowZoomedOutput);
		WRITECONFIG(gWindowZoomedModified);
		WRITECONFIG(gWindowZoomedInput);
		WRITECONFIG(gOptShowOriginal);
		WRITECONFIG(gOptShowModified);
		WRITECONFIG(gOptShowResult);
		WRITECONFIG(gOptImagesDocked);
		WRITECONFIG(gOptTrackFile);
		WRITECONFIG(gOptTopmost);
		WRITECONFIG(gDeviceId);
		json_object_dotset_string(root, "Device.Name", gDevice->getname());
#undef WRITECONFIG
		gDevice->writeOptions(root);

		Modifier *walker = gModifierApplyStack;		
		int number = 0;
		while (walker)
		{
			char path[256];
			sprintf(path, "Stack.Item[%d]", number);
			char temp[256];
			sprintf(temp, "%s.Name", path);
			json_object_dotset_string(root, temp, walker->getname());
			sprintf(temp, "%s.Type", path);
			json_object_dotset_number(root, temp, walker->gettype());
			JSON_Object *item = json_object_dotget_object(root, path);
			walker->serialize_common(item);
			walker->serialize(item);
			walker = walker->mApplyNext;
			number++;
		}


		json_serialize_to_file_pretty(root_value, FileName);
		json_value_free(root_value);

	}
	gDirty = 1;
	gDirtyPic = 1;
}

char * mystrdup(char * aString)
{
	int len = 0;
	while (aString[len]) len++;
	char * data = new char[len + 1];
	memcpy(data, aString, len);
	data[len] = 0;
	return data;
}

unsigned int *loadscr(char *aFilename)
{
	unsigned int *data = (unsigned int *)stbi__malloc((192 * 256) * 4);
	unsigned char *t = new unsigned char[32 * 192 + 32 * 24];
	FILE * f = fopen(aFilename, "rb");
	fread(t, 1, 32*192+32*24, f);
	fclose(f);
	int i, j, c;
	for (i = 0, c = 0; i < 192; i++)
	{
		for (j = 0; j < 256; j++, c++)
		{
			int d = (t[SPEC_Y(i)*32 + (j / 8)] << (j & 7)) & 0x80;
			unsigned char a = t[32 * 192 + (i / 8) * 32 + j / 8];
			int idx = a;
			if (!d) idx >>= 3;
			idx &= 7;
			if (a & 0x40)
				idx |= 8;

			data[c] = gSpeccyPalette[idx] | 0xff000000;
		}
	}
	delete[] t;

	gDirty = 1;
	gDirtyPic = 1;

	return data;
}

void loadimg(char *aFilename = nullptr)
{
    const char *FileName;

    if(aFilename)
        FileName = aFilename;
    else if ((FileName = openDialog("Load image",
                                    "All supported types\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.psd;*.gif;*.hdr;*.pic;*.pnm;*.scr\0"
                                            "PNG (*.png)\0*.png\0"
                                            "JPG (*.jpg)\0*.jpg;*.jpeg\0"
                                            "TGA (*.tga)\0*.tga\0"
                                            "BMP (*.bmp)\0*.bmp\0"
                                            "PSD (*.psd)\0*.psd\0"
                                            "GIF (*.gif)\0*.gif\0"
                                            "HDR (*.hdr)\0*.hdr\0"
                                            "PIC (*.pic)\0*.pic\0"
                                            "PNM (*.pnm)\0*.pnm\0"
                                            "SCR (*.scr)\0*.scr\0"
                                            "All Files (" ALL_FILES ")\0" ALL_FILES "\0\0")))
        ;

	if (FileName)
	{
		FILE *f = fopen(FileName, "rb");
		if (!f)
			return;
		fseek(f, 0, SEEK_END);
		int len = ftell(f);
		fclose(f);

		// gSourceImageName may be the same pointer as aFilename, so freeing it first
		// and then trying to duplicate it to itself might... well..
		char *name = mystrdup((char *)FileName);
		int n, iters = 0, x, y;
		unsigned int *data = 0;
		while (data == 0 && iters < 16)
		{
			data = (unsigned int*)stbi_load(name, &x, &y, &n, 4);
			if (data == 0)
				SDL_Delay(20);
			iters++;
		}

		if (data == 0)
		{
			if (len == (32 * 192 + 32 * 24))
			{
				data = loadscr(name);
				x = 256;
				y = 192;
			}
			else
			{
				return;
			}
		}

		gSourceImageX = x;
		gSourceImageY = y;

		if (gSourceImageName)
			delete[] gSourceImageName;
		gSourceImageName = name;
		if (gSourceImageData)
			stbi_image_free(gSourceImageData);
		gSourceImageData = data;


		int i, j;
		for (i = 0; i < gDevice->mYRes; i++)
		{
			for (j = 0; j < gDevice->mXRes; j++)
			{
				int pix = 0xff000000;
				if (j < gSourceImageX && i < gSourceImageY)
					pix = gSourceImageData[i * gSourceImageX + j] | 0xff000000;
				gBitmapOrig[i * gDevice->mXRes + j] = pix;
			}
		}

		update_texture(gTextureOrig, gBitmapOrig);
		
		gSourceImageDate = getFileDate(gSourceImageName);
	}
	gDirty = 1;
	gDirtyPic = 1;
}

void generateimg()
{
	int x, y;
	float re0 = -0.7f;
	float im0 = 0.27f;

	for (x = 0; x < gDevice->mXRes; x++)
	{
		for (y = 0; y < gDevice->mYRes; y++)
		{
			float re2 = 1.5f * (x - gDevice->mXRes / 2) / (0.5f * gDevice->mXRes);
			float im2 = (y - gDevice->mYRes / 2) / (0.5f * gDevice->mYRes);
			int iter = 0;
			while (iter < 100)
			{
				iter++;

				float re1 = re2;
				float im1 = im2;

				re2 = re1 * re1 - im1 * im1 + re0;
				im2 = 2 * re1 * im1 + im0;

				if ((re2 * re2 + im2 * im2) > 4) break;
			}
			gBitmapOrig[y * gDevice->mXRes + x] = 0xff000000 | ((iter == 100) ? 0 : ((int)(sin(iter * 0.179) * 120 + 120) << 16) | ((int)(sin(iter * 0.13) * 120 + 120) << 8) | ((int)(sin(iter * 0.1) * 120 + 120) << 0));
		}
	}

	update_texture(gTextureOrig, gBitmapOrig);
	gDirty = 1;
	gDirtyPic = 1;

}


void savepng(char *aFilename = nullptr)
{
    const char *FileName;

    if(aFilename)
        FileName = aFilename;
    else if ((FileName = saveDialog("Save png",
                        "PNG (*.png)\0*.png\0"
                        "All Files (" ALL_FILES ")\0" ALL_FILES "\0\0",
                        "png")))
        ;

	if (FileName)
	{
		stbi_write_png(FileName, gDevice->mXRes, gDevice->mYRes, 4, gBitmapSpec, gDevice->mXRes * 4);
	}
}


void savescr(char *aFilename = nullptr)
{
    const char *FileName;

    if(aFilename)
        FileName = aFilename;
    else if ((FileName = saveDialog("Save scr",
                                    "scr (*.scr)\0*.scr\0"
                                    "All Files (" ALL_FILES ")\0" ALL_FILES "\0\0",
                                    "scr")))
        ;

    if (FileName)
    {
        FILE * f = fopen(FileName, "wb");
        gDevice->savescr(f);
        fclose(f);
    }
}

void saveh(char *aFilename = 0)
{
    const char *FileName;

    if(aFilename)
        FileName = aFilename;
    else if ((FileName = saveDialog("Save h",
                                    "C header (*.h)\0*.h\0"
                                            "All Files (" ALL_FILES ")\0" ALL_FILES "\0\0",
                                    "h")))
        ;

    if (FileName)
    {
        FILE * f = fopen(FileName, "w");
        gDevice->saveh(f);
        fclose(f);
    }
}


void saveinc(char *aFilename = 0)
{
    const char *FileName;

    if(aFilename)
        FileName = aFilename;
    else if ((FileName = saveDialog("Save inc",
                                    "inc (*.inc)\0*.inc\0"
                                            "All Files (" ALL_FILES ")\0" ALL_FILES "\0\0",
                                    "inc")))
        ;

    if (FileName)
    {
        FILE * f = fopen(FileName, "w");
        gDevice->saveinc(f);
        fclose(f);
    }
}


/*

void calccrap()
{
	FILE * f = fopen("test.h", "w");
	int i, j;
	for (i = 0; i < 8; i++)
	{
		for (j = 0; j < 8; j++)
		{
			fprintf(f, "0x%06x,\n", mix(gSpeccyPalette[i], gSpeccyPalette[j]));
		}
	}
	fprintf(f, "\n");
	for (i = 0; i < 8; i++)
	{
		for (j = 0; j < 8; j++)
		{
			fprintf(f, "0x%06x,\n", mix(gSpeccyPalette[i], gSpeccyPalette[j + 8]));
		}
	}
	fprintf(f, "\n");
	for (i = 0; i < 8; i++)
	{
		for (j = 0; j < 8; j++)
		{
			fprintf(f, "0x%06x,\n", mix(gSpeccyPalette[i + 8], gSpeccyPalette[j + 8]));
		}
	}
	fclose(f);
	exit(0);
}

void measurecrap()
{
	int x, y, n;
	unsigned char *data = stbi_load("grid.png", &x, &y, &n, 4);
	FILE * f = fopen("test.h", "w");
	int i, j, k, l;
	for (i = 0; i < 12; i++)
	{
		for (j = 0; j < 16; j++)
		{
			float r = 0, g = 0, b = 0;
			for (k = 1; k < y/12-2; k++)
			{
				for (l = 1; l < x/16-2; l++)
				{
					r += (float)data[((i * (y / 12) + k)*x + j*(x / 16) + l) * 4 + 2] / (((y / 12)-2) * ((x / 16)-2));
					g += (float)data[((i * (y / 12) + k)*x + j*(x / 16) + l) * 4 + 1] / (((y / 12)-2) * ((x / 16)-2));
					b += (float)data[((i * (y / 12) + k)*x + j*(x / 16) + l) * 4 + 0] / (((y / 12)-2) * ((x / 16)-2));
				}
			}
			int color = ((int)floor(r * 1) << 16) | ((int)floor(g * 1) << 8) | ((int)floor(b * 1) << 0);
			fprintf(f, "0x%06x, // %d\n", color, i * 16 + j);
		}
	}
	fclose(f);
	exit(0);
}
*/

char *run_pipe(const char *cmd)
{
#ifdef _WIN32
	FILE *f = _popen(cmd, "rt");
#else
	FILE *f = popen(cmd, "r");
#endif
	if (!f) return 0;
	static char buf[4096];
	buf[0] = 0;
	fgets(buf, sizeof(buf), f);
#ifdef _WIN32
	_pclose(f);
#else
	pclose(f);
#endif
	if (buf[0] == 0) return 0;
	// trim newline
	size_t len = strlen(buf);
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		buf[--len] = 0;
	return buf;
}

void get_video_frame(int frameNum)
{
	gDirty = 1;
	gDirtyPic = 1;

	if (gVideoWidth == 0 || gVideoHeight == 0) return;

	double sec = (double)frameNum / gVideoFps;
	int vw = gVideoWidth;
	int vh = gVideoHeight;

	char cmd[4096];
#ifdef _WIN32
	sprintf(cmd,
		"ffmpeg -ss %.3f -i \"%s\" -vframes 1 -f rawvideo -pix_fmt rgb24 "
		"-s %dx%d -v quiet -",
		sec, gVideoFilename, vw, vh);
	FILE *pipe = _popen(cmd, "rb");
#else
	sprintf(cmd,
		"ffmpeg -ss %.3f -i \"%s\" -vframes 1 -f rawvideo -pix_fmt rgb24 "
		"-s %dx%d -v quiet -",
		sec, gVideoFilename, vw, vh);
	FILE *pipe = popen(cmd, "r");
#endif
	if (!pipe) return;

	unsigned char *buf = new unsigned char[vw * vh * 3];
	size_t read = fread(buf, 1, vw * vh * 3, pipe);
#ifdef _WIN32
	_pclose(pipe);
#else
	pclose(pipe);
#endif

	if (read != (size_t)(vw * vh * 3))
	{
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
	gVideoCurrentFrame = frameNum;

	// Update Original texture so the window shows the current scrubbed frame
	update_texture(gTextureOrig, gBitmapOrig);
}

void load_video(const char *filename)
{
	if (!filename) return;

	char cmd[4096];
	const char *res;

	// ffprobe: resolution
	sprintf(cmd, "ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 \"%s\"", filename);
	res = run_pipe(cmd);
	if (!res) { printf("ffprobe error: can't get video info\n"); return; }
	if (sscanf(res, "%d,%d", &gVideoWidth, &gVideoHeight) != 2) return;

	// ffprobe: duration
	sprintf(cmd, "ffprobe -v error -show_entries format=duration -of csv=p=0 \"%s\"", filename);
	res = run_pipe(cmd);
	if (!res) return;
	gVideoDuration = atof(res);

	// ffprobe: frame rate
	sprintf(cmd, "ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 \"%s\"", filename);
	res = run_pipe(cmd);
	if (!res) return;
	// r_frame_rate is "num/den" or "num"
	if (strchr(res, '/'))
	{
		int num = 0, den = 1;
		sscanf(res, "%d/%d", &num, &den);
		gVideoFps = (den > 0) ? (double)num / den : 25.0;
		gVideoFpsNum = (den > 0) ? num : 25000;
		gVideoFpsDen = (den > 0) ? den : 1000;
	}
	else
	{
		gVideoFps = atof(res);
		gVideoFpsNum = (int)(gVideoFps * 1000 + 0.5);
		gVideoFpsDen = 1000;
	}
	if (gVideoFps <= 0) { gVideoFps = 25.0; gVideoFpsNum = 25000; gVideoFpsDen = 1000; }

	gVideoTotalFrames = (int)(gVideoDuration * gVideoFps + 0.5);
	gVideoCurrentFrame = 0;
	gVideoMode = true;
	strcpy(gVideoFilename, filename);

	// Загружаем первый кадр
	get_video_frame(0);
}

int pipe_mode = 0;

#ifdef _WIN32
#include <windows.h>
static PROCESS_INFORMATION gExportProc;
static HANDLE gExportStderrRead = NULL;
static int gExportRunning = 0;
#endif

void start_video_export()
{
	if (gOptExportFilename[0] == 0)
	{
		const char *base = strrchr(gVideoFilename, '\\');
		if (!base) base = strrchr(gVideoFilename, '/');
		if (base) base++; else base = gVideoFilename;
		strcpy(gOptExportFilename, base);
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

	build_applystack();
	JSON_Value *root_value = json_value_init_object();
	JSON_Object *root = json_value_get_object(root_value);
	json_object_dotset_string(root, "About.WhatIsThis", "Image Spectrumizer " VERSION " workspace file");
	json_object_dotset_string(root, "About.Magic", "0x50534D49");
	json_object_dotset_number(root, "About.Version", 4);

#define WRITECONFIG(x) json_object_dotset_number(root, "Config." #x, x);
	WRITECONFIG(gDeviceId);
#undef WRITECONFIG
	json_object_dotset_string(root, "Device.Name", gDevice->getname());
	gDevice->writeOptions(root);

	Modifier *walker = gModifierApplyStack;
	int number = 0;
	while (walker)
	{
		char path[256], temp[256];
		sprintf(path, "Stack.Item[%d]", number);
		sprintf(temp, "%s.Name", path);
		json_object_dotset_string(root, temp, walker->getname());
		sprintf(temp, "%s.Type", path);
		json_object_dotset_number(root, temp, walker->gettype());
		JSON_Object *item = json_object_dotget_object(root, path);
		walker->serialize_common(item);
		walker->serialize(item);
		walker = walker->mApplyNext;
		number++;
	}
	fprintf(stderr, "DIAG: start_video_export() workspace=%s modifiers=%d\n", workspacePath, number);
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

	char cmd[16384];
	_snprintf(cmd, sizeof(cmd) - 1,
		"ffmpeg -loglevel info -fflags nobuffer -i \"%s\" "
		"-f rawvideo -pix_fmt rgb24 - | "
		"\"%s\" \"%s\" --pipe --width %d --height %d | "
		"ffmpeg -loglevel info -y -sws_flags neighbor -f rawvideo -pix_fmt rgba -s %dx%d -r %s -i - "
		"-vf \"scale=iw*%d:-1:flags=neighbor\" ",
		gVideoFilename,
		exePath, workspacePath,
		gVideoWidth, gVideoHeight,
		gDevice->mXRes, gDevice->mYRes,
		fpsStr,
		gOptExportScale);

	// User extra params
	if (gOptExportExtraParams[0])
		sprintf(cmd + strlen(cmd), "%s ", gOptExportExtraParams);

	// Encoder-specific args
	switch (gOptExportEncoder)
	{
		case 0: // NVIDIA NVENC
		sprintf(cmd + strlen(cmd),
			"-c:v hevc_nvenc -profile:v main -pix_fmt yuv420p "
			"-preset fast -movflags +faststart -rc constqp -qp %d \"%s\"",
			gOptExportQuality, exportAbsPath);
		break;
	case 1: // AMD AMF
		sprintf(cmd + strlen(cmd),
			"-c:v hevc_amf -rc cqp -qp_p %d -qp_i %d -pix_fmt yuv420p \"%s\"",
			gOptExportQuality, gOptExportQuality, exportAbsPath);
		break;
	default: // CPU x264
		sprintf(cmd + strlen(cmd),
			"-c:v libx264 -crf %d -pix_fmt yuv420p \"%s\"",
			gOptExportQuality, exportAbsPath);
		break;
	}

	// Write batch file (needed for cmd.exe pipeline with |)
	// Use group redirect 2>>"log" (... ) to capture ALL stderr (cmd.exe + pipe processes)
	char logPath[MAX_PATH + 32];
	_snprintf(logPath, MAX_PATH + 32, "%s\\img2spec_export_stderr.log", tempDir);

	char batchPath[MAX_PATH];
	_snprintf(batchPath, MAX_PATH, "%s\\img2spec_export.bat", tempDir);

	FILE *f = fopen(batchPath, "w");
	fprintf(f, "@echo off\n");
	fprintf(f, "echo [%%DATE%% %%TIME%%] Before pipe > \"%s\"\n", logPath);
	fprintf(f, "2>>\"%s\" (\n", logPath);
	fprintf(f, "  %s\n", cmd);
	fprintf(f, ")\n");
	fprintf(f, "echo [%%DATE%% %%TIME%%] Exit=%%ERRORLEVEL%% >> \"%s\"\n", logPath);
	fclose(f);

	// Run batch file via cmd.exe (CREATE_NO_WINDOW = no console window)
	char cmdline[MAX_PATH + 32];
	sprintf(cmdline, "cmd.exe /c \"%s\"", batchPath);

	STARTUPINFOA si = {0};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {0};

	gExportRunning = 1;
	gVideoExportProgress = 0.0f;
	gVideoExportActive = true;

	if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
		CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
	{
		gExportRunning = 0;
		gVideoExportProgress = 0.0f;
		gVideoExportActive = false;
		printf("Export: CreateProcess failed (error %d)\n", GetLastError());
	}
	else
	{
		gExportProc = pi;
	}
#endif
}

void poll_video_export()
{
#ifdef _WIN32
	if (!gExportRunning) return;

	DWORD exitCode;
	if (GetExitCodeProcess(gExportProc.hProcess, &exitCode) && exitCode == STILL_ACTIVE)
	{
		// Still running — update progress estimate based on frame position
		// (no accurate progress without parsing ffmpeg stderr)
		gVideoExportProgress = (float)gVideoCurrentFrame / (float)gVideoTotalFrames;
	}
	else
	{
		// Encoding process done — close handles
		gExportRunning = 0;
		CloseHandle(gExportProc.hProcess);
		CloseHandle(gExportProc.hThread);

		// Check if source video has an audio stream
		char probeCmd[4096];
		char probeResult[64] = "";
		_snprintf(probeCmd, sizeof(probeCmd),
			"ffprobe -v error -select_streams a:0 -show_entries stream=codec_type -of csv=p=0 \"%s\"",
			gVideoFilename);
		FILE *probe = _popen(probeCmd, "r");
		if (probe)
		{
			if (fgets(probeResult, sizeof(probeResult), probe))
			{
				// Remove trailing newline
				size_t len = strlen(probeResult);
				if (len > 0 && probeResult[len-1] == '\n') probeResult[len-1] = 0;
			}
			_pclose(probe);
		}

		int hasAudio = (strstr(probeResult, "audio") != NULL);

		if (hasAudio)
		{
			// Build _tmp filename correctly (strip .ext, append _tmp.mp4)
			char tmpPath[MAX_PATH];
			strcpy(tmpPath, gOptExportFilename);
			char *dot = strrchr(tmpPath, '.');
			if (dot) *dot = 0;
			strcat(tmpPath, "_tmp.mp4");

			char exportAbsPath[MAX_PATH];
			_snprintf(exportAbsPath, MAX_PATH, "%s\\%s", gStartupCwd, gOptExportFilename);
			char tmpAbsPath[MAX_PATH];
			_snprintf(tmpAbsPath, MAX_PATH, "%s\\%s_tmp.mp4", gStartupCwd, gOptExportFilename);

			// Audio remux (no console window)
			char remuxCmd[8192];
			_snprintf(remuxCmd, sizeof(remuxCmd),
				"cmd.exe /c ffmpeg -loglevel info -i \"%s\" -i \"%s\" "
				"-c:v copy -c:a aac -map 0:v:0 -map 1:a:0 -y \"%s\""
				"&& move /Y \"%s\" \"%s\"",
				exportAbsPath, gVideoFilename,
				tmpAbsPath, tmpAbsPath, exportAbsPath);

			STARTUPINFOA si2 = {0}; si2.cb = sizeof(si2);
			PROCESS_INFORMATION pi2 = {0};

			if (CreateProcessA(NULL, remuxCmd, NULL, NULL, FALSE,
				CREATE_NO_WINDOW, NULL, NULL, &si2, &pi2))
			{
				WaitForSingleObject(pi2.hProcess, INFINITE);
				DWORD remuxExit = 0;
				GetExitCodeProcess(pi2.hProcess, &remuxExit);
				CloseHandle(pi2.hProcess);
				CloseHandle(pi2.hThread);

				if (remuxExit != 0)
				{
					printf("Export: audio remux failed (exit code %lu), video saved without audio\n", remuxExit);
					// Clean up orphaned _tmp.mp4 if it exists
					DeleteFileA(tmpAbsPath);
				}
			}
			else
			{
				printf("Export: audio remux CreateProcess failed (error %d), video saved without audio\n", GetLastError());
			}
		}

		gVideoExportProgress = 1.0f;
		gVideoExportActive = false;

		printf("Export complete: %s\n", gOptExportFilename);
	}
#endif
}

void cancel_video_export()
{
#ifdef _WIN32
	if (gExportRunning)
	{
		TerminateProcess(gExportProc.hProcess, 1);
		CloseHandle(gExportProc.hProcess);
		CloseHandle(gExportProc.hThread);
		gExportRunning = 0;
		gVideoExportActive = false;
	}
#endif
}

void pipe_loop()
{
	int dw = gDevice->mXRes;
	int dh = gDevice->mYRes;
	int dpixels = dw * dh;

	fprintf(stderr, "DIAG: pipe_loop() device=%s res=%dx%d pipeRes=%dx%d\n",
		gDevice ? gDevice->getname() : "NULL", dw, dh, gPipeWidth, gPipeHeight);

	{
		int mc = 0;
		Modifier *w = gModifierRoot;
		while (w) { mc++; w = w->mNext; }
		fprintf(stderr, "DIAG: pipe_loop() modifier count = %d\n", mc);
	}

#ifdef _WIN32
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	// Large buffer for stdin (read frames from decoder), default stdout (fflush per frame)
	setvbuf(stdin, NULL, _IOFBF, 16 * 1024 * 1024);

	// Use original resolution from --width/--height if provided, else device res
	int sw = gPipeWidth > 0 ? gPipeWidth : dw;
	int sh = gPipeHeight > 0 ? gPipeHeight : dh;
	int spixels = sw * sh;

	unsigned char *buf = new unsigned char[spixels * 3];
	unsigned char *frame_out = new unsigned char[dpixels * 4];
	int frameCount = 0;
	while (fread(buf, 1, spixels * 3, stdin) == (size_t)(spixels * 3))
	{
		// Store full-resolution source for modifiers (ScalePos etc.)
		if (gSourceImageData)
			stbi_image_free(gSourceImageData);
		gSourceImageData = (unsigned int *)malloc(sw * sh * 4);
		gSourceImageX = sw;
		gSourceImageY = sh;
		if (gSourceImageData)
		{
			for (int i = 0; i < spixels; i++)
			{
				int r = buf[i * 3 + 0];
				int g = buf[i * 3 + 1];
				int b = buf[i * 3 + 2];
				gSourceImageData[i] = r | (g << 8) | (b << 16) | 0xff000000;
			}
		}

		// Copy center-clipped version into device buffer
		for (int y = 0; y < dh; y++)
		{
			for (int x = 0; x < dw; x++)
			{
				int pix = 0xff000000;
				if (x < sw && y < sh)
				{
					int r = buf[(y * sw + x) * 3 + 0];
					int g = buf[(y * sw + x) * 3 + 1];
					int b = buf[(y * sw + x) * 3 + 2];
					pix = r | (g << 8) | (b << 16) | 0xff000000;
				}
				gBitmapOrig[y * dw + x] = pix;
			}
		}

		gDirtyPic = 1;
		gDirty = 1;
		if (frameCount == 0)
			fprintf(stderr, "DIAG: pipe_loop() processing first frame\n");
		process_image();
		gDevice->filter();
		gDirty = 0;
		gDirtyPic = 0;

		// Build RGBA output buffer: one fwrite + fflush per frame
		for (int i = 0; i < dpixels; i++)
		{
			unsigned int c = gBitmapSpec[i];
			frame_out[i * 4 + 0] = (unsigned char)(c & 0xff);
			frame_out[i * 4 + 1] = (unsigned char)((c >> 8) & 0xff);
			frame_out[i * 4 + 2] = (unsigned char)((c >> 16) & 0xff);
			frame_out[i * 4 + 3] = 0xff;
		}
		fwrite(frame_out, 1, dpixels * 4, stdout);
		fflush(stdout);
		frameCount++;
	}
	delete[] frame_out;
	fprintf(stderr, "DIAG: pipe_loop() processed %d frames\n", frameCount);
	delete[] buf;
}

int main(int aParamc, char**aParams)
{
	SDL_SysWMinfo wminfo;

	GetCurrentDirectoryA(MAX_PATH, gStartupCwd);

	gDevice = new ZXSpectrumDevice;

	// Pre-scan: detect --pipe mode
	pipe_mode = 0;
	for (int i = 1; i < aParamc; i++)
	{
		if (strcmp(aParams[i], "--pipe") == 0)
			pipe_mode = 1;
	}

	SDL_Window *window = 0;
	SDL_GLContext glcontext = 0;
	ImVec4 clear_color(0, 0, 0, 0);

	if (!pipe_mode)
	{
		clear_color = ImColor(114, 144, 154);

		// Setup SDL
		if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
		{
			printf("Error: %s\n", SDL_GetError());
			return -1;
		}

		// Setup window
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
		SDL_DisplayMode current;
		SDL_GetCurrentDisplayMode(0, &current);
		window = SDL_CreateWindow("Image Spectrumizer " VERSION " - http://iki.fi/sol", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
		glcontext = SDL_GL_CreateContext(window);
		SDL_VERSION(&wminfo.version);
		SDL_GetWindowWMInfo(window, &wminfo);
		// Setup ImGui binding
		ImGui_ImplSdl_Init(window);

	glGenTextures(1, &gTextureOrig);
	glBindTexture(GL_TEXTURE_2D, gTextureOrig);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)0);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenTextures(1, &gTextureProc);
	glBindTexture(GL_TEXTURE_2D, gTextureProc);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)0);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenTextures(1, &gTextureSpec);
	glBindTexture(GL_TEXTURE_2D, gTextureSpec);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)0);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenTextures(1, &gTextureAttr);
	glBindTexture(GL_TEXTURE_2D, gTextureAttr);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)0);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenTextures(1, &gTextureAttr2);
	glBindTexture(GL_TEXTURE_2D, gTextureAttr2);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)0);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenTextures(1, &gTextureBitm);
	glBindTexture(GL_TEXTURE_2D, gTextureBitm);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)0);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}

	bool done = false;

	int commandline_export = 0;
	int commandline_export_fn = 0;
	if (aParamc > 1)
	{
		for (int i = 1; i < aParamc; i++)
		{
			if (aParams[i][0] == '-')
			{
				// --width and --height for --pipe mode
				if (aParams[i][1] == '-' && aParams[i][2] != 0)
				{
					if (strcmp(aParams[i] + 2, "width") == 0 && i + 1 < aParamc)
						gPipeWidth = atoi(aParams[++i]);
					else if (strcmp(aParams[i] + 2, "height") == 0 && i + 1 < aParamc)
						gPipeHeight = atoi(aParams[++i]);
					continue;
				}
				switch (aParams[i][1])
				{
				case 'p': commandline_export = 1; break;
				case 'h': commandline_export = 2; break;
				case 'i': commandline_export = 3; break;
				case 's': commandline_export = 4; break;
				}
				commandline_export_fn = i + 1;
				i++;
			}
			else
			{
				fprintf(stderr, "DIAG: main() loading arg '%s'\n", aParams[i]);
				loadimg(aParams[i]);
				loadworkspace(aParams[i]);
			}
		}
	}

	// --pipe mode: workspace loaded above, now enter pipe loop
	if (pipe_mode)
	{
		fprintf(stderr, "DIAG: main() --pipe mode, device=%s res=%dx%d gPipeWidth=%d gPipeHeight=%d\n",
			gDevice ? gDevice->getname() : "NULL",
			gDevice ? gDevice->mXRes : 0,
			gDevice ? gDevice->mYRes : 0,
			gPipeWidth, gPipeHeight);
		pipe_loop();
		return 0;
	}

	if (aParamc < commandline_export_fn)
		return 0;

	if (commandline_export)
	{
		process_image();
		gDevice->filter();
		switch (commandline_export)
		{
		case 1: savepng(aParams[commandline_export_fn]); break;
		case 2: saveh(aParams[commandline_export_fn]); break;
		case 3: saveinc(aParams[commandline_export_fn]); break;
		case 4: savescr(aParams[commandline_export_fn]); break;
		}
		done = true;
	}

	if (!gSourceImageName)
		generateimg();

	if (!done)
		SDL_ShowWindow(window);

    // Main loop
    while (!done)
    {
		ImVec2 picsize((float)gDevice->mXRes, (float)gDevice->mYRes);
		
		SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSdl_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
        }

        ImGui_ImplSdl_NewFrame(window);
		//ImGui::ShowTestWindow();

		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Load image")) { loadimg(); }
				if (ImGui::MenuItem("Load video")) { const char *fn = openDialog("Load video",
					"All supported video\0*.mp4;*.avi;*.mkv;*.mov;*.webm;*.flv\0"
					"MP4 (*.mp4)\0*.mp4\0"
					"AVI (*.avi)\0*.avi\0"
					"MKV (*.mkv)\0*.mkv\0"
					"All Files (" ALL_FILES ")\0" ALL_FILES "\0\0");
					if (fn) load_video(fn); }
				if (ImGui::MenuItem("Reload changed image", 0, (bool*)&gOptTrackFile)) {};
				if (ImGui::MenuItem("Topmost window", 0, (bool*)&gOptTopmost)) { gDirty = 1; };
				ImGui::Separator();
				if (ImGui::MenuItem("Load workspace")) { loadworkspace(); }
				ImGui::Separator();
				if (ImGui::MenuItem("Save workspace")) { saveworkspace(); }
				ImGui::Separator();
				if (ImGui::MenuItem("Export .png")) { savepng(); }
				if (ImGui::MenuItem("Export .scr (binary)")) { savescr(); }
				if (ImGui::MenuItem("Export .h")) { saveh(); }
				if (ImGui::MenuItem("Export .inc")) { saveinc(); }
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Window"))
			{
				if (ImGui::MenuItem("Open all windows")) { gWindowAttribBitmap = true; gWindowHistograms = true; gWindowModifierPalette = true; gWindowZoomedOutput = true; gWindowZoomedModified = true; gWindowZoomedInput = true; gWindowOptions = true; }
				ImGui::Separator();
				if (ImGui::MenuItem("Attribute/bitmap", 0, &gWindowAttribBitmap)) {}
				if (ImGui::MenuItem("Histogram", 0, &gWindowHistograms)) {}
				if (ImGui::MenuItem("Modifier palette", 0, &gWindowModifierPalette)) {}
				ImGui::Separator();
				if (ImGui::MenuItem("Zoomed output", 0, &gWindowZoomedOutput)) {}
				if (ImGui::MenuItem("Zoomed modified", 0, &gWindowZoomedModified)) {}
				if (ImGui::MenuItem("Zoomed input", 0, &gWindowZoomedInput)) {}
				ImGui::Separator();
				if (ImGui::MenuItem("Options", 0, &gWindowOptions)) {}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Modifier"))
			{
				if (ImGui::MenuItem("Open modifier palette")) { gWindowModifierPalette = !gWindowModifierPalette; }
				ImGui::Separator();
				if (ImGui::MenuItem("Add Scale/Position modifier")) { addModifier(new ScalePosModifier); }
				ImGui::Separator();
				if (ImGui::MenuItem("Add RGB modifier")) { addModifier(new RGBModifier); }
				if (ImGui::MenuItem("Add HSV modifier")) { addModifier(new HSVModifier); }
				if (ImGui::MenuItem("Add YIQ modifier")) { addModifier(new YIQModifier); }
				if (ImGui::MenuItem("Add Contrast modifier")) { addModifier(new ContrastModifier); }
				ImGui::Separator();
				if (ImGui::MenuItem("Add Superblack modifier")) { addModifier(new SuperblackModifier); }
				if (ImGui::MenuItem("Add Curve modifier")) { addModifier(new CurveModifier); }
				if (ImGui::MenuItem("Add Blur modifier")) { addModifier(new BlurModifier); }
				if (ImGui::MenuItem("Add Edge modifier")) { addModifier(new EdgeModifier); }
				if (ImGui::MenuItem("Add Quantize modifier")) { addModifier(new QuantizeModifier); }
				if (ImGui::MenuItem("Add Min/max modifier")) { addModifier(new MinmaxModifier); }
				ImGui::Separator();
				if (ImGui::MenuItem("Add Noise modifier")) { addModifier(new NoiseModifier); }
				if (ImGui::MenuItem("Add Ordered Dither modifier")) { addModifier(new OrderedDitherModifier); }
				if (ImGui::MenuItem("Add Error Diffusion Dither modifier")) { addModifier(new ErrorDiffusionDitherModifier); }
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Device"))
			{
				if (ImGui::MenuItem("ZX Spectrum (16 colors)", 0, gDeviceId == 0, gDeviceId != 0)) { gDirty = 1; gDeviceId = 0; delete gDevice; gDevice = new ZXSpectrumDevice; }
				if (ImGui::MenuItem("ZX Spectrum 3x64 mode", 0, gDeviceId == 1, gDeviceId != 1)) { gDirty = 1; gDeviceId = 1; delete gDevice; gDevice = new ZX3x64Device; }
				if (ImGui::MenuItem("ZX Spectrum halftile mode", 0, gDeviceId == 2, gDeviceId != 2)) { gDirty = 1; gDeviceId = 2; delete gDevice; gDevice = new ZXHalfTileDevice; }
				ImGui::Separator();
				if (ImGui::MenuItem("C64 hires mode (experimental)", 0, gDeviceId == 3, gDeviceId != 3)) { gDirty = 1; gDeviceId = 3; delete gDevice; gDevice = new C64HiresDevice; }
				if (ImGui::MenuItem("C64 multicolor mode (experimental)", 0, gDeviceId == 4, gDeviceId != 4)) { gDirty = 1; gDeviceId = 4; delete gDevice; gDevice = new C64MulticolorDevice; }
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Help"))
			{
				if (ImGui::MenuItem("About")) { gWindowAbout = !gWindowAbout; }
				ImGui::Separator();
				if (ImGui::MenuItem("Show help")) { gWindowHelp= !gWindowHelp; }

				ImGui::EndMenu();
			}
			if (gSourceImageName)
			{
				if (ImGui::BeginMenu(gSourceImageName))
				{
					if (ImGui::MenuItem("Reload")) loadimg(gSourceImageName);
					ImGui::Separator();
					if (ImGui::MenuItem("Reload automatically", 0, (bool*)&gOptTrackFile)) {};

					ImGui::EndMenu();
				}
			}
			ImGui::EndMainMenuBar();
		}

		if (gWindowModifierPalette)
		{
			if (ImGui::Begin("Add Modifiers", &gWindowModifierPalette, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
			{
				if (ImGui::Button("Scale/Position", ImVec2(-1, 0))) { addModifier(new ScalePosModifier); }
				ImGui::Separator();
				if (ImGui::Button("RGB", ImVec2(-1, 0))) { addModifier(new RGBModifier); }
				if (ImGui::Button("HSV", ImVec2(-1, 0))) { addModifier(new HSVModifier); }
				if (ImGui::Button("YIQ", ImVec2(-1, 0))) { addModifier(new YIQModifier); }
				if (ImGui::Button("Contrast", ImVec2(-1, 0))) { addModifier(new ContrastModifier); }
				ImGui::Separator();
				if (ImGui::Button("Superblack", ImVec2(-1, 0))) { addModifier(new SuperblackModifier); }
				if (ImGui::Button("Curve", ImVec2(-1, 0))) { addModifier(new CurveModifier); }
				if (ImGui::Button("Blur", ImVec2(-1, 0))) { addModifier(new BlurModifier); }
				if (ImGui::Button("Edge", ImVec2(-1, 0))) { addModifier(new EdgeModifier); }
				if (ImGui::Button("Quantize", ImVec2(-1, 0))) { addModifier(new QuantizeModifier); }
				if (ImGui::Button("Min/max", ImVec2(-1, 0))) { addModifier(new MinmaxModifier); }
				ImGui::Separator();
				if (ImGui::Button("Noise", ImVec2(-1, 0))) { addModifier(new NoiseModifier); }
				if (ImGui::Button("Ordered Dither", ImVec2(-1, 0))) { addModifier(new OrderedDitherModifier); }
				// widest button defines the window width, so we can't set it to "auto size"
				if (ImGui::Button("Error Diffusion Dither" /*, ImVec2(-1, 0)*/)) { addModifier(new ErrorDiffusionDitherModifier); }
			}
			ImGui::End();
		}


		if (gWindowAbout)
		{
			ImGui::SetNextWindowSize(ImVec2(400, 300));
			if (ImGui::Begin("About Image Spectrumizer " VERSION, &gWindowAbout, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
			{
				ImGui::TextWrapped(
					"Image Spectrumizer\n"
					"\n"
					"Sources available at:\n"
					"https://github.com/jarikomppa/img2spec\n"
					"\n"
					"Copyright(c) 2015-2016 Jari Komppa, http://iki.fi/sol\n"
					"\n"
					"This software is provided 'as-is', without any express or implied "
					"warranty.In no event will the authors be held liable for any damages "
					"arising from the use of this software.\n"
					"\n"
					"Permission is granted to anyone to use this software for any purpose, "
					"including commercial applications, and to alter it and redistribute it "
					"freely, subject to the following restrictions :\n"
					"\n"
					"1. The origin of this software must not be misrepresented; you must not "
					"claim that you wrote the original software.If you use this software "
					"in a product, an acknowledgement in the product documentation would be "
					"appreciated but is not required.\n"
					"2. Altered source versions must be plainly marked as such, and must not be "
					"misrepresented as being the original software.\n"
					"3. This notice may not be removed or altered from any source distribution.\n"
					"\n"
					"--- 8< --- 8< --- 8< ---\n"
					"\n");
				ImGui::TextWrapped(
					"Uses Ocornut's ImGui library, from:\n"
					"https://github.com/ocornut/imgui\n"
					"\n"
					"The MIT License(MIT)\n"
					"\n"
					"Copyright(c) 2014 - 2016 Omar Cornut and ImGui contributors\n"
					"\n"
					"Permission is hereby granted, free of charge, to any person obtaining a copy "
					"of this software and associated documentation files(the \"Software\"), to deal "
					"in the Software without restriction, including without limitation the rights "
					"to use, copy, modify, merge, publish, distribute, sublicense, and / or sell "
					"copies of the Software, and to permit persons to whom the Software is "
					"furnished to do so, subject to the following conditions :\n"
					"\n"
					"The above copyright notice and this permission notice shall be included in all "
					"copies or substantial portions of the Software.\n"
					"\n"
					"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
					"IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
					"FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE "
					"AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER "
					"LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
					"OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
					"SOFTWARE.\n"
					"\n"
					"--- 8< --- 8< --- 8< ---\n"
					"\n");
				ImGui::TextWrapped(
					"Uses various STB libraries from:\n"
					"https://github.com/nothings/stb/\n"
					"by Sean Barrett, under public domain\n"
					"\n"
					"--- 8< --- 8< --- 8< ---\n"
					"\n"
					"For JSON parsing, uses Parson(http://kgabis.github.com/parson/ )\n"
					"Copyright(c) 2012 - 2016 Krzysztof Gabis\n"
					"\n"
					"Permission is hereby granted, free of charge, to any person obtaining a copy "
					"of this software and associated documentation files(the \"Software\"), to deal "
					"in the Software without restriction, including without limitation the rights "
					"to use, copy, modify, merge, publish, distribute, sublicense, and / or sell "
					"copies of the Software, and to permit persons to whom the Software is "
					"furnished to do so, subject to the following conditions :\n"
					"\n"
					"The above copyright notice and this permission notice shall be included in "
					"all copies or substantial portions of the Software.\n"
					"\n"
					"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
					"IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
					"FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE "
					"AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER "
					"LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
					"OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN "
					"THE SOFTWARE.\n"
					"\n");

			}
			ImGui::End();
		}

		if (gWindowHelp)
		{
			ImGui::SetNextWindowSize(ImVec2(400, 300));
			if (ImGui::Begin("Halp!", &gWindowHelp, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
			{
				ImGui::TextWrapped(
					"Tips:\n"
					"----\n"
					"- The window can be resized.\n"
					"- Various helper windows can be opened from the window menu.\n"
					"- Don't hesitate to play with conversion options.\n"
					"\n"
					"Typical workflow:\n"
					"----------------"
					"\n"
					"1. Load an image (file->load image)\n"
					"2. Add modifiers (modifiers->...)\n"
					"3. (optionally) open options (window->options) and change conversion options\n"
					"4. Tweak modifiers until result is acceptable\n"
					"5. Save result (file->save ...)\n"
					"\n"
					"Image editor interoperation\n"
					"--------------------------\n"
					"If you keep the image file open in image spectrumizer and the image editor of your "
					"choice (such as photoshop), image spectrumizer can detect when the file has changed "
					"and reloads the image automatically. This way you can keep editing the source image "
					"with (potentially) better tools than image spectrumizer can offer, and see the "
					"results relatively quickly.\n"
					"\n"
					"This feature is enabled by default, and can be disabled from the options.\n"
					"\n"
					"File formats:\n"
					"------------\n"
					"The file formats for scr, h and inc are basically the same. Bitmap (in spectrum screen "
					"order) is followed by attribute data. In case of non-standard cell sizes, the attribute "
					"data is bigger, but still in linear order - it is up to the code that uses the data to "
					"figure out how to use it.\n"
					"\n"
					"If you prefer the data to be in linear order (not in spectrum screen order), you can "
					"change that from the options.\n"
					"\n"
					".scr is a binary format, basically memory dump of the spectrum screen.\n"
					"\n"
					".h is C array, only data is included, so typical use would be:\n"
					"  const char myimagedata[]= {\n"
					"  #include \"myimagedata.h\"\n"
					"  };\n"
					"\n"
					".inc is assembler .db lines.\n"
					"\n"
					"Command line arguments\n"
					"----------------------\n"
					"Any image or workspace filenames given as arguments are loaded (in the order given). If "
					"multiple loadable images or workspaces are given, the last ones are the ones that are used. \n"
					"\n"
					"(Well, all of them do get loaded, they just replace the earlier ones in memory).\n"
					"\n"
					"To save results on commandline, use the following flags:\n"
					"-p pngfilename.png\n"
					"-h headerfilename.h\n"
					"-i incfilename.inc\n"
					"-s scrfilename.scr\n"
					"\n"
					"Example:\n"
					"\n"
					"img2spec cat.png mush.isw -h cat.h\n"
					"\n"
					"Output files are overwritten without a warning, but I trust you know what you're doing.\n"
					"\n"
					"3x64 mode\n"
					"---------\n"
					"In this mode, two sets of attributes are calculated, and the application is expected to "
					"swap between the two every frame. This creates approximately 3*64 colors (due to double "
					"blacks, the actual number of colors is quite much lower). The effect flickers on emulators "
					"but works \"fine\" on CRTs. (Why 3x64 and not 192? because there's three sets of (about) 64 "
					"color palettes, and each cell can use 2 from one of those).\n"
					"\n"
					"Note that the color reproduction results in real hardware will differ from the simulated, "
					"depending on the capabilities of the display device, and may be a flickering nightmare.\n "
					"\n"
					"Halftile mode\n"
					"-------------\n"
					"In this mode, display bitmap is filled with half-filled tiles for a super-low res, but\n"
					"relatively high color mode, as each \"pixel\" can use colors more freely\n"
					);
			}
			ImGui::End();
		}


		if (gWindowOptions)
		{
			if (ImGui::Begin("Options", &gWindowOptions, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
			{
				gDevice->options();
				ImGui::Separator();
				ImGui::SliderInt("Zoomed window zoom factor", &gOptZoom, 1, 8);
				ImGui::Combo("Zoomed window style", &gOptZoomStyle, "Normal\0Separated cells\0");
				ImGui::Separator();
				ImGui::Combo("Track changes to source image", &gOptTrackFile, "Do not track\0Reload when changed\0");
				if (ImGui::Combo("Keep window topmost", &gOptTopmost, "Normal window\0Topmost window\0")) gDirty = 1;
			}
			ImGui::End();
		}

		if (gWindowAttribBitmap)
		{
			if (ImGui::Begin("Attrib/bitmap", &gWindowAttribBitmap, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
			{
				gDevice->attr_bitm();
			}
			ImGui::End();
		}

		if (gWindowZoomedOutput)
		{
			if (ImGui::Begin("Zoomed output", &gWindowZoomedOutput, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
			{
				gDevice->zoomed(0);
			}
			ImGui::End();
		}

		if (gWindowZoomedModified)
		{
			if (ImGui::Begin("Zoomed modified", &gWindowZoomedModified, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
			{
				gDevice->zoomed(1);
			}
			ImGui::End();
		}

		if (gWindowZoomedInput)
		{
			if (ImGui::Begin("Zoomed input", &gWindowZoomedInput, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
			{
				gDevice->zoomed(2);
			}
			ImGui::End();
		}

		if (gWindowHistograms)
		{
			if (ImGui::Begin("Histograms", &gWindowHistograms, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
			{
				calc_histogram(gBitmapOrig);
				ImGui::PlotHistogram("###hist1", gHistogramR, 256, 0, 0, 0, 1024, ImVec2(256, 32)); ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine();
				ImGui::PlotHistogram("###hist2", gHistogramG, 256, 0, 0, 0, 1024, ImVec2(256, 32)); ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine();
				ImGui::PlotHistogram("###hist3", gHistogramB, 256, 0, 0, 0, 1024, ImVec2(256, 32));
				calc_histogram(gBitmapProc);
				ImGui::PlotHistogram("###hist4", gHistogramR, 256, 0, 0, 0, 1024, ImVec2(256, 32)); ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine();
				ImGui::PlotHistogram("###hist5", gHistogramG, 256, 0, 0, 0, 1024, ImVec2(256, 32)); ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine();
				ImGui::PlotHistogram("###hist6", gHistogramB, 256, 0, 0, 0, 1024, ImVec2(256, 32));
				calc_histogram(gBitmapSpec);
				ImGui::PlotHistogram("###hist7", gHistogramR, 256, 0, 0, 0, 1024, ImVec2(256, 32)); ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine();
				ImGui::PlotHistogram("###hist8", gHistogramG, 256, 0, 0, 0, 1024, ImVec2(256, 32)); ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine();
				ImGui::PlotHistogram("###hist9", gHistogramB, 256, 0, 0, 0, 1024, ImVec2(256, 32));
			}
			ImGui::End();
		}

		if (gWindowExport && gVideoMode)
		{
			if (ImGui::Begin("Export video", &gWindowExport,
				ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
			{
				ImGuiInputTextFlags flags = gVideoExportActive ? ImGuiInputTextFlags_ReadOnly : 0;
				ImGui::InputText("Output file", gOptExportFilename, 1024, flags);
				ImGui::Combo("Encoder", &gOptExportEncoder,
					"NVIDIA NVENC\0AMD AMF\0CPU x264\0");
				ImGui::SliderInt("Quality (CRF/QP)", &gOptExportQuality, 0, 51);
				ImGui::SliderInt("Scale", &gOptExportScale, 1, 32);
				ImGui::InputText("Extra ffmpeg params", gOptExportExtraParams, 1024);
				const char *encoders[] = {"NVENC", "AMF", "x264"};
				ImGui::Text("Settings: %s | x%d | Q%d",
					encoders[gOptExportEncoder],
					gOptExportScale, gOptExportQuality);

				if (gVideoExportActive)
				{
					int pct = (int)(gVideoExportProgress * 100);
					ImGui::Text("Progress: %d%%", pct);
					if (ImGui::Button("Cancel"))
						cancel_video_export();
				}
				else
				{
					if (ImGui::Button("Start export"))
					{
						start_video_export();
						gVideoExportActive = true;
					}
				}
			}
			ImGui::End();
		}
		
		if (gOptImagesDocked)
		{
			//ImGui::SetNextWindowSize(ImVec2(828, 512));
			//ImGui::Begin("Image", 0, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
			ImGui::SetNextWindowSize(ImVec2(828, ImGui::GetIO().DisplaySize.y-32));
			ImGui::Begin("Image", 0, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
		}
		else
		{
			ImGui::Begin("Image", 0, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);
		}
	

		if (gOptShowOriginal)
		{
			ImVec2 tex_screen_pos = ImGui::GetCursorScreenPos();
			ImGui::Image((ImTextureID)gTextureOrig, picsize, ImVec2(0, 0), ImVec2(gDevice->mXRes / 1024.0f, gDevice->mYRes / 512.0f));
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				float focus_sz = 32.0f;
				float focus_x = ImGui::GetMousePos().x - tex_screen_pos.x - focus_sz * 0.5f; if (focus_x < 0.0f) focus_x = 0.0f; else if (focus_x > gDevice->mXRes - focus_sz) focus_x = gDevice->mXRes - focus_sz;
				float focus_y = ImGui::GetMousePos().y - tex_screen_pos.y - focus_sz * 0.5f; if (focus_y < 0.0f) focus_y = 0.0f; else if (focus_y > gDevice->mYRes - focus_sz) focus_y = gDevice->mYRes - focus_sz;
				ImVec2 uv0 = ImVec2((focus_x) / 1024.0f, (focus_y) / 512.0f);
				ImVec2 uv1 = ImVec2((focus_x + focus_sz) / 1024.0f, (focus_y + focus_sz) / 512.0f);
				ImGui::Image((ImTextureID)gTextureOrig, ImVec2(128, 128), uv0, uv1, ImColor(255, 255, 255, 255), ImColor(255, 255, 255, 128));
				ImGui::EndTooltip();
			}
			if (gOptShowModified || gOptShowResult) { ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine(); }
		}

		if (gOptShowModified)
		{
			ImVec2 tex_screen_pos = ImGui::GetCursorScreenPos();
			ImGui::Image((ImTextureID)gTextureProc, picsize, ImVec2(0, 0), ImVec2(gDevice->mXRes / 1024.0f, gDevice->mYRes / 512.0f));
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				float focus_sz = 32.0f;
				float focus_x = ImGui::GetMousePos().x - tex_screen_pos.x - focus_sz * 0.5f; if (focus_x < 0.0f) focus_x = 0.0f; else if (focus_x > gDevice->mXRes - focus_sz) focus_x = gDevice->mXRes - focus_sz;
				float focus_y = ImGui::GetMousePos().y - tex_screen_pos.y - focus_sz * 0.5f; if (focus_y < 0.0f) focus_y = 0.0f; else if (focus_y > gDevice->mYRes - focus_sz) focus_y = gDevice->mYRes - focus_sz;
				ImVec2 uv0 = ImVec2((focus_x) / 1024.0f, (focus_y) / 512.0f);
				ImVec2 uv1 = ImVec2((focus_x + focus_sz) / 1024.0f, (focus_y + focus_sz) / 512.0f);
				ImGui::Image((ImTextureID)gTextureProc, ImVec2(128, 128), uv0, uv1, ImColor(255, 255, 255, 255), ImColor(255, 255, 255, 128));
				ImGui::EndTooltip();
			}
			if (gOptShowResult) { ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine(); }
		}

		if (gOptShowResult)
		{
			ImVec2 tex_screen_pos = ImGui::GetCursorScreenPos();
			ImGui::Image((ImTextureID)gTextureSpec, picsize, ImVec2(0, 0), ImVec2(gDevice->mXRes / 1024.0f, gDevice->mYRes / 512.0f));
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				float focus_sz = 32.0f;
				float focus_x = ImGui::GetMousePos().x - tex_screen_pos.x - focus_sz * 0.5f; if (focus_x < 0.0f) focus_x = 0.0f; else if (focus_x > gDevice->mXRes - focus_sz) focus_x = gDevice->mXRes - focus_sz;
				float focus_y = ImGui::GetMousePos().y - tex_screen_pos.y - focus_sz * 0.5f; if (focus_y < 0.0f) focus_y = 0.0f; else if (focus_y > gDevice->mYRes - focus_sz) focus_y = gDevice->mYRes - focus_sz;
				ImVec2 uv0 = ImVec2((focus_x) / 1024.0f, (focus_y) / 512.0f);
				ImVec2 uv1 = ImVec2((focus_x + focus_sz) / 1024.0f, (focus_y + focus_sz) / 512.0f);
				ImGui::Image((ImTextureID)gTextureSpec, ImVec2(128, 128), uv0, uv1, ImColor(255, 255, 255, 255), ImColor(255, 255, 255, 128));
				ImGui::EndTooltip();
			}



		}

		ImGui::Checkbox("Original", &gOptShowOriginal); ImGui::SameLine();
		ImGui::Checkbox("Modified", &gOptShowModified); ImGui::SameLine();
		ImGui::Checkbox("Result", &gOptShowResult); ImGui::SameLine();
		ImGui::Checkbox("Dock images", &gOptImagesDocked);

		if (gVideoMode)
		{
			ImGui::Separator();

			int frame = gVideoCurrentFrame;
			if (ImGui::SliderInt("##timeline", &frame, 0,
				(gVideoTotalFrames > 1) ? (gVideoTotalFrames - 1) : 1,
				"Frame %d"))
			{
				if (frame != gVideoCurrentFrame && frame >= 0 &&
					frame < gVideoTotalFrames)
					get_video_frame(frame);
			}

			ImGui::SameLine();
			if (ImGui::Button("|<")) get_video_frame(0);
			ImGui::SameLine();
			if (ImGui::Button("<"))
				get_video_frame(std::max(0, gVideoCurrentFrame - 1));
			ImGui::SameLine();
			if (ImGui::Button(">"))
				get_video_frame(std::min(gVideoTotalFrames - 1, gVideoCurrentFrame + 1));
			ImGui::SameLine();
			if (ImGui::Button(">|"))
				get_video_frame(gVideoTotalFrames - 1);
			ImGui::SameLine();

			int sec = (int)(gVideoCurrentFrame / gVideoFps);
			int totalSec = (int)(gVideoDuration);
			ImGui::Text("%02d:%02d / %02d:%02d (%.0f fps)",
				sec / 60, sec % 60,
				totalSec / 60, totalSec % 60,
				gVideoFps);

		ImGui::Separator();
		if (ImGui::Button("Export video..."))
		{
			// Set default output filename (basename only, save in CWD)
			const char *base = strrchr(gVideoFilename, '\\');
			if (!base) base = strrchr(gVideoFilename, '/');
			if (base) base++; else base = gVideoFilename;
			strcpy(gOptExportFilename, base);
			char *dot = strrchr(gOptExportFilename, '.');
			if (dot) *dot = 0;
			strcat(gOptExportFilename, "_spmz.mp4");
			gWindowExport = true;
		}
		}

		if (!gOptImagesDocked)
		{
			ImGui::End();

//			ImGui::SetNextWindowSize(ImVec2(828, 400));
//			ImGui::Begin("Modifiers", 0, ImGuiWindowFlags_NoResize);
			ImGui::SetNextWindowSize(ImVec2(828, ImGui::GetIO().DisplaySize.y-32));
			ImGui::Begin("Modifiers", 0, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
		}
		ImGui::BeginChild("Mod");
		modifier_ui();
		ImGui::EndChild();		
		ImGui::End();

		if (gOptTrackFile && !gVideoMode && gSourceImageName)
		{
			int fd = getFileDate(gSourceImageName);
			if (fd != gSourceImageDate)
				loadimg(gSourceImageName);
		}

		if (gDirtyPic && !gVideoMode)
		{
			if (gSourceImageData)
				loadimg(gSourceImageName);
			else
				generateimg();
		}

		if (gVideoExportActive)
			poll_video_export();

		if (gDirty)
		{
			process_image();
			gDevice->filter();

			update_texture(gTextureProc, gBitmapProc);
			update_texture(gTextureSpec, gBitmapSpec);
			
			gDirty = 0;
			gDirtyPic = 0;

#ifdef _WIN32
			if (gOptTopmost)
				SetWindowPos(wminfo.info.win.window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			else
				SetWindowPos(wminfo.info.win.window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#endif
				
		}

        // Rendering
        glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui::Render();
        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    if (!pipe_mode)
    {
        ImGui_ImplSdl_Shutdown();
        SDL_GL_DeleteContext(glcontext);  
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    return 0;
}
