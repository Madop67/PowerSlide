#include <platform/native_branding.h>

#include <macros.h>

#include "native_branding_data.h"
#include "platform/native_log.h"

#include <stddef.h>

global_variable unsigned char s_brandingLogo[NATIVE_BRANDING_LOGO_WIDTH * NATIVE_BRANDING_LOGO_HEIGHT * 4];
global_variable int s_brandingLogoDecoded;

// Expands (count, palette index << 6 | alpha >> 2) pairs into RGBA.
internal void NativeBranding_Decode(const unsigned char *rle, size_t rleSize, unsigned char *rgba, size_t pixelCount)
{
	size_t pixel = 0;
	for (size_t i = 0; (i + 1 < rleSize) && (pixel < pixelCount); i += 2)
	{
		const unsigned char value = rle[i + 1];
		const unsigned char *color = kNativeBrandingPalette[value >> 6];
		const unsigned char alpha = (unsigned char)(((value & 0x3f) * 255 + 31) / 63);
		for (int run = 0; (run < rle[i]) && (pixel < pixelCount); run++, pixel++)
		{
			unsigned char *out = &rgba[pixel * 4];
			out[0] = color[0];
			out[1] = color[1];
			out[2] = color[2];
			out[3] = alpha;
		}
	}
}

const unsigned char *NativeBranding_GetLogo(int *width, int *height)
{
	if (!s_brandingLogoDecoded)
	{
		NativeBranding_Decode(kNativeBrandingLogoRle, sizeof(kNativeBrandingLogoRle), s_brandingLogo,
		                      (size_t)NATIVE_BRANDING_LOGO_WIDTH * NATIVE_BRANDING_LOGO_HEIGHT);
		s_brandingLogoDecoded = 1;
	}
	*width = NATIVE_BRANDING_LOGO_WIDTH;
	*height = NATIVE_BRANDING_LOGO_HEIGHT;
	return s_brandingLogo;
}

void NativeBranding_ResampleLogo(unsigned char *out, int width, int height)
{
	int sourceWidth = 0;
	int sourceHeight = 0;
	const unsigned char *source = NativeBranding_GetLogo(&sourceWidth, &sourceHeight);

	for (int y = 0; y < height; y++)
	{
		const int y0 = y * sourceHeight / height;
		const int y1 = SDL_max((y + 1) * sourceHeight / height, y0 + 1);
		for (int x = 0; x < width; x++, out += 4)
		{
			const int x0 = x * sourceWidth / width;
			const int x1 = SDL_max((x + 1) * sourceWidth / width, x0 + 1);
			// Average with premultiplied alpha, so transparent pixels don't darken the edges.
			unsigned int sum[4] = {0, 0, 0, 0};
			for (int sy = y0; sy < y1; sy++)
			{
				for (int sx = x0; sx < x1; sx++)
				{
					const unsigned char *in = &source[(sy * sourceWidth + sx) * 4];
					sum[0] += in[0] * in[3];
					sum[1] += in[1] * in[3];
					sum[2] += in[2] * in[3];
					sum[3] += in[3];
				}
			}
			const unsigned int count = (unsigned int)((y1 - y0) * (x1 - x0));
			for (int c = 0; c < 3; c++)
			{
				out[c] = (unsigned char)((sum[3] != 0) ? (sum[c] + sum[3] / 2) / sum[3] : 0);
			}
			out[3] = (unsigned char)((sum[3] + count / 2) / count);
		}
	}
}

void NativeBranding_SetWindowIcon(SDL_Window *window)
{
	static unsigned char pixels[NATIVE_BRANDING_ICON_SIZE * NATIVE_BRANDING_ICON_SIZE * 4];

	NativeBranding_Decode(kNativeBrandingIconRle, sizeof(kNativeBrandingIconRle), pixels,
	                      (size_t)NATIVE_BRANDING_ICON_SIZE * NATIVE_BRANDING_ICON_SIZE);

	SDL_Surface *icon = SDL_CreateSurfaceFrom(NATIVE_BRANDING_ICON_SIZE, NATIVE_BRANDING_ICON_SIZE, SDL_PIXELFORMAT_RGBA32, pixels,
	                                          NATIVE_BRANDING_ICON_SIZE * 4);
	if (icon == NULL)
	{
		return;
	}
	if (!SDL_SetWindowIcon(window, icon))
	{
		Platform_LogWarn("[CTR Native] Cannot set the window icon: %s\n", SDL_GetError());
	}
	SDL_DestroySurface(icon);
}
