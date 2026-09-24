class Device
{
public:
	virtual ~Device()
	{
		gDirty = 1;
		gDirtyPic = 1;
	}
	int mXRes;
	int mYRes;
	virtual void filter() = 0;
	virtual int estimate_rgb(int c) = 0;
	// Fixed target palette for libdither color dithering (dither_bridge.h).
	// Returns 0xRRGGBB entry i (no alpha); count via palette_count().
	virtual int palette_count() = 0;
	virtual int palette_entry(int i) = 0;
	virtual void savescr(FILE * f) = 0;
	// File extension for per-frame raw dumps in video export (temp/scr).
	// Default "scr" covers ZX Spectrum family; devices with a different
	// on-disk layout override it (ZX3x64 -> "3sc", C64Hires -> "h64", ...).
	virtual const char *dumpext() { return "scr"; }
	virtual void saveh(FILE * f) = 0;
	virtual void saveinc(FILE * f) = 0;
	virtual void attr_bitm() = 0;
	virtual void options() = 0;
	virtual void zoomed(int aWhich) = 0;
	virtual void writeOptions(JSON_Object *root) = 0;
	virtual void readOptions(JSON_Object *root) = 0;
	virtual char *getname() = 0;
};