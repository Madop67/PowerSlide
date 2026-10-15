// Disc image import for first-run setup. See disc_import.h.
//
// The game reads one layout only: a raw single-track MODE2/2352 BIN with the
// data track at byte zero (see platform/native_disc_image.c). This file turns
// a .bin, a single-track .cue or a CD .chd into exactly that, so the guest
// needs no CUE or CHD code.

#include "disc_import.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include <libchdr/cdrom.h>
#include <libchdr/chd.h>

#define DISC_IMPORT_SECTOR_SIZE       2352u
#define DISC_IMPORT_FORM1_DATA_OFFSET 24u
#define DISC_IMPORT_FORM1_DATA_SIZE   2048u
#define DISC_IMPORT_PVD_LBA           16u
#define DISC_IMPORT_PVD_VOLUME_SIZE   80u
#define DISC_IMPORT_PVD_ROOT_RECORD   156u
#define DISC_IMPORT_MAX_ROOT_SECTORS  16u
#define DISC_IMPORT_MAX_CUE_SIZE      65536u
#define DISC_IMPORT_PATH_MAX          4096
#define DISC_IMPORT_COPY_CHUNK        (DISC_IMPORT_SECTOR_SIZE * 256u)
#define DISC_IMPORT_SERIAL            "SCUS_944.26"

static void DiscImport_SetError(char *err, size_t errSize, SDL_PRINTF_FORMAT_STRING const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(3);

static void DiscImport_SetError(char *err, size_t errSize, const char *fmt, ...)
{
	va_list args;

	if ((err == NULL) || (errSize == 0))
	{
		return;
	}

	va_start(args, fmt);
	SDL_vsnprintf(err, errSize, fmt, args);
	va_end(args);
}

static uint32_t DiscImport_ReadLE32(const uint8_t *data)
{
	return ((uint32_t)data[0]) | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int DiscImport_IsContentUri(const char *path)
{
	return SDL_strncmp(path, "content://", 10) == 0;
}

static int DiscImport_HasExtension(const char *path, const char *ext)
{
	const char *dot = SDL_strrchr(path, '.');
	const char *slash = SDL_strrchr(path, '/');
	const char *backslash = SDL_strrchr(path, '\\');

	if ((backslash != NULL) && ((slash == NULL) || (backslash > slash)))
	{
		slash = backslash;
	}

	return (dot != NULL) && ((slash == NULL) || (dot > slash)) && (SDL_strcasecmp(dot + 1, ext) == 0);
}

// ---- Checking a raw BIN -----------------------------------------------------

static int DiscImport_ReadDataSector(SDL_IOStream *file, uint32_t lba, uint8_t *payload)
{
	uint8_t sector[DISC_IMPORT_SECTOR_SIZE];

	if (SDL_SeekIO(file, (Sint64)lba * DISC_IMPORT_SECTOR_SIZE, SDL_IO_SEEK_SET) < 0)
	{
		return 0;
	}

	if (SDL_ReadIO(file, sector, sizeof(sector)) != sizeof(sector))
	{
		return 0;
	}

	// Raw sync pattern (00 FF*10 00) and mode 2, as the guest's reader expects.
	if ((sector[0] != 0x00) || (sector[11] != 0x00) || (sector[15] != 0x02))
	{
		return 0;
	}

	for (int i = 1; i < 11; i++)
	{
		if (sector[i] != 0xff)
		{
			return 0;
		}
	}

	memcpy(payload, &sector[DISC_IMPORT_FORM1_DATA_OFFSET], DISC_IMPORT_FORM1_DATA_SIZE);
	return 1;
}

static int DiscImport_NameIs(const uint8_t *name, uint32_t nameLen, const char *want)
{
	size_t wantLen = SDL_strlen(want);

	if ((nameLen >= 2) && (name[nameLen - 2] == ';'))
	{
		nameLen -= 2;
	}

	if (nameLen != wantLen)
	{
		return 0;
	}

	for (size_t i = 0; i < wantLen; i++)
	{
		if (SDL_toupper(name[i]) != SDL_toupper((unsigned char)want[i]))
		{
			return 0;
		}
	}

	return 1;
}

int DiscImport_Check(const char *path, char *err, size_t errSize)
{
	uint8_t pvd[DISC_IMPORT_FORM1_DATA_SIZE];
	uint8_t *root = NULL;
	int hasBigfile = 0;
	uint32_t cnfLba = 0;
	uint32_t cnfSize = 0;
	int ok = 0;

	SDL_IOStream *file = SDL_IOFromFile(path, "rb");
	if (file == NULL)
	{
		DiscImport_SetError(err, errSize, "The disc image could not be opened.");
		return 0;
	}

	Sint64 size = SDL_GetIOSize(file);
	if ((size <= 0) || ((size % DISC_IMPORT_SECTOR_SIZE) != 0))
	{
		DiscImport_SetError(err, errSize,
		                    "This is not a raw 2352-byte-sector disc image. Use a .bin/.cue or .chd dump; a 2048-byte .iso is missing the audio and video data.");
		goto done;
	}

	if (!DiscImport_ReadDataSector(file, DISC_IMPORT_PVD_LBA, pvd) || (pvd[0] != 1) || (memcmp(&pvd[1], "CD001", 5) != 0))
	{
		DiscImport_SetError(err, errSize, "This file is not a PlayStation disc image.");
		goto done;
	}

	// The PVD's volume space size counts every sector on the disc.
	if ((uint64_t)size / DISC_IMPORT_SECTOR_SIZE < DiscImport_ReadLE32(&pvd[DISC_IMPORT_PVD_VOLUME_SIZE]))
	{
		DiscImport_SetError(err, errSize, "The disc image is damaged or truncated.");
		goto done;
	}

	const uint8_t *rootRecord = &pvd[DISC_IMPORT_PVD_ROOT_RECORD];
	uint32_t rootLba = DiscImport_ReadLE32(&rootRecord[2]);
	uint32_t rootSize = DiscImport_ReadLE32(&rootRecord[10]);
	uint32_t rootSectors = (rootSize + DISC_IMPORT_FORM1_DATA_SIZE - 1) / DISC_IMPORT_FORM1_DATA_SIZE;

	if ((rootSectors == 0) || (rootSectors > DISC_IMPORT_MAX_ROOT_SECTORS))
	{
		DiscImport_SetError(err, errSize, "This file is not a PlayStation disc image.");
		goto done;
	}

	root = malloc((size_t)rootSectors * DISC_IMPORT_FORM1_DATA_SIZE);
	if (root == NULL)
	{
		DiscImport_SetError(err, errSize, "Out of memory.");
		goto done;
	}

	for (uint32_t i = 0; i < rootSectors; i++)
	{
		if (!DiscImport_ReadDataSector(file, rootLba + i, &root[i * DISC_IMPORT_FORM1_DATA_SIZE]))
		{
			DiscImport_SetError(err, errSize, "The disc image is damaged or truncated.");
			goto done;
		}
	}

	for (uint32_t offset = 0; offset < rootSize;)
	{
		uint8_t length = root[offset];

		if (length == 0)
		{
			offset = (offset + DISC_IMPORT_FORM1_DATA_SIZE) & ~(DISC_IMPORT_FORM1_DATA_SIZE - 1);
			continue;
		}

		if ((length < 34) || (offset + length > rootSize) || (33u + root[offset + 32] > length))
		{
			break;
		}

		const uint8_t *name = &root[offset + 33];
		uint32_t nameLen = root[offset + 32];

		if (DiscImport_NameIs(name, nameLen, "BIGFILE.BIG"))
		{
			hasBigfile = 1;
		}
		else if (DiscImport_NameIs(name, nameLen, "SYSTEM.CNF"))
		{
			cnfLba = DiscImport_ReadLE32(&root[offset + 2]);
			cnfSize = DiscImport_ReadLE32(&root[offset + 10]);
		}

		offset += length;
	}

	if ((cnfSize == 0) || !hasBigfile)
	{
		DiscImport_SetError(err, errSize, "This disc is not Crash Team Racing.");
		goto done;
	}

	uint8_t cnf[DISC_IMPORT_FORM1_DATA_SIZE + 1];
	if (!DiscImport_ReadDataSector(file, cnfLba, cnf))
	{
		DiscImport_SetError(err, errSize, "The disc image is damaged or truncated.");
		goto done;
	}

	cnf[(cnfSize < DISC_IMPORT_FORM1_DATA_SIZE) ? cnfSize : DISC_IMPORT_FORM1_DATA_SIZE] = '\0';
	if (SDL_strstr((const char *)cnf, DISC_IMPORT_SERIAL) == NULL)
	{
		DiscImport_SetError(err, errSize, "This is not the North American (NTSC-U, SCUS-94426) release of Crash Team Racing.");
		goto done;
	}

	ok = 1;

done:
	free(root);
	SDL_CloseIO(file);
	return ok;
}

// ---- Writing the output -----------------------------------------------------

struct DiscImportOut
{
	SDL_IOStream *file;
	char path[DISC_IMPORT_PATH_MAX];
};

static int DiscImport_Write(struct DiscImportOut *out, const void *data, size_t size, char *err, size_t errSize)
{
	if (SDL_WriteIO(out->file, data, size) != size)
	{
		DiscImport_SetError(err, errSize, "Could not write the disc image (%s). Check that there is enough free disk space.", SDL_GetError());
		return 0;
	}

	return 1;
}

static int DiscImport_Cancelled(volatile int *cancel, char *err, size_t errSize)
{
	if ((cancel != NULL) && *cancel)
	{
		DiscImport_SetError(err, errSize, "Cancelled.");
		return 1;
	}

	return 0;
}

static int DiscImport_CopyBin(const char *src, struct DiscImportOut *out, DiscImportProgressFn progress, void *user, volatile int *cancel, char *err,
                              size_t errSize)
{
	int ok = 0;

	// Check the source first so a wrong file fails before a long copy.
	if (!DiscImport_Check(src, err, errSize))
	{
		return 0;
	}

	SDL_IOStream *in = SDL_IOFromFile(src, "rb");
	uint8_t *buffer = malloc(DISC_IMPORT_COPY_CHUNK);
	Sint64 size = (in != NULL) ? SDL_GetIOSize(in) : -1;
	if ((in == NULL) || (buffer == NULL) || (size <= 0))
	{
		DiscImport_SetError(err, errSize, "The disc image could not be opened.");
		goto done;
	}

	uint64_t total = (uint64_t)size;
	uint64_t done = 0;
	while (done < total)
	{
		size_t got = SDL_ReadIO(in, buffer, DISC_IMPORT_COPY_CHUNK);
		if (got == 0)
		{
			DiscImport_SetError(err, errSize, "Could not read the disc image.");
			goto done;
		}

		if (DiscImport_Cancelled(cancel, err, errSize) || !DiscImport_Write(out, buffer, got, err, errSize))
		{
			goto done;
		}

		done += got;
		if (progress != NULL)
		{
			progress(user, done, total);
		}
	}

	ok = 1;

done:
	free(buffer);
	if (in != NULL)
	{
		SDL_CloseIO(in);
	}
	return ok;
}

// ---- Cue sheets -------------------------------------------------------------

// A cue sheet is small text that names its tracks. Android's file picker
// hands out content:// URIs that often have no extension, so look inside.
static int DiscImport_IsCue(const char *path)
{
	if (DiscImport_HasExtension(path, "cue"))
	{
		return 1;
	}

	SDL_IOStream *file = SDL_IOFromFile(path, "rb");
	if (file == NULL)
	{
		return 0;
	}

	char text[1024];
	Sint64 size = SDL_GetIOSize(file);
	size_t got = SDL_ReadIO(file, text, sizeof(text) - 1);
	SDL_CloseIO(file);

	text[got] = '\0';
	return (size > 0) && (size <= (Sint64)DISC_IMPORT_MAX_CUE_SIZE) && (SDL_strcasestr(text, "FILE") != NULL) && (SDL_strcasestr(text, "TRACK") != NULL);
}

static int DiscImport_IsSpace(char c)
{
	return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n');
}

// Resolves the one BINARY file a single-track MODE2/2352 cue sheet names.
static int DiscImport_ResolveCue(const char *cuePath, char *binPath, size_t binPathSize, char *err, size_t errSize)
{
	char fileName[DISC_IMPORT_PATH_MAX] = "";
	int files = 0;
	int tracks = 0;
	int trackOk = 0;
	int indexOk = 0;

	// NOTE: A content:// URI grants access to the one file the user picked,
	// never to the .bin beside it.
	if (DiscImport_IsContentUri(cuePath))
	{
		DiscImport_SetError(err, errSize, "A .cue file can't be used here. Choose the .bin file that goes with it, or a .chd file.");
		return 0;
	}

	size_t textSize = 0;
	char *text = SDL_LoadFile(cuePath, &textSize);
	if ((text == NULL) || (textSize > DISC_IMPORT_MAX_CUE_SIZE))
	{
		SDL_free(text);
		DiscImport_SetError(err, errSize, "The cue sheet could not be opened.");
		return 0;
	}

	char *saveLine = NULL;
	for (char *line = SDL_strtok_r(text, "\r\n", &saveLine); line != NULL; line = SDL_strtok_r(NULL, "\r\n", &saveLine))
	{
		char *p = line;
		while (DiscImport_IsSpace(*p))
		{
			p++;
		}

		if ((SDL_strncasecmp(p, "FILE", 4) == 0) && DiscImport_IsSpace(p[4]))
		{
			files++;
			p += 5;
			while (DiscImport_IsSpace(*p))
			{
				p++;
			}

			char *end;
			if (*p == '"')
			{
				p++;
				end = SDL_strchr(p, '"');
			}
			else
			{
				end = p;
				while ((*end != '\0') && !DiscImport_IsSpace(*end))
				{
					end++;
				}
			}

			if ((end == NULL) || ((size_t)(end - p) >= sizeof(fileName)))
			{
				break;
			}

			memcpy(fileName, p, (size_t)(end - p));
			fileName[end - p] = '\0';
		}
		else if (SDL_strncasecmp(p, "TRACK", 5) == 0)
		{
			tracks++;
			trackOk = (SDL_strcasestr(p, "MODE2/2352") != NULL);
		}
		else if (SDL_strncasecmp(p, "INDEX", 5) == 0)
		{
			unsigned int number, mm, ss, ff;
			if ((SDL_sscanf(p + 5, "%u %u:%u:%u", &number, &mm, &ss, &ff) == 4) && (number == 1))
			{
				indexOk = (mm == 0) && (ss == 0) && (ff == 0);
			}
		}
	}

	SDL_free(text);

	if ((files != 1) || (tracks != 1) || !trackOk || !indexOk || (fileName[0] == '\0'))
	{
		DiscImport_SetError(err, errSize, "This cue sheet is not a single-track MODE2/2352 disc image.");
		return 0;
	}

	int absolute = (fileName[0] == '/') || (fileName[0] == '\\') || ((fileName[0] != '\0') && (fileName[1] == ':'));
	if (absolute)
	{
		SDL_snprintf(binPath, binPathSize, "%s", fileName);
		return 1;
	}

	const char *slash = SDL_strrchr(cuePath, '/');
	const char *backslash = SDL_strrchr(cuePath, '\\');
	if ((backslash != NULL) && ((slash == NULL) || (backslash > slash)))
	{
		slash = backslash;
	}

	int dirLen = (slash == NULL) ? 0 : (int)(slash - cuePath + 1);
	if (SDL_snprintf(binPath, binPathSize, "%.*s%s", dirLen, cuePath, fileName) >= (int)binPathSize)
	{
		DiscImport_SetError(err, errSize, "The disc image path is too long.");
		return 0;
	}

	return 1;
}

// ---- CHD --------------------------------------------------------------------

// libchdr reads through these, so a CHD opens from any path SDL can open.
static uint64_t DiscImport_ChdSize(void *file)
{
	Sint64 size = SDL_GetIOSize((SDL_IOStream *)file);
	return (size < 0) ? (uint64_t)-1 : (uint64_t)size;
}

static size_t DiscImport_ChdRead(void *buffer, size_t size, size_t count, void *file)
{
	return ((size == 0) || (count == 0)) ? 0 : SDL_ReadIO((SDL_IOStream *)file, buffer, size * count) / size;
}

static int DiscImport_ChdClose(void *file)
{
	return SDL_CloseIO((SDL_IOStream *)file) ? 0 : -1;
}

static int DiscImport_ChdSeek(void *file, int64_t offset, int whence)
{
	SDL_IOWhence sdlWhence = (whence == SEEK_CUR) ? SDL_IO_SEEK_CUR : ((whence == SEEK_END) ? SDL_IO_SEEK_END : SDL_IO_SEEK_SET);
	return (SDL_SeekIO((SDL_IOStream *)file, offset, sdlWhence) < 0) ? -1 : 0;
}

static const core_file_callbacks s_discImportChdCallbacks = {
	DiscImport_ChdSize,
	DiscImport_ChdRead,
	DiscImport_ChdClose,
	DiscImport_ChdSeek,
};

static int DiscImport_IsChd(const char *path)
{
	char magic[8];
	int isChd = 0;

	SDL_IOStream *file = SDL_IOFromFile(path, "rb");
	if (file != NULL)
	{
		isChd = (SDL_ReadIO(file, magic, sizeof(magic)) == sizeof(magic)) && (memcmp(magic, "MComprHD", 8) == 0);
		SDL_CloseIO(file);
	}

	return isChd;
}

static int DiscImport_ConvertChd(const char *src, struct DiscImportOut *out, DiscImportProgressFn progress, void *user, volatile int *cancel, char *err,
                                 size_t errSize)
{
	chd_file *chd = NULL;
	uint8_t *hunk = NULL;
	char meta[256];
	char type[64], subtype[64], pgtype[64], pgsub[64];
	int track = 0, frames = 0, pregap = 0, postgap = 0;
	int ok = 0;

	SDL_IOStream *file = SDL_IOFromFile(src, "rb");
	if (file == NULL)
	{
		DiscImport_SetError(err, errSize, "The CHD file could not be opened.");
		return 0;
	}

	// NOTE: libchdr takes the stream; it closes it on failure and in chd_close.
	chd_error result = chd_open_core_file_callbacks(&s_discImportChdCallbacks, file, CHD_OPEN_READ, NULL, &chd);
	if (result != CHDERR_NONE)
	{
		DiscImport_SetError(err, errSize, "The CHD file could not be opened (%s).", chd_error_string(result));
		return 0;
	}

	const chd_header *header = chd_get_header(chd);
	uint32_t framesPerHunk = header->hunkbytes / CD_FRAME_SIZE;

	memset(meta, 0, sizeof(meta));
	if (chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, 0, meta, sizeof(meta) - 1, NULL, NULL, NULL) == CHDERR_NONE)
	{
		if (SDL_sscanf(meta, CDROM_TRACK_METADATA2_FORMAT, &track, type, subtype, &frames, &pregap, pgtype, pgsub, &postgap) != 8)
		{
			frames = 0;
		}
	}
	else if (chd_get_metadata(chd, CDROM_TRACK_METADATA_TAG, 0, meta, sizeof(meta) - 1, NULL, NULL, NULL) == CHDERR_NONE)
	{
		if (SDL_sscanf(meta, CDROM_TRACK_METADATA_FORMAT, &track, type, subtype, &frames) != 4)
		{
			frames = 0;
		}
	}
	else
	{
		DiscImport_SetError(err, errSize, "This CHD file is not a CD image.");
		goto done;
	}

	int extraTracks = (chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, 1, meta, sizeof(meta) - 1, NULL, NULL, NULL) == CHDERR_NONE) ||
	                  (chd_get_metadata(chd, CDROM_TRACK_METADATA_TAG, 1, meta, sizeof(meta) - 1, NULL, NULL, NULL) == CHDERR_NONE);

	if ((frames <= 0) || (track != 1) || extraTracks || (SDL_strcmp(type, "MODE2_RAW") != 0) || (pregap != 0) || (framesPerHunk == 0) ||
	    (header->hunkbytes % CD_FRAME_SIZE != 0) || ((uint64_t)frames > (uint64_t)header->totalhunks * framesPerHunk))
	{
		DiscImport_SetError(err, errSize, "This CHD file is not a single-track MODE2/2352 disc image.");
		goto done;
	}

	hunk = malloc(header->hunkbytes);
	if (hunk == NULL)
	{
		DiscImport_SetError(err, errSize, "Out of memory.");
		goto done;
	}

	uint32_t loadedHunk = UINT32_MAX;
	for (uint32_t frame = 0; frame < (uint32_t)frames; frame++)
	{
		uint32_t hunkIndex = frame / framesPerHunk;

		if (hunkIndex != loadedHunk)
		{
			if (DiscImport_Cancelled(cancel, err, errSize))
			{
				goto done;
			}

			result = chd_read(chd, hunkIndex, hunk);
			if (result != CHDERR_NONE)
			{
				DiscImport_SetError(err, errSize, "The CHD file is damaged (%s).", chd_error_string(result));
				goto done;
			}

			loadedHunk = hunkIndex;
		}

		// Each frame is 2352 bytes of sector data followed by 96 bytes of subcode.
		if (!DiscImport_Write(out, &hunk[(frame % framesPerHunk) * CD_FRAME_SIZE], CD_MAX_SECTOR_DATA, err, errSize))
		{
			goto done;
		}

		if ((progress != NULL) && (((frame + 1) % 1024 == 0) || (frame + 1 == (uint32_t)frames)))
		{
			progress(user, frame + 1, (uint64_t)frames);
		}
	}

	ok = 1;

done:
	free(hunk);
	chd_close(chd);
	return ok;
}

// ---- Import -----------------------------------------------------------------

int DiscImport_Run(const char *src, const char *assetsDir, DiscImportProgressFn progress, void *user, volatile int *cancel, char *err, size_t errSize)
{
	struct DiscImportOut out;
	char finalPath[DISC_IMPORT_PATH_MAX];
	char binPath[DISC_IMPORT_PATH_MAX];
	int ok;

	if (!SDL_CreateDirectory(assetsDir))
	{
		DiscImport_SetError(err, errSize, "Could not create %s: %s.", assetsDir, SDL_GetError());
		return 0;
	}

	if ((SDL_snprintf(finalPath, sizeof(finalPath), "%s/%s", assetsDir, DISC_IMPORT_BIN_NAME) >= (int)sizeof(finalPath)) ||
	    (SDL_snprintf(out.path, sizeof(out.path), "%s.part", finalPath) >= (int)sizeof(out.path)))
	{
		DiscImport_SetError(err, errSize, "The data folder path is too long.");
		return 0;
	}

	out.file = SDL_IOFromFile(out.path, "wb");
	if (out.file == NULL)
	{
		DiscImport_SetError(err, errSize, "Could not write %s: %s.", out.path, SDL_GetError());
		return 0;
	}

	if (DiscImport_IsChd(src))
	{
		ok = DiscImport_ConvertChd(src, &out, progress, user, cancel, err, errSize);
	}
	else if (DiscImport_IsCue(src))
	{
		ok = DiscImport_ResolveCue(src, binPath, sizeof(binPath), err, errSize) && DiscImport_CopyBin(binPath, &out, progress, user, cancel, err, errSize);
	}
	else
	{
		ok = DiscImport_CopyBin(src, &out, progress, user, cancel, err, errSize);
	}

	if (!SDL_CloseIO(out.file) && ok)
	{
		DiscImport_SetError(err, errSize, "Could not write the disc image (%s). Check that there is enough free disk space.", SDL_GetError());
		ok = 0;
	}

	ok = ok && DiscImport_Check(out.path, err, errSize);

	// NOTE: SDL_RenamePath replaces an existing file on every platform,
	// Windows included.
	if (ok && !SDL_RenamePath(out.path, finalPath))
	{
		DiscImport_SetError(err, errSize, "Could not save %s: %s.", finalPath, SDL_GetError());
		ok = 0;
	}

	if (!ok)
	{
		SDL_RemovePath(out.path);
	}

	return ok;
}
