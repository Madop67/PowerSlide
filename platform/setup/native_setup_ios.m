// iOS file picker for first-run setup. See native_setup_ios.h.
//
// SDL_main runs on the main thread inside UIKit's run loop, so the picker is
// presented directly and the run loop is spun until it is dismissed, the way
// SDL's UIKit message box waits.

#include "native_setup_ios.h"

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <SDL3/SDL.h>

@interface NativeSetupPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property(nonatomic) BOOL done;
@property(nonatomic, strong) NSURL *url;
@end

@implementation NativeSetupPickerDelegate

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
	(void)controller;
	self.url = urls.firstObject;
	self.done = YES;
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller
{
	(void)controller;
	self.done = YES;
}

@end

static NSURL *s_nativeSetupPicked;

static UIWindowScene *NativeSetupIOS_ActiveScene(void)
{
	for (UIScene *scene in UIApplication.sharedApplication.connectedScenes)
	{
		if ((scene.activationState == UISceneActivationStateForegroundActive) && [scene isKindOfClass:[UIWindowScene class]])
		{
			return (UIWindowScene *)scene;
		}
	}

	return nil;
}

int NativeSetupIOS_PickFile(char *path, size_t pathSize)
{
	@autoreleasepool
	{
		NativeSetupIOS_ForgetPicked();

		// NOTE: asCopy gives the app its own copy, downloaded from iCloud first
		// if need be, with no security-scoped access to manage.
		UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeData ] asCopy:YES];
		NativeSetupPickerDelegate *delegate = [NativeSetupPickerDelegate new];
		picker.delegate = delegate;
		picker.allowsMultipleSelection = NO;

		// The setup window may be gone or not made yet; present from a window
		// of our own above everything else.
		UIWindowScene *scene = NativeSetupIOS_ActiveScene();
		UIWindow *window = (scene != nil) ? [[UIWindow alloc] initWithWindowScene:scene] : [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
		window.rootViewController = [UIViewController new];
		window.windowLevel = UIWindowLevelAlert;
		[window makeKeyAndVisible];
		[window.rootViewController presentViewController:picker animated:YES completion:nil];

		while (!delegate.done)
		{
			[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		}

		window.hidden = YES;

		if ((delegate.url == nil) || !delegate.url.isFileURL)
		{
			return 0;
		}

		const char *picked = delegate.url.fileSystemRepresentation;
		if (SDL_strlcpy(path, picked, pathSize) >= pathSize)
		{
			[NSFileManager.defaultManager removeItemAtURL:delegate.url error:nil];
			return 0;
		}

		s_nativeSetupPicked = delegate.url;
		return 1;
	}
}

void NativeSetupIOS_ForgetPicked(void)
{
	if (s_nativeSetupPicked != nil)
	{
		// NOTE: A disc image is up to 700 MB; don't keep a second copy around.
		[NSFileManager.defaultManager removeItemAtURL:s_nativeSetupPicked error:nil];
		s_nativeSetupPicked = nil;
	}
}
