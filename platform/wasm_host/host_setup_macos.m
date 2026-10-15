// First-run setup window for the macOS app bundle. See host_setup_macos.h.
//
// NOTE: If this window runs, it creates NSApp before SDL does. SDL3 then skips
// its own menus and app subclass and dispatches events from SDL_PumpEvents
// instead, so the standard app menu is built here and kept for the game.

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "host_disc_import.h"
#include "host_setup_macos.h"

#include "platform/native_product.h"

#define HOST_SETUP_PRODUCT @NATIVE_PRODUCT_NAME

// Window width 540 minus 20 pt margins, the 64 pt icon and the 20 pt gap.
#define HOST_SETUP_COLUMN_WIDTH 416

static NSString *HostSetup_AssetsDir(NSString *baseDir)
{
	return [baseDir stringByAppendingPathComponent:@"assets"];
}

static NSString *HostSetup_DiscPath(NSString *baseDir)
{
	return [HostSetup_AssetsDir(baseDir) stringByAppendingPathComponent:@HOST_DISC_IMPORT_BIN_NAME];
}

static int HostSetup_HasUsableDisc(NSString *baseDir)
{
	char err[256];
	return HostDiscImport_Check(HostSetup_DiscPath(baseDir).fileSystemRepresentation, err, sizeof(err));
}

static NSTextField *HostSetup_Label(NSString *text, NSFont *font)
{
	NSTextField *label = [NSTextField wrappingLabelWithString:text];
	label.font = font;
	label.selectable = NO;
	return label;
}

// ---- Controller -------------------------------------------------------------

@interface HostSetupController : NSObject <NSWindowDelegate>
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSTextField *pathField;
@property(nonatomic, strong) NSTextField *statusLabel;
@property(nonatomic, strong) NSProgressIndicator *progress;
@property(nonatomic, strong) NSButton *chooseButton;
@property(nonatomic, strong) NSButton *installButton;
@property(nonatomic, strong) NSButton *quitButton;
@property(nonatomic, copy) NSString *baseDir;
@property(nonatomic, copy) NSString *sourcePath;
@property(nonatomic) BOOL hasExistingDisc;
@property(nonatomic) BOOL busy;
@property(nonatomic) BOOL quitRequested;
@end

@implementation HostSetupController
{
	volatile int _cancel;
}

- (instancetype)initWithBaseDir:(NSString *)baseDir hasExistingDisc:(BOOL)hasExistingDisc
{
	self = [super init];
	if (self != nil)
	{
		_baseDir = [baseDir copy];
		_hasExistingDisc = hasExistingDisc;
		[self buildWindow];
	}
	return self;
}

- (void)buildWindow
{
	self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 540, 300)
	                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
	                                            backing:NSBackingStoreBuffered
	                                              defer:NO];
	self.window.title = HOST_SETUP_PRODUCT @" Setup";
	self.window.delegate = self;
	self.window.releasedWhenClosed = NO;

	NSImageView *icon = [NSImageView imageViewWithImage:NSApp.applicationIconImage];
	[icon.widthAnchor constraintEqualToConstant:64].active = YES;
	[icon.heightAnchor constraintEqualToConstant:64].active = YES;

	NSTextField *heading = HostSetup_Label(@"Set up " HOST_SETUP_PRODUCT, [NSFont boldSystemFontOfSize:17]);
	NSString *bodyText = self.hasExistingDisc
	                         ? @"Choose a different disc image to replace the one " HOST_SETUP_PRODUCT @" uses now, or click Cancel to keep it."
	                         : HOST_SETUP_PRODUCT @" needs your own copy of Crash Team Racing for the PlayStation (North America, "
	                                               @"SCUS-94426). Choose a disc image: a .cue/.bin pair or a .chd file. "
	                                               @"It is copied to your Library folder, so you only do this once.";
	NSTextField *body = HostSetup_Label(bodyText, [NSFont systemFontOfSize:NSFont.systemFontSize]);

	self.pathField = [NSTextField textFieldWithString:@""];
	self.pathField.editable = NO;
	self.pathField.placeholderString = @"No disc image chosen";
	self.pathField.lineBreakMode = NSLineBreakByTruncatingMiddle;
	[self.pathField setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];

	self.chooseButton = [NSButton buttonWithTitle:@"Choose…" target:self action:@selector(choose:)];

	NSStackView *pathRow = [NSStackView stackViewWithViews:@[ self.pathField, self.chooseButton ]];
	pathRow.spacing = 8;

	self.progress = [[NSProgressIndicator alloc] init];
	self.progress.style = NSProgressIndicatorStyleBar;
	self.progress.indeterminate = NO;
	self.progress.minValue = 0;
	self.progress.maxValue = 1;
	// NOTE: Transparent rather than hidden, so showing it never resizes the window.
	self.progress.alphaValue = 0;

	self.statusLabel = HostSetup_Label(@"", [NSFont systemFontOfSize:NSFont.smallSystemFontSize]);
	self.statusLabel.textColor = NSColor.secondaryLabelColor;

	self.quitButton = [NSButton buttonWithTitle:(self.hasExistingDisc ? @"Cancel" : @"Quit") target:self action:@selector(quit:)];
	self.quitButton.keyEquivalent = @"\033";
	self.installButton = [NSButton buttonWithTitle:@"Install" target:self action:@selector(install:)];
	self.installButton.keyEquivalent = @"\r";
	self.installButton.enabled = NO;

	NSView *spacer = [[NSView alloc] init];
	[spacer setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
	NSStackView *buttonRow = [NSStackView stackViewWithViews:@[ spacer, self.quitButton, self.installButton ]];
	buttonRow.spacing = 12;

	NSStackView *column = [NSStackView stackViewWithViews:@[ heading, body, pathRow, self.progress, self.statusLabel, buttonRow ]];
	column.orientation = NSUserInterfaceLayoutOrientationVertical;
	column.alignment = NSLayoutAttributeLeading;
	column.spacing = 12;
	[column setCustomSpacing:16 afterView:body];
	// NOTE: Wrapping labels have no width of their own; pin the column so the
	// text wraps at the window width instead of collapsing it.
	[column.widthAnchor constraintEqualToConstant:HOST_SETUP_COLUMN_WIDTH].active = YES;
	for (NSTextField *label in @[ heading, body, self.statusLabel ])
	{
		label.preferredMaxLayoutWidth = HOST_SETUP_COLUMN_WIDTH;
	}
	for (NSView *view in @[ body, pathRow, self.progress, self.statusLabel, buttonRow ])
	{
		[view.widthAnchor constraintEqualToAnchor:column.widthAnchor].active = YES;
	}

	NSStackView *root = [NSStackView stackViewWithViews:@[ icon, column ]];
	root.alignment = NSLayoutAttributeTop;
	root.spacing = 20;
	root.edgeInsets = NSEdgeInsetsMake(20, 20, 20, 20);
	root.translatesAutoresizingMaskIntoConstraints = NO;

	NSView *content = self.window.contentView;
	[content addSubview:root];
	[NSLayoutConstraint activateConstraints:@[
		[root.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
		[root.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
		[root.topAnchor constraintEqualToAnchor:content.topAnchor],
		[root.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
	]];

	// NOTE: The outer stack under-reports its fitting height, so size the
	// window from the column (the taller side) plus the margins.
	[content layoutSubtreeIfNeeded];
	CGFloat height = MAX(column.fittingSize.height, 64) + root.edgeInsets.top + root.edgeInsets.bottom;
	[self.window setContentSize:NSMakeSize(root.fittingSize.width, height)];
	[self.window center];
}

- (void)setStatus:(NSString *)text isError:(BOOL)isError
{
	self.statusLabel.stringValue = text;
	self.statusLabel.textColor = isError ? NSColor.systemRedColor : NSColor.secondaryLabelColor;
}

- (void)choose:(id)sender
{
	(void)sender;

	NSOpenPanel *panel = [NSOpenPanel openPanel];
	panel.canChooseFiles = YES;
	panel.canChooseDirectories = NO;
	panel.allowsMultipleSelection = NO;
	panel.message = @"Choose a Crash Team Racing (NTSC-U) disc image: .cue, .bin or .chd";

	NSMutableArray<UTType *> *types = [NSMutableArray array];
	for (NSString *ext in @[ @"cue", @"bin", @"chd" ])
	{
		UTType *type = [UTType typeWithFilenameExtension:ext];
		if (type != nil)
		{
			[types addObject:type];
		}
	}
	panel.allowedContentTypes = types;

	[panel beginSheetModalForWindow:self.window
	              completionHandler:^(NSModalResponse response) {
		              if ((response != NSModalResponseOK) || (panel.URL == nil))
		              {
			              return;
		              }

		              self.sourcePath = panel.URL.path;
		              self.pathField.stringValue = self.sourcePath;
		              self.installButton.enabled = YES;
		              [self setStatus:@"" isError:NO];
	              }];
}

- (void)setBusy:(BOOL)busy
{
	_busy = busy;
	self.chooseButton.enabled = !busy;
	self.installButton.enabled = !busy && (self.sourcePath != nil);
	self.progress.alphaValue = busy ? 1 : 0;
}

static void HostSetup_Progress(void *user, uint64_t done, uint64_t total)
{
	HostSetupController *controller = (__bridge HostSetupController *)user;
	double fraction = (total != 0) ? (double)done / (double)total : 0.0;

	dispatch_async(dispatch_get_main_queue(), ^{
		controller.progress.doubleValue = fraction;
	});
}

- (void)install:(id)sender
{
	(void)sender;

	if ((self.sourcePath == nil) || self.busy)
	{
		return;
	}

	NSString *source = [self.sourcePath copy];
	NSString *assetsDir = HostSetup_AssetsDir(self.baseDir);

	_cancel = 0;
	self.progress.doubleValue = 0;
	self.busy = YES;
	[self setStatus:@"Installing the disc image…" isError:NO];

	dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
		char err[512] = "";
		int ok = HostDiscImport_Run(source.fileSystemRepresentation, assetsDir.fileSystemRepresentation, HostSetup_Progress, (__bridge void *)self,
		                            &self->_cancel, err, sizeof(err));
		NSString *message = [NSString stringWithUTF8String:err];

		dispatch_async(dispatch_get_main_queue(), ^{
			self.busy = NO;

			if (self.quitRequested)
			{
				[NSApp stopModalWithCode:NSModalResponseCancel];
			}
			else if (ok)
			{
				[NSApp stopModalWithCode:NSModalResponseOK];
			}
			else
			{
				[self setStatus:(message.length != 0 ? message : @"The disc image could not be installed.") isError:YES];
			}
		});
	});
}

- (void)quit:(id)sender
{
	(void)sender;

	if (self.busy)
	{
		// The worker stops at its next chunk and removes its partial file.
		self.quitRequested = YES;
		_cancel = 1;
		[self setStatus:@"Cancelling…" isError:NO];
		return;
	}

	[NSApp stopModalWithCode:NSModalResponseCancel];
}

- (BOOL)windowShouldClose:(NSWindow *)sender
{
	(void)sender;
	[self quit:nil];
	return NO;
}

@end

// ---- App setup --------------------------------------------------------------

static void HostSetup_BuildMenus(void)
{
	NSMenu *mainMenu = [[NSMenu alloc] init];

	NSMenu *appMenu = [[NSMenu alloc] initWithTitle:HOST_SETUP_PRODUCT];
	[appMenu addItemWithTitle:@"About " HOST_SETUP_PRODUCT action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
	[appMenu addItem:[NSMenuItem separatorItem]];
	[appMenu addItemWithTitle:@"Hide " HOST_SETUP_PRODUCT action:@selector(hide:) keyEquivalent:@"h"];
	NSMenuItem *hideOthers = [appMenu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
	hideOthers.keyEquivalentModifierMask = NSEventModifierFlagOption | NSEventModifierFlagCommand;
	[appMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
	[appMenu addItem:[NSMenuItem separatorItem]];
	[appMenu addItemWithTitle:@"Quit " HOST_SETUP_PRODUCT action:@selector(terminate:) keyEquivalent:@"q"];

	NSMenuItem *appItem = [[NSMenuItem alloc] init];
	appItem.submenu = appMenu;
	[mainMenu addItem:appItem];

	NSMenu *windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
	[windowMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
	[windowMenu addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];

	NSMenuItem *windowItem = [[NSMenuItem alloc] init];
	windowItem.submenu = windowMenu;
	[mainMenu addItem:windowItem];

	NSApp.mainMenu = mainMenu;
	NSApp.windowsMenu = windowMenu;
}

static void HostSetup_EnsureApp(void)
{
	if (NSApp != nil)
	{
		return;
	}

	[NSApplication sharedApplication];
	NSApp.activationPolicy = NSApplicationActivationPolicyRegular;
	HostSetup_BuildMenus();
	[[NSUserDefaults standardUserDefaults] registerDefaults:@{@"ApplePersistenceIgnoreState" : @YES}];
	[NSApp finishLaunching];
}

int HostSetup_IsBundled(void)
{
	@autoreleasepool
	{
		// NOTE: NSBundle also reads an Info.plist that merely sits next to a bare
		// executable, so require a real .app as well.
		NSBundle *bundle = NSBundle.mainBundle;
		return (bundle.bundleIdentifier != nil) && [bundle.bundlePath.pathExtension isEqualToString:@"app"];
	}
}

int HostSetup_Run(char *baseDir, size_t baseDirSize)
{
	@autoreleasepool
	{
		NSError *error = nil;
		NSURL *support = [NSFileManager.defaultManager URLForDirectory:NSApplicationSupportDirectory
		                                                      inDomain:NSUserDomainMask
		                                             appropriateForURL:nil
		                                                        create:YES
		                                                         error:&error];
		NSString *base = [support URLByAppendingPathComponent:HOST_SETUP_PRODUCT].path;

		if ((base == nil) || ![NSFileManager.defaultManager createDirectoryAtPath:base withIntermediateDirectories:YES attributes:nil error:&error])
		{
			HostSetup_ShowFatal("Could not create the " NATIVE_PRODUCT_NAME " folder in ~/Library/Application Support.", NULL);
			return 0;
		}

		if (snprintf(baseDir, baseDirSize, "%s", base.fileSystemRepresentation) >= (int)baseDirSize)
		{
			HostSetup_ShowFatal("The " NATIVE_PRODUCT_NAME " data folder path is too long.", NULL);
			return 0;
		}

		BOOL hasDisc = HostSetup_HasUsableDisc(base);
		BOOL optionHeld = (NSEvent.modifierFlags & NSEventModifierFlagOption) != 0;
		if (hasDisc && !optionHeld)
		{
			return 1;
		}

		HostSetup_EnsureApp();
		[NSApp activateIgnoringOtherApps:YES];

		HostSetupController *controller = [[HostSetupController alloc] initWithBaseDir:base hasExistingDisc:hasDisc];
		[controller.window makeKeyAndOrderFront:nil];
		NSModalResponse response = [NSApp runModalForWindow:controller.window];
		[controller.window orderOut:nil];

		if (response == NSModalResponseOK)
		{
			return 1;
		}

		// Cancel keeps the disc that is already installed.
		return hasDisc ? 1 : 0;
	}
}

void HostSetup_ShowFatal(const char *message, const char *logPath)
{
	@autoreleasepool
	{
		HostSetup_EnsureApp();
		[NSApp activateIgnoringOtherApps:YES];

		NSAlert *alert = [[NSAlert alloc] init];
		alert.alertStyle = NSAlertStyleCritical;
		alert.messageText = HOST_SETUP_PRODUCT @" stopped";
		alert.informativeText = [NSString stringWithUTF8String:message];
		[alert addButtonWithTitle:@"OK"];
		if (logPath != NULL)
		{
			[alert addButtonWithTitle:@"Show Log"];
		}

		if (([alert runModal] == NSAlertSecondButtonReturn) && (logPath != NULL))
		{
			NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:logPath]];
			[NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[ url ]];
		}
	}
}
