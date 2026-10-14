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
