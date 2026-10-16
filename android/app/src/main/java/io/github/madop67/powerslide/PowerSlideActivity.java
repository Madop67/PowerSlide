package io.github.madop67.powerslide;

import android.os.Bundle;

import org.libsdl.app.SDLActivity;

/**
 * PowerSlide runs as SDL's activity. The game, the wasm host and SDL itself
 * are all in libmain.so (SDL is linked statically), so there is nothing else
 * to load.
 */
public class PowerSlideActivity extends SDLActivity {
    /** The Dual Screen tab's second display (see PresentationBridge). */
    private PresentationBridge mPresentationBridge;

    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // NOTE: Its native methods live in libmain.so, which super.onCreate() loaded.
        if (!mBrokenLibraries) {
            mPresentationBridge = new PresentationBridge(this);
        }
    }

    @Override
    protected void onStart() {
        super.onStart();
        if (mPresentationBridge != null) {
            mPresentationBridge.onStart();
        }
    }

    @Override
    protected void onStop() {
        if (mPresentationBridge != null) {
            mPresentationBridge.onStop();
        }
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        if (mPresentationBridge != null) {
            mPresentationBridge.destroy();
            mPresentationBridge = null;
        }
        super.onDestroy();
    }
}
