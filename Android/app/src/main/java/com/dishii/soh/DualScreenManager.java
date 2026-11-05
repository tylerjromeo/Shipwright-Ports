package com.dishii.soh;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.ViewGroup;
import android.view.WindowManager;

/**
 * Puts a full-screen Surface on a second physical display (e.g. the bottom screen of an AYN Thor) and hands it to
 * native code, which draws the ImGui menus/trackers there. Touches on that display are forwarded to ImGui.
 *
 * Native side: libultraship/src/ship/window/gui/SecondaryScreen.cpp.
 *
 * On single-screen devices no secondary display is found and this class does nothing.
 */
public class DualScreenManager {
    private static final String TAG = "DualScreenManager";

    // Touch actions understood by native code.
    private static final int TOUCH_DOWN = 0;
    private static final int TOUCH_MOVE = 1;
    private static final int TOUCH_UP = 2;

    private static native void nativeSurfaceChanged(Surface surface, int width, int height, float densityScale);
    private static native void nativeSurfaceDestroyed();
    private static native void nativeTouch(int action, float x, float y);

    private final Activity activity;
    private final DisplayManager displayManager;
    private SecondaryPresentation presentation;
    private boolean started;

    private final DisplayManager.DisplayListener displayListener = new DisplayManager.DisplayListener() {
        @Override
        public void onDisplayAdded(int displayId) {
            if (presentation == null) {
                Display display = displayManager.getDisplay(displayId);
                if (isUsableSecondaryDisplay(display)) {
                    showPresentation(display);
                }
            }
        }

        @Override
        public void onDisplayRemoved(int displayId) {
            if (presentation != null && presentation.getDisplay() != null
                    && presentation.getDisplay().getDisplayId() == displayId) {
                dismissPresentation();
            }
        }

        @Override
        public void onDisplayChanged(int displayId) {
            // Size changes arrive through the SurfaceView's own callbacks.
        }
    };

    public DualScreenManager(Activity activity) {
        this.activity = activity;
        this.displayManager = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
    }

    private static boolean isUsableSecondaryDisplay(Display display) {
        // Don't filter on getState(): the AYN Thor reports its bottom screen as STATE_OFF until something is shown on it.
        return display != null && display.getDisplayId() != Display.DEFAULT_DISPLAY;
    }

    private Display findSecondaryDisplay() {
        // Prefer displays the system marks as suitable for presentations.
        for (Display display : displayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            if (isUsableSecondaryDisplay(display)) {
                return display;
            }
        }
        // Some dual-screen devices expose their second panel without the presentation category.
        for (Display display : displayManager.getDisplays()) {
            if (isUsableSecondaryDisplay(display) && (display.getFlags() & Display.FLAG_PRIVATE) == 0) {
                return display;
            }
        }
        return null;
    }

    public void start() {
        if (displayManager == null || started) {
            return;
        }
        started = true;
        displayManager.registerDisplayListener(displayListener, null);
        Display display = findSecondaryDisplay();
        if (display != null) {
            showPresentation(display);
        } else {
            Log.i(TAG, "No secondary display found");
        }
    }

    public void stop() {
        if (displayManager != null && started) {
            displayManager.unregisterDisplayListener(displayListener);
        }
        started = false;
        dismissPresentation();
    }

    private float densityScaleFor(Display secondary) {
        DisplayMetrics primaryMetrics = new DisplayMetrics();
        activity.getWindowManager().getDefaultDisplay().getRealMetrics(primaryMetrics);
        DisplayMetrics secondaryMetrics = new DisplayMetrics();
        secondary.getRealMetrics(secondaryMetrics);
        if (primaryMetrics.densityDpi <= 0 || secondaryMetrics.densityDpi <= 0) {
            return 1.0f;
        }
        return (float) secondaryMetrics.densityDpi / (float) primaryMetrics.densityDpi;
    }

    private void showPresentation(Display display) {
        dismissPresentation();
        try {
            Log.i(TAG, "Showing UI on display " + display.getDisplayId() + " (" + display.getName() + ")");
            presentation = new SecondaryPresentation(activity, display, densityScaleFor(display));
            presentation.show();
        } catch (Exception e) {
            Log.w(TAG, "Failed to show secondary display presentation", e);
            presentation = null;
        }
    }

    private void dismissPresentation() {
        if (presentation != null) {
            try {
                presentation.dismiss();
            } catch (Exception e) {
                Log.w(TAG, "Error dismissing secondary display presentation", e);
            }
            presentation = null;
        }
    }

    private static class SecondaryPresentation extends Presentation {
        private final float densityScale;

        SecondaryPresentation(Context outerContext, Display display, float densityScale) {
            super(outerContext, display);
            this.densityScale = densityScale;
        }

        @SuppressLint("ClickableViewAccessibility")
        @Override
        protected void onCreate(Bundle savedInstanceState) {
            super.onCreate(savedInstanceState);

            // Keep controller/keyboard focus on the game's activity: this window only needs touch input.
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                    | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

            SurfaceView surfaceView = new SurfaceView(getContext());
            surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
                @Override
                public void surfaceCreated(SurfaceHolder holder) {
                    // surfaceChanged() always follows with the real size.
                }

                @Override
                public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                    nativeSurfaceChanged(holder.getSurface(), width, height, densityScale);
                }

                @Override
                public void surfaceDestroyed(SurfaceHolder holder) {
                    nativeSurfaceDestroyed();
                }
            });
            surfaceView.setOnTouchListener((view, event) -> {
                // Single-pointer: ImGui only has one mouse. Follow the first finger down.
                int action;
                switch (event.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        action = TOUCH_DOWN;
                        break;
                    case MotionEvent.ACTION_MOVE:
                        action = TOUCH_MOVE;
                        break;
                    case MotionEvent.ACTION_UP:
                    case MotionEvent.ACTION_CANCEL:
                        action = TOUCH_UP;
                        break;
                    default:
                        return true;
                }
                nativeTouch(action, event.getX(0), event.getY(0));
                return true;
            });
            setContentView(surfaceView, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT));
        }
    }
}
