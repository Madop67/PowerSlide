// Disc image import for the macOS app bundle's first-run setup.
//
// The game reads one layout only: a raw single-track MODE2/2352 BIN with the
// data track at byte zero (see platform/native_disc_image.c). This file turns
// a .bin, a single-track .cue or a CD .chd into exactly that, so the guest
// needs no CUE or CHD code.

#include "host_disc_import.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libchdr/cdrom.h>
#include <libchdr/chd.h>

#define HOST_DISC_SECTOR_SIZE       2352u
#define HOST_DISC_FORM1_DATA_OFFSET 24u
#define HOST_DISC_FORM1_DATA_SIZE   2048u
#define HOST_DISC_PVD_LBA           16u
#define HOST_DISC_PVD_VOLUME_SIZE   80u
#define HOST_DISC_PVD_ROOT_RECORD   156u
#define HOST_DISC_MAX_ROOT_SECTORS  16u
#define HOST_DISC_PATH_MAX          4096
#define HOST_DISC_COPY_CHUNK        (HOST_DISC_SECTOR_SIZE * 256u)
#define HOST_DISC_SERIAL            "SCUS_944.26"

__attribute__((format(printf, 3, 4))) static void HostDiscImport_SetError(char *err, size_t errSize, const char *fmt, ...)
{
	va_list args;

	if ((err == NULL) || (errSize == 0))
	{
		return;
	}

	va_start(args, fmt);
	vsnprintf(err, errSize, fmt, args);
	va_end(args);
}

static void HostDiscImport_SetWriteError(char *err, size_t errSize)
{
	if (errno == ENOSPC)
	{
		HostDiscImport_SetError(err, errSize, "There is not enough free disk space.");
	}
	else
	{
		HostDiscImport_SetError(err, errSize, "Could not write the disc image: %s.", strerror(errno));
	}
}

static uint32_t HostDiscImport_ReadLE32(const uint8_t *data)
{
	return ((uint32_t)data[0]) | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int HostDiscImport_HasExtension(const char *path, const char *ext)
{
	const char *dot = strrchr(path, '.');
	const char *slash = strrchr(path, '/');

	return (dot != NULL) && ((slash == NULL) || (dot > slash)) && (strcasecmp(dot + 1, ext) == 0);
}

// ---- Checking a raw BIN -----------------------------------------------------

static int HostDiscImport_ReadDataSector(FILE *file, uint32_t lba, uint8_t *payload)
{
	uint8_t sector[HOST_DISC_SECTOR_SIZE];

	if (fseeko(file, (off_t)lba * HOST_DISC_SECTOR_SIZE, SEEK_SET) != 0)
	{
		return 0;
	}

	if (fread(sector, 1, sizeof(sector), file) != sizeof(sector))
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

	memcpy(payload, &sector[HOST_DISC_FORM1_DATA_OFFSET], HOST_DISC_FORM1_DATA_SIZE);
	return 1;
}

static int HostDiscImport_NameIs(const uint8_t *name, uint32_t nameLen, const char *want)
{
	size_t wantLen = strlen(want);

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
		if (toupper(name[i]) != toupper((unsigned char)want[i]))
		{
			return 0;
		}
	}

	return 1;
}

int HostDiscImport_Check(const char *path, char *err, size_t errSize)
{
	uint8_t pvd[HOST_DISC_FORM1_DATA_SIZE];
	uint8_t *root = NULL;
	struct stat st;
	int hasBigfile = 0;
	uint32_t cnfLba = 0;
	uint32_t cnfSize = 0;
	int ok = 0;

	if ((stat(path, &st) != 0) || !S_ISREG(st.st_mode))
	{
		HostDiscImport_SetError(err, errSize, "The disc image could not be opened.");
		return 0;
	}

	if ((st.st_size == 0) || ((st.st_size % HOST_DISC_SECTOR_SIZE) != 0))
	{
		HostDiscImport_SetError(err, errSize,
		                        "This is not a raw 2352-byte-sector disc image. Use a .bin/.cue or .chd dump; a 2048-byte .iso is missing the audio and video data.");
		return 0;
	}

	FILE *file = fopen(path, "rb");
	if (file == NULL)
	{
		HostDiscImport_SetError(err, errSize, "The disc image could not be opened.");
		return 0;
	}

	if (!HostDiscImport_ReadDataSector(file, HOST_DISC_PVD_LBA, pvd) || (pvd[0] != 1) || (memcmp(&pvd[1], "CD001", 5) != 0))
	{
		HostDiscImport_SetError(err, errSize, "This file is not a PlayStation disc image.");
		goto done;
	}

	// The PVD's volume space size counts every sector on the disc.
	if ((uint64_t)st.st_size / HOST_DISC_SECTOR_SIZE < HostDiscImport_ReadLE32(&pvd[HOST_DISC_PVD_VOLUME_SIZE]))
	{
		HostDiscImport_SetError(err, errSize, "The disc image is damaged or truncated.");
		goto done;
	}

	const uint8_t *rootRecord = &pvd[HOST_DISC_PVD_ROOT_RECORD];
	uint32_t rootLba = HostDiscImport_ReadLE32(&rootRecord[2]);
	uint32_t rootSize = HostDiscImport_ReadLE32(&rootRecord[10]);
	uint32_t rootSectors = (rootSize + HOST_DISC_FORM1_DATA_SIZE - 1) / HOST_DISC_FORM1_DATA_SIZE;

	if ((rootSectors == 0) || (rootSectors > HOST_DISC_MAX_ROOT_SECTORS))
	{
		HostDiscImport_SetError(err, errSize, "This file is not a PlayStation disc image.");
		goto done;
	}

	root = malloc((size_t)rootSectors * HOST_DISC_FORM1_DATA_SIZE);
	if (root == NULL)
	{
		HostDiscImport_SetError(err, errSize, "Out of memory.");
		goto done;
	}

	for (uint32_t i = 0; i < rootSectors; i++)
	{
		if (!HostDiscImport_ReadDataSector(file, rootLba + i, &root[i * HOST_DISC_FORM1_DATA_SIZE]))
		{
			HostDiscImport_SetError(err, errSize, "The disc image is damaged or truncated.");
			goto done;
		}
	}

	for (uint32_t offset = 0; offset < rootSize;)
	{
		uint8_t length = root[offset];

		if (length == 0)
		{
			offset = (offset + HOST_DISC_FORM1_DATA_SIZE) & ~(HOST_DISC_FORM1_DATA_SIZE - 1);
			continue;
		}

		if ((length < 34) || (offset + length > rootSize) || (33u + root[offset + 32] > length))
		{
			break;
		}

		const uint8_t *name = &root[offset + 33];
		uint32_t nameLen = root[offset + 32];

		if (HostDiscImport_NameIs(name, nameLen, "BIGFILE.BIG"))
		{
			hasBigfile = 1;
		}
		else if (HostDiscImport_NameIs(name, nameLen, "SYSTEM.CNF"))
		{
			cnfLba = HostDiscImport_ReadLE32(&root[offset + 2]);
			cnfSize = HostDiscImport_ReadLE32(&root[offset + 10]);
		}

		offset += length;
	}

	if ((cnfSize == 0) || !hasBigfile)
	{
		HostDiscImport_SetError(err, errSize, "This disc is not Crash Team Racing.");
		goto done;
	}

	uint8_t cnf[HOST_DISC_FORM1_DATA_SIZE + 1];
	if (!HostDiscImport_ReadDataSector(file, cnfLba, cnf))
	{
		HostDiscImport_SetError(err, errSize, "The disc image is damaged or truncated.");
		goto done;
	}

	cnf[(cnfSize < HOST_DISC_FORM1_DATA_SIZE) ? cnfSize : HOST_DISC_FORM1_DATA_SIZE] = '\0';
	if (strstr((const char *)cnf, HOST_DISC_SERIAL) == NULL)
	{
		HostDiscImport_SetError(err, errSize, "This is not the North American (NTSC-U, SCUS-94426) release of Crash Team Racing.");
		goto done;
	}

	ok = 1;

done:
	free(root);
	fclose(file);
	return ok;
}

// ---- Writing the output -----------------------------------------------------

struct HostDiscImportOut
{
	FILE *file;
	char path[HOST_DISC_PATH_MAX];
};

static int HostDiscImport_Write(struct HostDiscImportOut *out, const void *data, size_t size, char *err, size_t errSize)
{
	if (fwrite(data, 1, size, out->file) != size)
	{
		HostDiscImport_SetWriteError(err, errSize);
		return 0;
	}

	return 1;
}

static int HostDiscImport_Cancelled(volatile int *cancel, char *err, size_t errSize)
{
	if ((cancel != NULL) && *cancel)
	{
		HostDiscImport_SetError(err, errSize, "Cancelled.");
		return 1;
	}

	return 0;
}

static int HostDiscImport_CopyBin(const char *src, struct HostDiscImportOut *out, HostDiscImportProgressFn progress, void *user, volatile int *cancel,
                                  char *err, size_t errSize)
{
	struct stat st;
	int ok = 0;

	// Check the source first so a wrong file fails before a long copy.
	if (!HostDiscImport_Check(src, err, errSize) || (stat(src, &st) != 0))
	{
		return 0;
	}

	FILE *in = fopen(src, "rb");
	uint8_t *buffer = malloc(HOST_DISC_COPY_CHUNK);
	if ((in == NULL) || (buffer == NULL))
	{
		HostDiscImport_SetError(err, errSize, "The disc image could not be opened.");
		goto done;
	}

	uint64_t total = (uint64_t)st.st_size;
	uint64_t done = 0;
	while (done < total)
	{
		size_t got = fread(buffer, 1, HOST_DISC_COPY_CHUNK, in);
		if (got == 0)
		{
			HostDiscImport_SetError(err, errSize, "Could not read the disc image.");
			goto done;
		}

		if (HostDiscImport_Cancelled(cancel, err, errSize) || !HostDiscImport_Write(out, buffer, got, err, errSize))
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
		fclose(in);
	}
	return ok;
}

// Resolves the one BINARY file a single-track MODE2/2352 cue sheet names.
static int HostDiscImport_ResolveCue(const char *cuePath, char *binPath, size_t binPathSize, char *err, size_t errSize)
{
	char line[1024];
	char fileName[HOST_DISC_PATH_MAX] = "";
	int files = 0;
	int tracks = 0;
	int trackOk = 0;
	int indexOk = 0;

	FILE *cue = fopen(cuePath, "r");
	if (cue == NULL)
	{
		HostDiscImport_SetError(err, errSize, "The cue sheet could not be opened.");
		return 0;
	}

	while (fgets(line, sizeof(line), cue) != NULL)
	{
		char *p = line;
		while (isspace((unsigned char)*p))
		{
			p++;
		}

		if (strncasecmp(p, "FILE", 4) == 0 && isspace((unsigned char)p[4]))
		{
			files++;
			p += 5;
			while (isspace((unsigned char)*p))
			{
				p++;
			}

			char *end;
			if (*p == '"')
			{
				p++;
				end = strchr(p, '"');
			}
			else
			{
				end = p;
				while ((*end != '\0') && !isspace((unsigned char)*end))
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
		else if (strncasecmp(p, "TRACK", 5) == 0)
		{
			tracks++;
			trackOk = (strcasestr(p, "MODE2/2352") != NULL);
		}
		else if (strncasecmp(p, "INDEX", 5) == 0)
		{
			unsigned int number, mm, ss, ff;
			if ((sscanf(p + 5, "%u %u:%u:%u", &number, &mm, &ss, &ff) == 4) && (number == 1))
			{
				indexOk = (mm == 0) && (ss == 0) && (ff == 0);
			}
		}
	}

	fclose(cue);

	if ((files != 1) || (tracks != 1) || !trackOk || !indexOk || (fileName[0] == '\0'))
	{
		HostDiscImport_SetError(err, errSize, "This cue sheet is not a single-track MODE2/2352 disc image.");
		return 0;
	}

	if (fileName[0] == '/')
	{
		snprintf(binPath, binPathSize, "%s", fileName);
		return 1;
	}

	const char *slash = strrchr(cuePath, '/');
	int dirLen = (slash == NULL) ? 0 : (int)(slash - cuePath + 1);
	if (snprintf(binPath, binPathSize, "%.*s%s", dirLen, cuePath, fileName) >= (int)binPathSize)
	{
		HostDiscImport_SetError(err, errSize, "The disc image path is too long.");
		return 0;
	}

	return 1;
}

static int HostDiscImport_ConvertChd(const char *src, struct HostDiscImportOut *out, HostDiscImportProgressFn progress, void *user,
                                     volatile int *cancel, char *err, size_t errSize)
{
	chd_file *chd = NULL;
	uint8_t *hunk = NULL;
	char meta[256];
	char type[64], subtype[64], pgtype[64], pgsub[64];
	int track = 0, frames = 0, pregap = 0, postgap = 0;
	int ok = 0;

	chd_error result = chd_open(src, CHD_OPEN_READ, NULL, &chd);
	if (result != CHDERR_NONE)
	{
		HostDiscImport_SetError(err, errSize, "The CHD file could not be opened (%s).", chd_error_string(result));
		return 0;
	}

	const chd_header *header = chd_get_header(chd);
	uint32_t framesPerHunk = header->hunkbytes / CD_FRAME_SIZE;

	memset(meta, 0, sizeof(meta));
	if (chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, 0, meta, sizeof(meta) - 1, NULL, NULL, NULL) == CHDERR_NONE)
	{
		if (sscanf(meta, CDROM_TRACK_METADATA2_FORMAT, &track, type, subtype, &frames, &pregap, pgtype, pgsub, &postgap) != 8)
		{
			frames = 0;
		}
	}
	else if (chd_get_metadata(chd, CDROM_TRACK_METADATA_TAG, 0, meta, sizeof(meta) - 1, NULL, NULL, NULL) == CHDERR_NONE)
	{
		if (sscanf(meta, CDROM_TRACK_METADATA_FORMAT, &track, type, subtype, &frames) != 4)
		{
			frames = 0;
		}
	}
	else
	{
		HostDiscImport_SetError(err, errSize, "This CHD file is not a CD image.");
		goto done;
	}

	int extraTracks = (chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, 1, meta, sizeof(meta) - 1, NULL, NULL, NULL) == CHDERR_NONE) ||
	                  (chd_get_metadata(chd, CDROM_TRACK_METADATA_TAG, 1, meta, sizeof(meta) - 1, NULL, NULL, NULL) == CHDERR_NONE);

	if ((frames <= 0) || (track != 1) || extraTracks || (strcmp(type, "MODE2_RAW") != 0) || (pregap != 0) || (framesPerHunk == 0) ||
	    (header->hunkbytes % CD_FRAME_SIZE != 0) || ((uint64_t)frames > (uint64_t)header->totalhunks * framesPerHunk))
	{
		HostDiscImport_SetError(err, errSize, "This CHD file is not a single-track MODE2/2352 disc image.");
		goto done;
	}

	hunk = malloc(header->hunkbytes);
	if (hunk == NULL)
	{
		HostDiscImport_SetError(err, errSize, "Out of memory.");
		goto done;
	}

	uint32_t loadedHunk = UINT32_MAX;
	for (uint32_t frame = 0; frame < (uint32_t)frames; frame++)
	{
		uint32_t hunkIndex = frame / framesPerHunk;

		if (hunkIndex != loadedHunk)
		{
			if (HostDiscImport_Cancelled(cancel, err, errSize))
			{
				goto done;
			}

			result = chd_read(chd, hunkIndex, hunk);
			if (result != CHDERR_NONE)
			{
				HostDiscImport_SetError(err, errSize, "The CHD file is damaged (%s).", chd_error_string(result));
				goto done;
			}

			loadedHunk = hunkIndex;
		}

		// Each frame is 2352 bytes of sector data followed by 96 bytes of subcode.
		if (!HostDiscImport_Write(out, &hunk[(frame % framesPerHunk) * CD_FRAME_SIZE], CD_MAX_SECTOR_DATA, err, errSize))
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

static int HostDiscImport_IsChd(const char *path)
{
	char magic[8];
	int isChd = 0;

	FILE *file = fopen(path, "rb");
	if (file != NULL)
	{
		isChd = (fread(magic, 1, sizeof(magic), file) == sizeof(magic)) && (memcmp(magic, "MComprHD", 8) == 0);
		fclose(file);
	}

	return isChd;
}

int HostDiscImport_Run(const char *src, const char *assetsDir, HostDiscImportProgressFn progress, void *user, volatile int *cancel, char *err,
                       size_t errSize)
{
	struct HostDiscImportOut out;
	char finalPath[HOST_DISC_PATH_MAX];
	char binPath[HOST_DISC_PATH_MAX];
	int ok;

	if ((mkdir(assetsDir, 0755) != 0) && (errno != EEXIST))
	{
		HostDiscImport_SetError(err, errSize, "Could not create %s: %s.", assetsDir, strerror(errno));
		return 0;
	}

	if ((snprintf(finalPath, sizeof(finalPath), "%s/%s", assetsDir, HOST_DISC_IMPORT_BIN_NAME) >= (int)sizeof(finalPath)) ||
	    (snprintf(out.path, sizeof(out.path), "%s.part", finalPath) >= (int)sizeof(out.path)))
	{
		HostDiscImport_SetError(err, errSize, "The data folder path is too long.");
		return 0;
	}

	out.file = fopen(out.path, "wb");
	if (out.file == NULL)
	{
		HostDiscImport_SetError(err, errSize, "Could not write %s: %s.", out.path, strerror(errno));
		return 0;
	}

	if (HostDiscImport_IsChd(src))
	{
		ok = HostDiscImport_ConvertChd(src, &out, progress, user, cancel, err, errSize);
	}
	else if (HostDiscImport_HasExtension(src, "cue"))
	{
		ok = HostDiscImport_ResolveCue(src, binPath, sizeof(binPath), err, errSize) &&
		     HostDiscImport_CopyBin(binPath, &out, progress, user, cancel, err, errSize);
	}
	else
	{
		ok = HostDiscImport_CopyBin(src, &out, progress, user, cancel, err, errSize);
	}

	if ((fclose(out.file) != 0) && ok)
	{
		HostDiscImport_SetWriteError(err, errSize);
		ok = 0;
	}

	ok = ok && HostDiscImport_Check(out.path, err, errSize);

	if (ok && (rename(out.path, finalPath) != 0))
	{
		HostDiscImport_SetError(err, errSize, "Could not save %s: %s.", finalPath, strerror(errno));
		ok = 0;
	}

	if (!ok)
	{
		unlink(out.path);
	}

	return ok;
}
