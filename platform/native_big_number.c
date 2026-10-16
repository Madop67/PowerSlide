// Big place number for big-field races. Retail's HUD number (STATIC_BIG1) is one flat
// mesh per digit, 1 to 8, picked by LOD distance: a front face shaded yellow at the top
// to red at the bottom, ringed by an outline that fades from half that colour to
// black. Places 9 to 24 get one-header models built from those meshes: the 9 is the
// 6 turned upside down and reshaded, the 0 is a ring drawn in the same style, and two
// digits are condensed side by side so the number keeps the single digit's right
// edge and baseline.

#include "platform/native_big_field.h"
#include "platform/native_log.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define BIG_NUMBER_DIGITS        8
#define BIG_NUMBER_FIRST_PLACE   (NATIVE_RETAIL_DRIVERS + 1)
#define BIG_NUMBER_PLACES        (NATIVE_MAX_DRIVERS - NATIVE_RETAIL_DRIVERS)
#define BIG_NUMBER_MAX_COMMANDS  256
#define BIG_NUMBER_MAX_VERTS     256
#define BIG_NUMBER_MAX_COLOURS   128
#define BIG_NUMBER_GLYPH_COLOURS 64

// Command word: flags << 24 | vertex stack slot << 16 | colour index << 9 | texture.
#define BIG_NUMBER_CMD_NEW_STRIP     0x80
#define BIG_NUMBER_CMD_CACHED_VERTEX 0x04
// Colour from the scratchpad copy of the colour table. That copy shares scratchpad
// with the cached vertices, which retail's digits avoid by how they number both;
// merged palettes don't, so place models read the table itself.
#define BIG_NUMBER_CMD_SCRATCH_COLOR 0x08
#define BIG_NUMBER_CMD_STACK_SLOT    0x57
#define BIG_NUMBER_CMD_COLOUR(c)     (((c) >> 9) & 0x7f)

// Size of a two-digit number against a single digit: height, and each digit's width.
#define BIG_NUMBER_PAIR_SCALE     0.84f
#define BIG_NUMBER_PAIR_CONDENSE  0.68f
// The second digit overlaps the first's outline by this share of a digit's width.
#define BIG_NUMBER_PAIR_OVERLAP   0.04f
// Outline width of the generated 0, in vertex units.
#define BIG_NUMBER_OUTLINE        11.0f

// A digit mesh: commands without the colour count and the 0xffffffff terminator,
// x/y/z vertex bytes (y is depth, always 0), and the header's x/y scale.
struct BigNumberGlyph
{
	const u32 *commands;
	int numCommands;
	const u32 *colours;
	const u8 *verts;
	int numVerts;
	int scaleX;
	int scaleY;
};

struct BigNumberGlyphStore
{
	u32 commands[BIG_NUMBER_MAX_COMMANDS / 2];
	u32 colours[BIG_NUMBER_GLYPH_COLOURS];
	u8 verts[BIG_NUMBER_MAX_VERTS / 2][3];
	int numColours;
};

struct BigNumberModel
{
	struct Model model;
	struct ModelHeader header;
	struct ModelFrame frame;
	// Right after the frame: ModelFrame.vertexOffset is sizeof(struct ModelFrame).
	u8 verts[BIG_NUMBER_MAX_VERTS][3];
	u32 commands[BIG_NUMBER_MAX_COMMANDS + 2];
	u32 colours[BIG_NUMBER_MAX_COLOURS];
};

CTR_STATIC_ASSERT(sizeof(struct ModelFrame) == 0x1c);
CTR_STATIC_ASSERT(offsetof(struct BigNumberModel, verts) == offsetof(struct BigNumberModel, frame) + sizeof(struct ModelFrame));

global_variable struct BigNumberModel s_bigNumberModels[BIG_NUMBER_PLACES];
global_variable struct BigNumberGlyphStore s_bigNumberNine;
global_variable struct BigNumberGlyphStore s_bigNumberZero;
// The retail model the places were built from, and whether that worked.
global_variable struct ModelHeader *s_bigNumberSource;
global_variable int s_bigNumberReady;

// Front-face shades by height: retail's levels and the vertex heights where each starts.
global_variable const u8 s_bigNumberLevels[] = {0x00, 0x1e, 0x4a, 0x63, 0x7c, 0x95, 0xb1, 0xcc, 0xe5, 0xff};
global_variable const u8 s_bigNumberLevelStart[] = {31, 56, 76, 105, 130, 151, 177, 198, 223};

enum
{
	BIG_NUMBER_BLACK,
	BIG_NUMBER_OUTLINE_SHADE,
	BIG_NUMBER_FACE,
};

// Retail colours are 0x00BBGGRR: face 0xff red, outline 0x7f red with half the green.
internal int BigNumber_ShadeKind(u32 colour)
{
	u32 red = colour & 0xff;
	if (red < 0x40)
	{
		return BIG_NUMBER_BLACK;
	}
	return (red < 0xc0) ? BIG_NUMBER_OUTLINE_SHADE : BIG_NUMBER_FACE;
}

internal u32 BigNumber_Shade(int kind, int height)
{
	int level = 0;

	if (kind == BIG_NUMBER_BLACK)
	{
		return 0;
	}
	while ((level < (int)SDL_arraysize(s_bigNumberLevelStart)) && (height >= s_bigNumberLevelStart[level]))
	{
		level++;
	}
	if (kind == BIG_NUMBER_OUTLINE_SHADE)
	{
		return 0x7f | ((u32)(s_bigNumberLevels[level] >> 1) << 8);
	}
	return 0xff | ((u32)s_bigNumberLevels[level] << 8);
}

internal int BigNumber_ColourIndex(u32 *colours, int *count, int capacity, u32 colour)
{
	for (int i = 0; i < *count; i++)
	{
		if (colours[i] == colour)
		{
			return i;
		}
	}
	if (*count >= capacity)
	{
		return -1;
	}
	colours[*count] = colour;
	return (*count)++;
}

internal u32 BigNumber_Command(int flags, int colourIndex)
{
	return ((u32)flags << 24) | ((u32)BIG_NUMBER_CMD_STACK_SLOT << 16) | ((u32)colourIndex << 9);
}

internal int BigNumber_GlyphFromHeader(struct ModelHeader *mh, struct BigNumberGlyph *glyph)
{
	const u32 *commands = (const u32 *)(uintptr_t)mh->ptrCommandList;
	int numVerts = 0;
	int count = 0;

	if ((commands == NULL) || (mh->ptrFrameData == NULL) || (mh->ptrColors == NULL) || (mh->ptrAnimations != NULL) || (mh->unk3 != 0))
	{
		return 0;
	}
	while (commands[1 + count] != 0xffffffffu)
	{
		u32 command = commands[1 + count];
		if ((command >> 16) == 0)
		{
			// Colour-only commands don't occur in the big numbers.
			return 0;
		}
		if (((command >> 24) & BIG_NUMBER_CMD_CACHED_VERTEX) == 0)
		{
			numVerts++;
		}
		if (++count > BIG_NUMBER_MAX_COMMANDS / 2)
		{
			return 0;
		}
	}

	glyph->commands = &commands[1];
	glyph->numCommands = count;
	glyph->colours = mh->ptrColors;
	glyph->verts = (const u8 *)mh->ptrFrameData + mh->ptrFrameData->vertexOffset;
	glyph->numVerts = numVerts;
	glyph->scaleX = mh->scale.x;
	glyph->scaleY = mh->scale.y;
	return numVerts <= BIG_NUMBER_MAX_VERTS / 2;
}

// The 6 turned half a turn (in the mesh plane, so the winding holds), each command
// reshaded for the height its vertex ends up at.
internal void BigNumber_BuildNine(const struct BigNumberGlyph *six, struct BigNumberGlyph *nine)
{
	struct BigNumberGlyphStore *store = &s_bigNumberNine;
	u8 stackVertex[256];
	int vertex = 0;

	memset(stackVertex, 0, sizeof(stackVertex));
	store->numColours = 0;
	for (int i = 0; i < six->numVerts; i++)
	{
		store->verts[i][0] = (u8)(255 - six->verts[i * 3 + 0]);
		store->verts[i][1] = six->verts[i * 3 + 1];
		store->verts[i][2] = (u8)(255 - six->verts[i * 3 + 2]);
	}
	for (int i = 0; i < six->numCommands; i++)
	{
		u32 command = six->commands[i];
		int slot = (command >> 16) & 0xff;
		int index;
		u32 colour;

		if (((command >> 24) & BIG_NUMBER_CMD_CACHED_VERTEX) == 0)
		{
			stackVertex[slot] = (u8)vertex++;
		}
		colour = six->colours[BIG_NUMBER_CMD_COLOUR(command)];
		colour = BigNumber_Shade(BigNumber_ShadeKind(colour), store->verts[stackVertex[slot]][2]);
		index = BigNumber_ColourIndex(store->colours, &store->numColours, BIG_NUMBER_GLYPH_COLOURS, colour);
		store->commands[i] = (command & ~(0x7fu << 9)) | ((u32)SDL_max(index, 0) << 9);
	}

	nine->commands = store->commands;
	nine->numCommands = six->numCommands;
	nine->colours = store->colours;
	nine->verts = &store->verts[0][0];
	nine->numVerts = six->numVerts;
	nine->scaleX = six->scaleX;
	nine->scaleY = six->scaleY;
}

// One closed strip between two loops of n points, a coloured kindA and b kindB.
internal void BigNumber_EmitRing(struct BigNumberGlyphStore *store, struct BigNumberGlyph *glyph, float (*a)[2], int kindA, float (*b)[2],
                                 int kindB, int n)
{
	for (int i = 0; i <= n * 2 + 1; i++)
	{
		const float *p = ((i & 1) == 0) ? a[(i / 2) % n] : b[(i / 2) % n];
		int kind = ((i & 1) == 0) ? kindA : kindB;
		u8 *v = store->verts[glyph->numVerts++];
		int index;

		v[0] = (u8)SDL_clamp((int)lroundf(p[0]), 0, 255);
		v[1] = 0;
		v[2] = (u8)SDL_clamp((int)lroundf(p[1]), 0, 255);
		index = BigNumber_ColourIndex(store->colours, &store->numColours, BIG_NUMBER_GLYPH_COLOURS, BigNumber_Shade(kind, v[2]));
		store->commands[glyph->numCommands++] =
		    BigNumber_Command((i == 0) ? BIG_NUMBER_CMD_NEW_STRIP : 0, SDL_max(index, 0));
	}
}

// Moves each point of a loop by distance along its corner's outward (away from the
// centre) or inward bisector.
internal void BigNumber_OffsetLoop(float (*in)[2], float (*out)[2], int n, float distance, int outward)
{
	for (int i = 0; i < n; i++)
	{
		const float *prev = in[(i + n - 1) % n];
		const float *next = in[(i + 1) % n];
		float tx = next[0] - prev[0];
		float ty = next[1] - prev[1];
		float length = sqrtf(tx * tx + ty * ty);
		float nx = ty / length;
		float ny = -tx / length;
		float toCentreX = 127.5f - in[i][0];
		float toCentreY = 127.5f - in[i][1];

		if (((nx * toCentreX + ny * toCentreY) > 0.0f) == (outward != 0))
		{
			nx = -nx;
			ny = -ny;
		}
		out[i][0] = in[i][0] + nx * distance;
		out[i][1] = in[i][1] + ny * distance;
	}
}

// A 0 in the 8's proportions: its outer outline without the waist, a hole scaled from it.
internal void BigNumber_BuildZero(const struct BigNumberGlyph *eight, struct BigNumberGlyph *zero)
{
	static const float kOuter[][2] = {{96, 243}, {167, 242}, {213, 221}, {241, 188}, {240, 140}, {238, 90}, {204, 34},
	                                  {124, 10}, {50, 26},   {13, 68},   {20, 130},  {29, 197},  {56, 225}};
	enum
	{
		kPoints = SDL_arraysize(kOuter)
	};
	struct BigNumberGlyphStore *store = &s_bigNumberZero;
	float outer[kPoints][2];
	float hole[kPoints][2];
	float outerEdge[kPoints][2];
	float holeEdge[kPoints][2];

	for (int i = 0; i < kPoints; i++)
	{
		outer[i][0] = kOuter[i][0];
		outer[i][1] = kOuter[i][1];
		hole[i][0] = 127.5f + (kOuter[i][0] - 127.5f) * 0.5f;
		hole[i][1] = 127.5f + (kOuter[i][1] - 127.5f) * 0.64f;
	}
	BigNumber_OffsetLoop(outer, outerEdge, kPoints, BIG_NUMBER_OUTLINE, 1);
	BigNumber_OffsetLoop(hole, holeEdge, kPoints, BIG_NUMBER_OUTLINE, 0);

	store->numColours = 0;
	zero->commands = store->commands;
	zero->numCommands = 0;
	zero->colours = store->colours;
	zero->verts = &store->verts[0][0];
	zero->numVerts = 0;
	zero->scaleX = eight->scaleX;
	zero->scaleY = eight->scaleY;
	BigNumber_EmitRing(store, zero, outer, BIG_NUMBER_FACE, hole, BIG_NUMBER_FACE, kPoints);
	BigNumber_EmitRing(store, zero, outer, BIG_NUMBER_OUTLINE_SHADE, outerEdge, BIG_NUMBER_BLACK, kPoints);
	BigNumber_EmitRing(store, zero, hole, BIG_NUMBER_OUTLINE_SHADE, holeEdge, BIG_NUMBER_BLACK, kPoints);
}

// Lays the glyphs out in one model. A single glyph keeps its own vertices and scale;
// two are condensed into the box of the 8 (reference), right- and bottom-aligned.
internal int BigNumber_Compose(struct BigNumberModel *out, struct ModelHeader *templateHeader, const struct Model *source,
                               const struct BigNumberGlyph *const *glyphs, int count, const struct BigNumberGlyph *reference)
{
	float width[2];
	float total = 0.0f;
	float left;
	int scaleX = glyphs[0]->scaleX;
	int scaleY = glyphs[0]->scaleY;
	int originX = -127;
	int numVerts = 0;
	int numCommands = 0;
	int numColours = 0;
	u32 *commands = &out->commands[1];

	memset(out, 0, sizeof(*out));
	if (count == 2)
	{
		for (int i = 0; i < 2; i++)
		{
			width[i] = 255.0f * glyphs[i]->scaleX * BIG_NUMBER_PAIR_SCALE * BIG_NUMBER_PAIR_CONDENSE;
		}
		total = width[0] + width[1] - 255.0f * reference->scaleX * BIG_NUMBER_PAIR_OVERLAP;
		left = 128.0f * reference->scaleX - total;
		scaleX = (int)ceilf(total / 250.0f);
		scaleY = reference->scaleY;
		originX = (int)floorf(left / scaleX);
	}

	for (int g = 0; g < count; g++)
	{
		const struct BigNumberGlyph *glyph = glyphs[g];
		int remap[BIG_NUMBER_MAX_COLOURS];
		int glyphColours = 0;

		if ((numVerts + glyph->numVerts > BIG_NUMBER_MAX_VERTS) || (numCommands + glyph->numCommands > BIG_NUMBER_MAX_COMMANDS))
		{
			return 0;
		}
		for (int i = 0; i < glyph->numVerts; i++)
		{
			const u8 *in = &glyph->verts[i * 3];
			u8 *v = out->verts[numVerts + i];
			if (count == 1)
			{
				memcpy(v, in, 3);
				continue;
			}
			float x = left + ((g == 0) ? 0.0f : width[0] - 255.0f * reference->scaleX * BIG_NUMBER_PAIR_OVERLAP) +
			          in[0] * glyph->scaleX * BIG_NUMBER_PAIR_SCALE * BIG_NUMBER_PAIR_CONDENSE;
			float y = in[2] * glyph->scaleY * BIG_NUMBER_PAIR_SCALE;
			v[0] = (u8)SDL_clamp((int)lroundf(x / scaleX) - originX, 0, 255);
			v[1] = 0;
			v[2] = (u8)SDL_clamp((int)lroundf(y / scaleY), 0, 255);
		}
		for (int i = 0; i < glyph->numCommands; i++)
		{
			glyphColours = SDL_max(glyphColours, BIG_NUMBER_CMD_COLOUR(glyph->commands[i]) + 1);
		}
		for (int i = 0; i < glyphColours; i++)
		{
			remap[i] = BigNumber_ColourIndex(out->colours, &numColours, BIG_NUMBER_MAX_COLOURS, glyph->colours[i]);
			if (remap[i] < 0)
			{
				return 0;
			}
		}
		for (int i = 0; i < glyph->numCommands; i++)
		{
			u32 command = glyph->commands[i];
			command &= ~(((u32)BIG_NUMBER_CMD_SCRATCH_COLOR << 24) | (0x7fu << 9));
			commands[numCommands++] = command | ((u32)remap[BIG_NUMBER_CMD_COLOUR(glyph->commands[i])] << 9);
		}
		numVerts += glyph->numVerts;
	}

	// The list starts with how many colours Execute copies to scratchpad, for the
	// commands that don't read the table.
	out->commands[0] = 0;
	commands[numCommands] = 0xffffffffu;

	out->frame.pos.x = (s16)originX;
	out->frame.pos.y = -127;
	out->frame.pos.z = 0;
	out->frame.vertexOffset = sizeof(struct ModelFrame);

	memcpy(out->header.name, templateHeader->name, sizeof(out->header.name));
	out->header.unk1 = templateHeader->unk1;
	out->header.maxDistanceLOD = templateHeader->maxDistanceLOD;
	out->header.flags = templateHeader->flags;
	out->header.scale.x = (s16)scaleX;
	out->header.scale.y = (s16)scaleY;
	out->header.scale.z = templateHeader->scale.z;
	out->header.ptrCommandList = (u32)(uintptr_t)out->commands;
	out->header.ptrFrameData = &out->frame;
	out->header.ptrTexLayout = templateHeader->ptrTexLayout;
	out->header.ptrColors = out->colours;

	memcpy(out->model.name, source->name, sizeof(out->model.name));
	out->model.id = source->id;
	out->model.numHeaders = 1;
	out->model.headers = &out->header;
	return 1;
}

internal int BigNumber_Build(struct Model *source)
{
	struct BigNumberGlyph digits[10];

	if (source->numHeaders != BIG_NUMBER_DIGITS)
	{
		return 0;
	}
	for (int i = 1; i <= BIG_NUMBER_DIGITS; i++)
	{
		if (!BigNumber_GlyphFromHeader(&source->headers[i - 1], &digits[i]))
		{
			return 0;
		}
	}
	BigNumber_BuildNine(&digits[6], &digits[9]);
	BigNumber_BuildZero(&digits[8], &digits[0]);

	for (int i = 0; i < BIG_NUMBER_PLACES; i++)
	{
		int place = BIG_NUMBER_FIRST_PLACE + i;
		const struct BigNumberGlyph *glyphs[2];
		int count = 0;

		if (place >= 10)
		{
			glyphs[count++] = &digits[place / 10];
		}
		glyphs[count++] = &digits[place % 10];
		// The last header is the one with the far LOD distance, so any depth picks it.
		if (!BigNumber_Compose(&s_bigNumberModels[i], &source->headers[BIG_NUMBER_DIGITS - 1], source, glyphs, count, &digits[8]))
		{
			return 0;
		}
	}
	return 1;
}

struct Model *NativeBigField_PlaceModel(struct Model *source, int place)
{
	if ((source == NULL) || (place < BIG_NUMBER_FIRST_PLACE) || (place >= BIG_NUMBER_FIRST_PLACE + BIG_NUMBER_PLACES))
	{
		return NULL;
	}
	if (source->headers != s_bigNumberSource)
	{
		s_bigNumberSource = source->headers;
		s_bigNumberReady = BigNumber_Build(source);
		if (!s_bigNumberReady)
		{
			Platform_LogWarn("[CTR Native] Unexpected big number model; places from 9th show as text\n");
		}
	}
	return s_bigNumberReady ? &s_bigNumberModels[place - BIG_NUMBER_FIRST_PLACE].model : NULL;
}

int NativeBigField_IsPlaceModel(const struct Model *model)
{
	for (int i = 0; i < BIG_NUMBER_PLACES; i++)
	{
		if (model == &s_bigNumberModels[i].model)
		{
			return 1;
		}
	}
	return 0;
}
