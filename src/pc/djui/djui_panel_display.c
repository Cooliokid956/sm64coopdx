#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "pc/gfx/gfx_window_manager.h"
#include "pc/pc_main.h"
#include "pc/utils/misc.h"
#include "pc/configfile.h"

#define OPTION_ORIGINAL_UNSET ((u32)-1)

static struct DjuiRect* sWindowPosCont = NULL;
static struct DjuiRect* sWindowResCont = NULL;
static struct DjuiSelectionbox* sFullscreenResBox = NULL;
static struct DjuiInputbox* sFrameLimitInput = NULL;
static struct DjuiSelectionbox* sInterpolationSelectionBox = NULL;
static struct DjuiText* sRestartText = NULL;
static u32 sMsaaSelection = 0;
static u32 sMsaaOriginal = OPTION_ORIGINAL_UNSET;
static u32 sGfxWindowBackendOriginal = OPTION_ORIGINAL_UNSET;

static void djui_panel_display_apply(UNUSED struct DjuiBase* caller) {
    configWindow.settings_changed = true;

    djui_base_set_visible(&sWindowPosCont->base, !configWindow.fullscreen);
    djui_base_set_visible(&sWindowResCont->base, !configWindow.fullscreen);
    djui_base_set_visible(&sFullscreenResBox->base, configWindow.fullscreen);
}

static void djui_panel_display_framerate_mode_change(UNUSED struct DjuiBase* caller) {
    djui_base_set_enabled(&sFrameLimitInput->base, configFramerateMode == RRM_MANUAL);
    djui_base_set_enabled(&sInterpolationSelectionBox->base, (configFrameLimit > 30 || configFramerateMode != RRM_MANUAL));
}

static void djui_panel_display_frame_limit_text_change(struct DjuiBase* caller) {
    djui_input_number_text_change(caller);
    djui_base_set_enabled(&sInterpolationSelectionBox->base, (configFrameLimit > 30 || configFramerateMode != RRM_MANUAL));
}

static void djui_panel_window_limit_text_change(struct DjuiBase* caller) {
    djui_input_number_text_change(caller);
    configWindow.settings_changed = true;
}

static void djui_panel_display_update_restart_text(UNUSED struct DjuiBase* caller) {
    if (sMsaaOriginal != configWindow.msaa || sGfxWindowBackendOriginal != configGraphicsBackend) {
        djui_text_set_text(sRestartText, DLANG(DISPLAY, MUST_RESTART));
    } else {
        djui_text_set_text(sRestartText, "");
    }
}

static void djui_panel_display_msaa_change(struct DjuiBase* caller) {
    switch (sMsaaSelection) {
        case 1:  configWindow.msaa = 2;  break;
        case 2:  configWindow.msaa = 4;  break;
        case 3:  configWindow.msaa = 8;  break;
        case 4:  configWindow.msaa = 16; break;
        default: configWindow.msaa = 0;  break;
    }

    djui_panel_display_update_restart_text(caller);
}

void djui_panel_display_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create(DLANG(DISPLAY, DISPLAY), false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    // save original msaa value
    if (sMsaaOriginal == OPTION_ORIGINAL_UNSET) { sMsaaOriginal = configWindow.msaa; }
    if (sGfxWindowBackendOriginal == OPTION_ORIGINAL_UNSET) { sGfxWindowBackendOriginal = configGraphicsBackend; }

    {
        struct DjuiRect* windowPosCont = djui_rect_container_create(body, 32);
        {
            struct DjuiText* text1 = djui_text_create(&windowPosCont->base, DLANG(DISPLAY, POSITION));
            djui_base_set_size_type(&text1->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
            djui_base_set_color(&text1->base, 220, 220, 220, 255);
            djui_base_set_size(&text1->base, 0.585f, 64);
            djui_base_set_alignment(&text1->base, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);
            djui_text_set_drop_shadow(text1, 64, 64, 64, 100);

            struct DjuiRect* inputsCont = djui_rect_container_create(&windowPosCont->base, 32);
            djui_base_set_alignment(&inputsCont->base, DJUI_HALIGN_RIGHT, DJUI_VALIGN_TOP);
            djui_base_set_size_type(&inputsCont->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
            djui_base_set_size(&inputsCont->base, 0.45f, 32);
            {
                struct DjuiInputNumber* x = djui_input_number_create(&inputsCont->base, &configWindow.x, 0, 1920);
                x->input.base.width.type = DJUI_SVT_RELATIVE;
                x->input.base.width.value = 0.45f;
                djui_interactable_hook_value_change(&x->input.base, djui_panel_window_limit_text_change);
    
                struct DjuiInputNumber* y = djui_input_number_create(&inputsCont->base, &configWindow.y, 0, 1080);
                y->input.base.hAlign = DJUI_HALIGN_RIGHT;
                y->input.base.width.type = DJUI_SVT_RELATIVE;
                y->input.base.width.value = 0.45f;
                djui_interactable_hook_value_change(&y->input.base, djui_panel_window_limit_text_change);
            }

            sWindowPosCont = windowPosCont;
        }

        struct DjuiRect* windowResCont = djui_rect_container_create(body, 32);
        {
            struct DjuiText* text1 = djui_text_create(&windowResCont->base, DLANG(DISPLAY, RESOLUTION));
            djui_base_set_size_type(&text1->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
            djui_base_set_color(&text1->base, 220, 220, 220, 255);
            djui_base_set_size(&text1->base, 0.585f, 64);
            djui_base_set_alignment(&text1->base, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);
            djui_text_set_drop_shadow(text1, 64, 64, 64, 100);

            struct DjuiRect* inputsCont = djui_rect_container_create(&windowResCont->base, 32);
            djui_base_set_alignment(&inputsCont->base, DJUI_HALIGN_RIGHT, DJUI_VALIGN_TOP);
            djui_base_set_size_type(&inputsCont->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
            djui_base_set_size(&inputsCont->base, 0.45f, 32);
            {
                struct DjuiInputNumber* width = djui_input_number_create(&inputsCont->base, &configWindow.w, 0, 1920);
                width->input.base.width.type = DJUI_SVT_RELATIVE;
                width->input.base.width.value = 0.45f;
                djui_interactable_hook_value_change(&width->input.base, djui_panel_window_limit_text_change);
    
                struct DjuiInputNumber* height = djui_input_number_create(&inputsCont->base, &configWindow.h, 0, 1080);
                height->input.base.width.type = DJUI_SVT_RELATIVE;
                height->input.base.width.value = 0.45f;
                height->input.base.hAlign = DJUI_HALIGN_RIGHT;
                djui_interactable_hook_value_change(&height->input.base, djui_panel_window_limit_text_change);
            }

            sWindowResCont = windowResCont;
        }

        SDL_DisplayMode mode;
        int display = SDL_GetWindowDisplayIndex(gfx_wm_get_window()); printf("display %d\n", display);
        int modes = SDL_GetNumDisplayModes(display); printf("%d mode\ns", modes);

        char** displayModes = calloc(modes, sizeof(char*));
        for (int i = 0; i < modes; i++) {
            SDL_GetDisplayMode(display, i, &mode);

            displayModes[i] = malloc(sizeof(char) * 20);
            snprintf(displayModes[i], 20, "%dx%d@%dhz", mode.w, mode.h, mode.refresh_rate);
            printf("mode %d: %dx%d@%dhz (%s)\n", i, mode.w, mode.h, mode.refresh_rate, SDL_GetPixelFormatName(mode.format));
        }

        sFullscreenResBox = djui_selectionbox_create(body, DLANG(DISPLAY, RESOLUTION), displayModes, modes, &configWindow.display_mode, djui_panel_display_apply);

        for (int i = 0; i < modes; i++) {
            free(displayModes[i]);
        }

        free(displayModes);
        
        if (configWindow.fullscreen) {
            djui_base_set_visible(&sWindowPosCont->base, false);
            djui_base_set_visible(&sWindowResCont->base, false);
        } else {
            djui_base_set_visible(&sFullscreenResBox->base, false);
        }

        djui_checkbox_create(body, DLANG(DISPLAY, BORDERLESS), &configWindow.borderless, djui_panel_display_apply);
        djui_checkbox_create(body, DLANG(DISPLAY, FULLSCREEN), &configWindow.fullscreen, djui_panel_display_apply);
        djui_checkbox_create(body, DLANG(DISPLAY, FORCE_4BY3), &configForce4By3, djui_panel_display_apply);
        djui_checkbox_create(body, DLANG(DISPLAY, SHOW_FPS), &configShowFPS, NULL);
        djui_checkbox_create(body, DLANG(DISPLAY, VSYNC), &configWindow.vsync, djui_panel_display_apply);

#if defined(_WIN32)
        static char *gfxBackendChoices[] = { "OpenGL", "DirectX 11" };
        djui_selectionbox_create(body, DLANG(DISPLAY, GRAPHICS_BACKEND), gfxBackendChoices, ARRAY_COUNT(gfxBackendChoices), &configGraphicsBackend, djui_panel_display_update_restart_text);
#endif

        char* framerateModeChoices[3] = { DLANG(DISPLAY, AUTO), DLANG(DISPLAY, MANUAL), DLANG(DISPLAY, UNCAPPED) };
        djui_selectionbox_create(body, DLANG(DISPLAY, FRAMERATE_MODE), framerateModeChoices, 3, &configFramerateMode, djui_panel_display_framerate_mode_change);

        struct DjuiRect* frameLimitRect = djui_rect_container_create(body, 32);
        {
            if (configFrameLimit < 30) { configFrameLimit = 30; }
            if (configFrameLimit > 3000) { configFrameLimit = 3000; }
            struct DjuiText* text1 = djui_text_create(&frameLimitRect->base, DLANG(DISPLAY, FRAME_LIMIT));
            djui_base_set_size_type(&text1->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
            djui_base_set_color(&text1->base, 220, 220, 220, 255);
            djui_base_set_size(&text1->base, 0.585f, 64);
            djui_base_set_alignment(&text1->base, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);
            djui_text_set_drop_shadow(text1, 64, 64, 64, 100);

            struct DjuiInputNumber* number = djui_input_number_create(&frameLimitRect->base, &configFrameLimit, 30, 3000);
            struct DjuiInputbox* inputbox = &number->input;
            djui_base_set_size_type(&inputbox->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
            djui_base_set_size(&inputbox->base, 0.45f, 32);
            djui_base_set_alignment(&inputbox->base, DJUI_HALIGN_RIGHT, DJUI_VALIGN_TOP);
            djui_interactable_hook_value_change(&inputbox->base, djui_panel_display_frame_limit_text_change);
            djui_base_set_enabled(&inputbox->base, configFramerateMode == RRM_MANUAL);
            sFrameLimitInput = inputbox;
        }

        char* interpChoices[2] = { DLANG(DISPLAY, FAST), DLANG(DISPLAY, ACCURATE) };
        struct DjuiSelectionbox* selectionbox1 = djui_selectionbox_create(body, DLANG(DISPLAY, INTERPOLATION), interpChoices, 2, &configInterpolationMode, NULL);
        djui_base_set_enabled(&selectionbox1->base, (configFrameLimit > 30 || configFramerateMode != RRM_MANUAL));
        sInterpolationSelectionBox = selectionbox1;

        char* filterChoices[3] = { DLANG(DISPLAY, NEAREST), DLANG(DISPLAY, LINEAR), DLANG(DISPLAY, TRIPOINT) };
        djui_selectionbox_create(body, DLANG(DISPLAY, FILTERING), filterChoices, 3, &configFiltering, NULL);

        int maxMsaa = gfx_wm_get_max_msaa();
        if (maxMsaa >= 2) {
            if      (configWindow.msaa >= 16) { sMsaaSelection = 4; }
            else if (configWindow.msaa >=  8) { sMsaaSelection = 3; }
            else if (configWindow.msaa >=  4) { sMsaaSelection = 2; }
            else if (configWindow.msaa >=  2) { sMsaaSelection = 1; }
            else                              { sMsaaSelection = 0; }

            int choiceCount = 2;
            if      (maxMsaa >= 16) { choiceCount = 5; }
            else if (maxMsaa >= 8)  { choiceCount = 4; }
            else if (maxMsaa >= 4)  { choiceCount = 3; }

            char* msaaChoices[5] = { DLANG(DISPLAY, OFF), "2x", "4x", "8x", "16x" };
            djui_selectionbox_create(body, DLANG(DISPLAY, ANTIALIASING), msaaChoices, choiceCount, &sMsaaSelection, djui_panel_display_msaa_change);
        }

        char* drawDistanceChoices[] = {
            DLANG(DISPLAY, D0P5X),
            DLANG(DISPLAY, D1X),
            DLANG(DISPLAY, D1P5X),
            DLANG(DISPLAY, D3X),
            DLANG(DISPLAY, D10X),
            DLANG(DISPLAY, D100X),
            DLANG(DISPLAY, INFINITE),
        };
        djui_selectionbox_create(body, DLANG(DISPLAY, DRAW_DISTANCE), drawDistanceChoices, ARRAY_COUNT(drawDistanceChoices), &configDrawDistance, NULL);

        djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);

        sRestartText = djui_text_create(body, "");
        djui_text_set_alignment(sRestartText, DJUI_HALIGN_CENTER, DJUI_VALIGN_TOP);
        djui_base_set_color(&sRestartText->base, 255, 100, 100, 255);
        djui_base_set_size_type(&sRestartText->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&sRestartText->base, 1.0f, 64);
    }

    // force the restart text to update
    djui_panel_display_update_restart_text(body);

    djui_panel_add(caller, panel, NULL);
}
