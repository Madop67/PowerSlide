package io.github.madop67.powerslide;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.graphics.Point;
import android.hardware.display.DisplayManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Display;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;

/**
 * The "Second display" mode of the Dual Screen tab on Android: the bottom
 * screen of a dual-screen handheld (AYN Thor), or any other display the game
 * is not on. SDL only knows the activity's own display, so the wasm host
 * (platform/wasm_host/host_presentation_android.c) reports this display and a
 * window on it to the game, and draws into the Presentation's surface with its
 * own EGL context.
 *
 * Runs on the UI thread. The game thread calls open() and close() through JNI.
 */
final class PresentationBridge implements DisplayManager.DisplayListener {
    private static final String TAG = "PowerSlide";

    private static PresentationBridge sInstance;

    private final Activity mActivity;
    private final DisplayManager mDisplayManager;
    private final Handler mHandler = new Handler(Looper.getMainLooper());

    private Display mTarget;
    private Presentation mPresentation;
    /** The game wants the second window open. */
    private boolean mWanted;
    /** The activity is visible; the presentation is only shown while it is. */
    private boolean mStarted;

    // Game -> UI thread (static: called through JNI).
    static void open() {
        post(true);
    }

    static void close() {
        post(false);
    }

    private static void post(final boolean wanted) {
        final PresentationBridge bridge = sInstance;
        if (bridge != null) {
            bridge.mHandler.post(() -> bridge.setWanted(wanted));
        }
    }

    // Implemented in host_presentation_android.c.
    private static native void nativeDisplayChanged(boolean present, String name, int width, int height);
    private static native void nativeSurfaceChanged(Surface surface, int width, int height);
    private static native void nativeSurfaceDestroyed();
    private static native void nativeTouch(int action, float x, float y);

    private static final int TOUCH_DOWN = 0;
    private static final int TOUCH_MOVE = 1;
    private static final int TOUCH_UP = 2;

    PresentationBridge(Activity activity) {
        mActivity = activity;
        mDisplayManager = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
        sInstance = this;
        logDisplays();
        mDisplayManager.registerDisplayListener(this, mHandler);
        mTarget = findTarget();
        reportTarget();
    }

    void onStart() {
        mStarted = true;
        update();
    }

    void onStop() {
        mStarted = false;
        update();
    }

    void destroy() {
        mDisplayManager.unregisterDisplayListener(this);
        mStarted = false;
        mWanted = false;
        update();
        if (sInstance == this) {
            sInstance = null;
        }
    }

    private void setWanted(boolean wanted) {
        mWanted = wanted;
        update();
    }

    // ---- displays ----------------------------------------------------------

    @SuppressWarnings("deprecation")
    private int gameDisplayId() {
        return mActivity.getWindowManager().getDefaultDisplay().getDisplayId();
    }

    /** A presentation display first, else any other display than the game's. */
    private Display findTarget() {
        final int game = gameDisplayId();
        for (Display display : mDisplayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            if (display.getDisplayId() != game && display.isValid()) {
                return display;
            }
        }
        for (Display display : mDisplayManager.getDisplays()) {
            if (display.getDisplayId() != game && display.isValid()) {
                return display;
            }
        }
        return null;
    }

    /** Logged so a device that lists its second screen differently can be diagnosed from logcat. */
    private void logDisplays() {
        final int game = gameDisplayId();
        for (Display display : mDisplayManager.getDisplays()) {
            final Point size = new Point();
            display.getRealSize(size);
            Log.i(TAG, "Display " + display.getDisplayId() + " \"" + display.getName() + "\" " + size.x + "x" + size.y
                    + " flags=0x" + Integer.toHexString(display.getFlags()) + " state=" + display.getState()
                    + (display.getDisplayId() == game ? " (game)" : ""));
        }
        for (Display display : mDisplayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            Log.i(TAG, "Presentation display: " + display.getDisplayId());
        }
    }

    private void reportTarget() {
        if (mTarget == null) {
            nativeDisplayChanged(false, null, 0, 0);
            return;
        }
        final Point size = new Point();
        mTarget.getRealSize(size);
        nativeDisplayChanged(true, mTarget.getName(), size.x, size.y);
    }

    private void refreshTarget() {
        final Display target = findTarget();
        final int oldId = (mTarget != null) ? mTarget.getDisplayId() : -1;
        final int newId = (target != null) ? target.getDisplayId() : -1;
        mTarget = target;
        if (oldId != newId) {
            Log.i(TAG, "Second display: " + ((target != null) ? (target.getDisplayId() + " \"" + target.getName() + "\"") : "none"));
            dismiss();
        }
        reportTarget();
        update();
    }

    @Override
    public void onDisplayAdded(int displayId) {
        logDisplays();
        refreshTarget();
    }

    @Override
    public void onDisplayRemoved(int displayId) {
        refreshTarget();
    }

    @Override
    public void onDisplayChanged(int displayId) {
        if ((mTarget != null) && (displayId == mTarget.getDisplayId())) {
            refreshTarget();
        }
    }

    // ---- presentation ------------------------------------------------------

    private void update() {
        final boolean show = mWanted && mStarted && (mTarget != null);
        if (show && (mPresentation == null)) {
            show();
        } else if (!show) {
            dismiss();
        }
    }

    @SuppressWarnings("deprecation")
    private void show() {
        final Presentation presentation = new Presentation(mActivity, mTarget);
        final SurfaceView view = new SurfaceView(presentation.getContext());
        view.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                nativeSurfaceChanged(holder.getSurface(), width, height);
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                nativeSurfaceDestroyed();
            }
        });
        view.setOnTouchListener((v, event) -> {
            // One finger, like a mouse: the first pointer only.
            switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                nativeTouch(TOUCH_DOWN, event.getX(0), event.getY(0));
                break;
            case MotionEvent.ACTION_MOVE:
                nativeTouch(TOUCH_MOVE, event.getX(0), event.getY(0));
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                nativeTouch(TOUCH_UP, event.getX(0), event.getY(0));
                break;
            default:
                break;
            }
            return true;
        });
        presentation.setContentView(view);
        // Not focusable, so the game's screen keeps the keys and controllers; taps still arrive.
        presentation.getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        presentation.getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        // The system dismisses it when its display goes away.
        presentation.setOnDismissListener(dialog -> {
            if (mPresentation == presentation) {
                mPresentation = null;
            }
        });
        try {
            presentation.show();
            mPresentation = presentation;
            Log.i(TAG, "Second screen shown on display " + mTarget.getDisplayId());
        } catch (WindowManager.InvalidDisplayException e) {
            Log.w(TAG, "Second screen: display " + mTarget.getDisplayId() + " is gone", e);
        }
    }

    private void dismiss() {
        if (mPresentation != null) {
            final Presentation presentation = mPresentation;
            mPresentation = null;
            presentation.dismiss();
        }
    }
}
