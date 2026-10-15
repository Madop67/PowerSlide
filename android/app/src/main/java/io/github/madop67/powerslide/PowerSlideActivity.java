package io.github.madop67.powerslide;

import org.libsdl.app.SDLActivity;

/**
 * PowerSlide runs as SDL's activity. The game, the wasm host and SDL itself
 * are all in libmain.so (SDL is linked statically), so there is nothing else
 * to load.
 */
public class PowerSlideActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }
}
